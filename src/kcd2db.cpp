#include <windows.h>
#include <atomic>
#include <chrono>
#include <bit>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <libmem/libmem.h>

#define KCD2_ENV_IMPORT
#include "db/LuaDB.h"
#include <cryengine/env.h>
#include <cryengine/IGame.h>
#include "log/log.h"

namespace
{
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
}

// 将字节序列转换为 libmem 兼容的模式
std::string bytes_to_pattern(const unsigned char* bytes, const size_t size)
{
    std::string pattern;
    pattern.reserve(size * 3);
    for (size_t i = 0; i < size; ++i)
    {
        pattern += kHexDigits[(bytes[i] & 0xF0) >> 4];
        pattern += kHexDigits[bytes[i] & 0x0F];
        if (i + 1 < size)
        {
            pattern += ' ';
        }
    }
    return pattern;
}

std::optional<uintptr_t> find_env_addr()
{
    lm_module_t module;
    constexpr auto CLIENT_DLL = "WHGame.DLL";
    while (!LM_FindModule(CLIENT_DLL, &module))
    {
        std::this_thread::yield();
    }
    // --- 步骤 1: 找到 "exec autoexec.cfg" 字符串 ---
    constexpr std::string_view search_string = "exec autoexec.cfg";
    const auto string_pattern = bytes_to_pattern(
        reinterpret_cast<const unsigned char*>(search_string.data()),
        search_string.length()
    );
    LogDebug("Scanning for anchor string pattern: %s", string_pattern.c_str());
    const uintptr_t string_addr = LM_SigScan(string_pattern.c_str(), module.base, module.size);
    if (!string_addr)
    {
        LogError("Could not find the anchor string 'exec autoexec.cfg'.");
        return std::nullopt;
    }
    LogInfo("Found anchor string at: 0x%llX", string_addr);
    // --- 步骤 2: 找到引用该字符串的 LEA 指令 ---
    uintptr_t lea_instruction_addr = 0;
    uintptr_t scan_start = module.base;
    while (scan_start < module.base + module.size)
    {
        const uintptr_t potential_lea = LM_SigScan("48 8D 15 ? ? ? ?", scan_start, module.size - (scan_start - module.base));
        if (!potential_lea)
        {
            LogError("Could not find any cross-references (LEA) to the anchor string.");
            return std::nullopt;
        }
        int32_t rip_offset;
        LM_ReadMemory(potential_lea + 3, reinterpret_cast<lm_byte_t*>(&rip_offset), sizeof(rip_offset));
        if (const uintptr_t referenced_addr = potential_lea + 7 + rip_offset; referenced_addr == string_addr)
        {
            lea_instruction_addr = potential_lea;
            LogInfo("Found LEA instruction referencing the string at: 0x%llX", lea_instruction_addr);
            break;
        }
        scan_start = potential_lea + 1;
    }
    if (!lea_instruction_addr)
    {
        LogError("Failed to locate the correct LEA instruction after scanning.");
        return std::nullopt;
    }
    // --- 步骤 3: 检查上下文并定位 pConsole 的 MOV 指令 ---
    uintptr_t pConsole_mov_addr = 0;
    // 检查是否为 "新版模式" (V1.4+)
    // 特征: lea 指令前 7 字节是 'mov r10, [rdx+118h]' (4C 8B 92 18 01 00 00)
    unsigned char v1_6_context_bytes[7];
    if (LM_ReadMemory(lea_instruction_addr - 7, v1_6_context_bytes, 7) &&
        memcmp(v1_6_context_bytes, "\x4C\x8B\x92\x18\x01\x00\x00", 7) == 0)
    {
        LogInfo("Version context matches 'New' (V1.4+) pattern.");
        // 在 V1.4 中, 'mov rcx, cs:qword_...' 在 lea 指令前 0x17 (23) 字节
        // 0x180C9D574 (lea) - 0x180C9D55D (mov) = 0x17
        pConsole_mov_addr = lea_instruction_addr - 0x17;
    }
    else
    {
        // 否则，假定为 "旧版模式" (V1.2, V1.3, etc.)
        // 特征: lea 指令前 7 字节是 'mov rcx, cs:qword_...' (48 8B 0D ? ? ? ?)
        unsigned char old_version_opcode[3];
        if (LM_ReadMemory(lea_instruction_addr - 7, old_version_opcode, 3) &&
            memcmp(old_version_opcode, "\x48\x8B\x0D", 3) == 0)
        {
            LogInfo("Version context matches 'Old' (V1.3, V1.2, etc.) pattern.");
            // 在旧版模式中，该指令就在 lea 前 7 个字节
            pConsole_mov_addr = lea_instruction_addr - 7;
        }
    }
    if (!pConsole_mov_addr)
    {
        LogError("Could not identify any known version context around the LEA instruction.");
        return std::nullopt;
    }
    LogInfo("Found pConsole MOV instruction at: 0x%llX", pConsole_mov_addr);
    // --- 步骤 4: 从 MOV 指令计算 gEnv 地址 ---
    int32_t rip_offset;
    if (!LM_ReadMemory(pConsole_mov_addr + 3, reinterpret_cast<lm_byte_t*>(&rip_offset), sizeof(rip_offset)))
    {
        LogError("Failed to read RIP offset from the MOV instruction.");
        return std::nullopt;
    }
    const uintptr_t console_ptr_addr = pConsole_mov_addr + 7 + rip_offset;

    // gEnv 的基地址 = gEnv->pConsole 的地址 - pConsole 在结构体中的偏移 (0xA8)
    return console_ptr_addr - 0xA8;
}

