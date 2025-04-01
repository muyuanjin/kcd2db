#include <windows.h>
#include <cstdio>
#include <mutex>
#include <optional>
#include <cassert>
#include <libmem/libmem.h>

#define KCD2_ENV_IMPORT
#include "db/LuaDB.h"
#include "kcd2/env.h"
#include "kcd2/IGame.h"
#include "log/log.h"

std::optional<uintptr_t> find_env_addr()
{
    lm_module_t module;
    constexpr auto CLIENT_DLL = "WHGame.DLL";
    // 持续尝试查找模块
    while (!LM_FindModule(CLIENT_DLL, &module))
    {
        std::this_thread::yield();
    }
    // 通常会找到两个地址，不过两个地址其实通过RIP之后的偏移是一样的，都是指向 gEnv->pConsole 的 qword_1848A7C68
    const auto pattern = "48 8B 0D ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 45 33 C9 45 33 C0 4C 8B 11";
    const auto scan_address = LM_SigScan(pattern, module.base, module.size);
    if (!scan_address)
    {
        LogError("Signature scan failed");
        return std::nullopt;
    }
    int32_t rip_offset;
    if (!LM_ReadMemory(scan_address + 3, reinterpret_cast<lm_byte_t*>(&rip_offset), sizeof(rip_offset)))
    {
        LogError("Failed to read RIP offset");
        return std::nullopt;
    }
    // RIP 相对寻址公式：TargetAddress = RIP + Offset
    // RIP = scan_address + 操作码 48 8B 0D 占 3 字节 + 偏移占 4 字节
    const auto console_addr = scan_address + 3 + 4 + rip_offset;
    // 根据1.2.2版本的 WHGame.DLL IDA Pro 分析结果 ，p console 的地址偏移是 0x48A7C68
    // lea rdx, aExecAutoexecCf ; "exec autoexec.cfg" 命令上一行就是 pConsole 的qword地址
    // 因为 gEnv 对象是全局变量被放在了.data 段, gEnv->pConsole->ExecuteString("exec autoexec.cfg");
    // gEnv->pConsole 被内联
    assert(console_addr == module.base + 0x48A7C68 && "Address verification failed");
    // pConsole 是 gEnv 的第 22 个成员，往前偏移21个指针，21 * 8 = 168 = 0xA8
    return console_addr - 0xA8;
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
    LuaDB* luaDB = nullptr;
    while ((luaDB = gLuaDB.load(std::memory_order_acquire)) == nullptr)
    {
        std::this_thread::yield();
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
        DWORD oldProtect;
        do
        {
            VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
            OriginalCompleteInit = static_cast<CompleteInitFunc>(vTable[COMPLETE_INIT_INDEX]);
        }
        while (InterlockedCompareExchangePointer(
            &vTable[COMPLETE_INIT_INDEX],
            reinterpret_cast<PVOID>(&Hooked_CompleteInit),
            reinterpret_cast<PVOID>(OriginalCompleteInit)) != OriginalCompleteInit);

        VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*), oldProtect, nullptr);

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


BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
        {
            Log_init();
            LogDebug("DLL attached");
            // 创建主工作线程
            if (const HANDLE hThread = CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr))
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
        if (gEnv && gEnv->pGame)
        {
            void** vTable = *reinterpret_cast<void***>(gEnv->pGame);
            // 恢复原函数
            DWORD oldProtect;
            VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*), PAGE_READWRITE, &oldProtect);
            vTable[COMPLETE_INIT_INDEX] = OriginalCompleteInit;
            VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*), oldProtect, nullptr);
        }
    }
    return TRUE;
}