using CompleteInitFunc = bool(__thiscall*)(IGame*);
constexpr int COMPLETE_INIT_INDEX = 4; // 第5个函数
// 原始函数指针
static CompleteInitFunc OriginalCompleteInit = nullptr;
static std::atomic<LuaDB*> gLuaDB{nullptr};
// 钩子函数
bool __thiscall Hooked_CompleteInit(IGame* pThis)
{
    LogDebug("Hooked_CompleteInit");
    LuaDB* luaDB = gLuaDB.load(std::memory_order_acquire);
    while (luaDB == nullptr)
    {
        std::this_thread::yield();
        luaDB = gLuaDB.load(std::memory_order_acquire);
    }
    luaDB->RegisterLuaAPI();
    LogInfo("Hooked_CompleteInit completed");
    return OriginalCompleteInit(pThis);
}

void start()
{
    LogDebug("Main thread started");
    if (const auto env_addr = find_env_addr())
    {
        LogDebug("Found environment address: 0x%llX", *env_addr);

        const auto* env_ptr = reinterpret_cast<SSystemGlobalEnvironment*>(env_addr.value());

        while (env_ptr->pGame == nullptr)
        {
            std::this_thread::yield();
        }
        LogDebug("Game Started");

        gEnv = *env_ptr;
        IGame* pGame = gEnv->pGame;

        void** vTable = *reinterpret_cast<void***>(pGame);
        DWORD initialOldProtect;
        VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*),PAGE_EXECUTE_READWRITE, &initialOldProtect);
        do
        {
            DWORD tempOldProtect;
            VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*),PAGE_EXECUTE_READWRITE, &tempOldProtect);
            static_assert(sizeof(CompleteInitFunc) == sizeof(void*), "Function pointer size mismatch");
            OriginalCompleteInit = std::bit_cast<CompleteInitFunc>(vTable[COMPLETE_INIT_INDEX]);
        }
        while (InterlockedCompareExchangePointer(
            &vTable[COMPLETE_INIT_INDEX],
            reinterpret_cast<PVOID>(&Hooked_CompleteInit),
            reinterpret_cast<PVOID>(OriginalCompleteInit)) != OriginalCompleteInit);

        VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*), initialOldProtect, nullptr);

        LogDebug("Hooked CompleteInit function");
        const auto luaDB = new LuaDB();
        gLuaDB.store(luaDB, std::memory_order_release);
        LogInfo("LuaDB initialized");

        while (env_ptr->pGame->GetIGameFramework() == nullptr
            || !env_ptr->pGame->GetIGameFramework()->IsGameStarted())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LogDebug("Game Framework Started");

        if (!luaDB->isRegistered())
        {
            LogWarn("LuaDB is not registered, will register now");
            luaDB->RegisterLuaAPI();
        }
    }
    else
    {
        LogError("Failed to find environment address");
    }
}

// 主要逻辑线程函数
DWORD WINAPI main_thread(LPVOID)
{
    try
    {
        start();
    }
    catch (const std::exception& e)
    {
        LogError("Flush: Error during flush: %s", e.what());
        return 1;
    }
    catch (...)
    {
        LogError("Flush: Unknown error during flush");
        return 1;
    }
    return 0;
}


BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, [](LPVOID /*unused*/) -> DWORD
        {
            Log_init();
            LogDebug("DLL attached");
            // 创建主工作线程
            // ReSharper disable once CppLocalVariableMayBeConst
            if (HANDLE hThread = CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr))
            {
                CloseHandle(hThread);
            }
            else
            {
                LogError("Failed to create main thread");
            }
            return 0;
        }, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log_close();
        if (gEnv->pGame)
        {
            CompleteInitFunc* vTable = *reinterpret_cast<CompleteInitFunc**>(gEnv->pGame);
            // 恢复原函数
            DWORD oldProtect;
            VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(CompleteInitFunc), PAGE_READWRITE, &oldProtect);
            vTable[COMPLETE_INIT_INDEX] = OriginalCompleteInit;
            VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(CompleteInitFunc), oldProtect, nullptr);
        }
    }
    return TRUE;
}
