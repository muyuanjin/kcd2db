#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <bit>
#include <array>
#include <cstdio>
#include <cctype>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <libmem/libmem.h>

#define KCD2_ENV_IMPORT
#include "db/LuaDB.h"
#include <cryengine/env.h>
#include <cryengine/IGame.h>
#include "log/log.h"

#ifndef KCD2DB_VERSION
#define KCD2DB_VERSION "dev"
#endif

#ifndef KCD2DB_BUILD_CONFIG
#define KCD2DB_BUILD_CONFIG "unknown"
#endif

std::string bytes_to_pattern(const unsigned char* bytes, size_t size);

namespace
{
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr auto kClientDll = "WHGame.DLL";

std::string get_compiler_version()
{
#if defined(_MSC_FULL_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang_version__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#else
    return "unknown";
#endif
}

constexpr const char* get_target_arch()
{
#if defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "unknown";
#endif
}

std::string wide_to_utf8(const wchar_t* value)
{
    if (!value || value[0] == L'\0')
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.resize(size - 1);
    return result;
}

std::string get_module_path(HMODULE module)
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;
    while (true)
    {
        size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
        {
            return "<unknown>";
        }
        if (size < buffer.size() - 1)
        {
            buffer.resize(size);
            return wide_to_utf8(buffer.c_str());
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string get_current_directory_path()
{
    const DWORD required = GetCurrentDirectoryW(0, nullptr);
    if (required == 0)
    {
        return "<unknown>";
    }

    std::wstring buffer(required, L'\0');
    const DWORD size = GetCurrentDirectoryW(required, buffer.data());
    if (size == 0)
    {
        return "<unknown>";
    }
    buffer.resize(size);
    return wide_to_utf8(buffer.c_str());
}

std::string make_expected_db_path()
{
    std::string cwd = get_current_directory_path();
    if (cwd.empty() || cwd == "<unknown>")
    {
        return "<unknown>\\kcd2db.db";
    }

    const char last = cwd.back();
    if (last != '\\' && last != '/')
    {
        cwd += '\\';
    }
    cwd += "kcd2db.db";
    return cwd;
}

std::string to_lower_ascii(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value)
    {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

bool contains_diagnostic_token(const lm_module_t& module)
{
    const std::string text = to_lower_ascii(std::string(module.name) + " " + module.path);
    constexpr std::array tokens = {"whgame", "kingdom", "cry", "game"};
    return std::any_of(tokens.begin(), tokens.end(), [&](const char* token)
    {
        return text.find(token) != std::string::npos;
    });
}

std::string format_module(const lm_module_t& module)
{
    std::ostringstream oss;
    oss << "name=" << module.name
        << ", path=" << module.path
        << ", base=0x" << std::hex << std::uppercase << module.base
        << ", size=0x" << module.size;
    return oss.str();
}

void log_module_info(const char* prefix, const lm_module_t& module)
{
    const std::string info = format_module(module);
    LogDebug("%s: %s", prefix, info.c_str());
}

struct ModuleDiagnostics
{
    std::vector<lm_module_t> matches;
    std::vector<std::string> fallbackNames;
    bool enumSucceeded = false;
};

lm_bool_t LM_CALL collect_module_diagnostics(lm_module_t* module, lm_void_t* arg)
{
    auto* diagnostics = static_cast<ModuleDiagnostics*>(arg);
    if (!module)
    {
        return LM_TRUE;
    }

    if (contains_diagnostic_token(*module))
    {
        diagnostics->matches.push_back(*module);
    }
    if (diagnostics->fallbackNames.size() < 30)
    {
        diagnostics->fallbackNames.emplace_back(module->name);
    }
    return LM_TRUE;
}

void log_module_diagnostics()
{
    LogWarn("LM_FindModule(\"%s\") has not matched yet; enumerating loaded modules.", kClientDll);

    ModuleDiagnostics diagnostics;
    diagnostics.enumSucceeded = LM_EnumModules(collect_module_diagnostics, &diagnostics) == LM_TRUE;
    if (!diagnostics.enumSucceeded)
    {
        LogWarn("LM_EnumModules failed while waiting for %s.", kClientDll);
        return;
    }

    if (!diagnostics.matches.empty())
    {
        LogWarn("Relevant loaded modules matching WHGame/Kingdom/Cry/Game:");
        for (const auto& module : diagnostics.matches)
        {
            const std::string info = format_module(module);
            LogWarn("  %s", info.c_str());
        }
        return;
    }

    LogWarn("No relevant modules matched WHGame/Kingdom/Cry/Game. First %zu loaded modules:",
            diagnostics.fallbackNames.size());
    for (const auto& name : diagnostics.fallbackNames)
    {
        LogWarn("  %s", name.c_str());
    }
}

void log_startup_diagnostics(HMODULE selfModule)
{
    LogDebug("kcd2db version: %s, config: %s, compiler: %s, arch: %s",
             KCD2DB_VERSION,
             KCD2DB_BUILD_CONFIG,
             get_compiler_version().c_str(),
             get_target_arch());
    LogDebug("kcd2db.asi path: %s", get_module_path(selfModule).c_str());
    LogDebug("Process exe path: %s", get_module_path(nullptr).c_str());
    LogDebug("Current working directory: %s", get_current_directory_path().c_str());
    LogDebug("Expected DB path: %s", make_expected_db_path().c_str());
    LogDebug("Command line: %s", wide_to_utf8(GetCommandLineW()).c_str());
}

void log_scan_module_error(const char* message, const lm_module_t& module)
{
    LogError("%s Module: %s", message, format_module(module).c_str());
}

void log_lea_context_window(uintptr_t leaInstructionAddr)
{
    constexpr size_t kWindowBefore = 16;
    constexpr size_t kWindowAfter = 16;
    constexpr size_t kWindowSize = kWindowBefore + 1 + kWindowAfter;

    std::array<unsigned char, kWindowSize> bytes{};
    const uintptr_t start = leaInstructionAddr - kWindowBefore;
    if (LM_ReadMemory(start, reinterpret_cast<lm_byte_t*>(bytes.data()), bytes.size()))
    {
        const std::string window = bytes_to_pattern(bytes.data(), bytes.size());
        LogError("LEA context bytes [0x%llX..0x%llX]: %s",
                 start,
                 start + bytes.size() - 1,
                 window.c_str());
    }
    else
    {
        LogWarn("Failed to read LEA context bytes around 0x%llX.", leaInstructionAddr);
    }
}
}

// 将字节序列转换为 libmem 支持的模式
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
    using namespace std::chrono;
    const auto waitStart = steady_clock::now();
    auto nextDiagnosticTime = waitStart + seconds(10);
    bool wroteInitialWaitLog = false;
    while (!LM_FindModule(kClientDll, &module))
    {
        const auto now = steady_clock::now();
        if (!wroteInitialWaitLog)
        {
            LogInfo("Waiting for module %s...", kClientDll);
            wroteInitialWaitLog = true;
        }
        if (now >= nextDiagnosticTime)
        {
            log_module_diagnostics();
            nextDiagnosticTime = now + seconds(30);
        }
        std::this_thread::sleep_for(milliseconds(100));
    }
    log_module_info("Found target module", module);
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
        log_scan_module_error("Could not find the anchor string 'exec autoexec.cfg'.", module);
        return std::nullopt;
    }
    LogDebug("Found anchor string at: 0x%llX", string_addr);
    // --- 步骤 2: 找到引用该字符串的 LEA 指令 ---
    uintptr_t lea_instruction_addr = 0;
    uintptr_t scan_start = module.base;
    while (scan_start < module.base + module.size)
    {
        const uintptr_t potential_lea = LM_SigScan("48 8D 15 ? ? ? ?", scan_start, module.size - (scan_start - module.base));
        if (!potential_lea)
        {
            log_scan_module_error("Could not find any cross-references (LEA) to the anchor string.", module);
            return std::nullopt;
        }
        int32_t rip_offset;
        LM_ReadMemory(potential_lea + 3, reinterpret_cast<lm_byte_t*>(&rip_offset), sizeof(rip_offset));
        if (const uintptr_t referenced_addr = potential_lea + 7 + rip_offset; referenced_addr == string_addr)
        {
            lea_instruction_addr = potential_lea;
            LogDebug("Found LEA instruction referencing the string at: 0x%llX", lea_instruction_addr);
            break;
        }
        scan_start = potential_lea + 1;
    }
    if (!lea_instruction_addr)
    {
        log_scan_module_error("Failed to locate the correct LEA instruction after scanning.", module);
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
        LogDebug("Version context matches 'New' (V1.4+) pattern.");
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
            LogDebug("Version context matches 'Old' (V1.3, V1.2, etc.) pattern.");
            // 在旧版模式中，该指令就在 lea 前 7 个字节
            pConsole_mov_addr = lea_instruction_addr - 7;
        }
    }
    if (!pConsole_mov_addr)
    {
        log_scan_module_error("Could not identify any known version context around the LEA instruction.", module);
        log_lea_context_window(lea_instruction_addr);
        return std::nullopt;
    }
    LogDebug("Found pConsole MOV instruction at: 0x%llX", pConsole_mov_addr);
    // --- 步骤 4: 从 MOV 指令计算 gEnv 地址 ---
    int32_t rip_offset;
    if (!LM_ReadMemory(pConsole_mov_addr + 3, reinterpret_cast<lm_byte_t*>(&rip_offset), sizeof(rip_offset)))
    {
        log_scan_module_error("Failed to read RIP offset from the MOV instruction.", module);
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
    const auto waitStart = std::chrono::steady_clock::now();
    auto nextWaitLog = waitStart + std::chrono::seconds(1);
    while (luaDB == nullptr)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextWaitLog)
        {
            const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - waitStart).count();
            LogWarn("Hooked_CompleteInit is waiting for LuaDB initialization (%lld ms).", waitedMs);
            nextWaitLog = now + std::chrono::seconds(5);
        }
        std::this_thread::yield();
        luaDB = gLuaDB.load(std::memory_order_acquire);
    }
    // Game Lua state must be touched from the CompleteInit thread.
    luaDB->RegisterLuaAPI();
    LogDebug("Hooked_CompleteInit completed");
    return OriginalCompleteInit(pThis);
}

void RestoreCompleteInitHook()
{
    if (gEnv->pGame == nullptr || OriginalCompleteInit == nullptr)
    {
        LogDebug("CompleteInit hook restore skipped; game or original function is unavailable.");
        return;
    }

    void** vTable = *reinterpret_cast<void***>(gEnv->pGame);
    void** slot = &vTable[COMPLETE_INIT_INDEX];
    const auto hookPtr = reinterpret_cast<void*>(&Hooked_CompleteInit);

    if (*slot != hookPtr)
    {
        LogWarn("CompleteInit hook restore skipped; vtable slot no longer points to this module.");
        return;
    }

    DWORD oldProtect;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogWarn("CompleteInit hook restore failed; VirtualProtect returned %lu.", GetLastError());
        return;
    }

    *slot = std::bit_cast<void*>(OriginalCompleteInit);
    VirtualProtect(slot, sizeof(void*), oldProtect, nullptr);
    LogDebug("Restored CompleteInit hook.");
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
        LogInfo("Using LuaDB database at: %s", make_expected_db_path().c_str());
        const auto luaDB = new LuaDB();
        gLuaDB.store(luaDB, std::memory_order_release);
        LogDebug("LuaDB initialized");

        while (env_ptr->pGame->GetIGameFramework() == nullptr
            || !env_ptr->pGame->GetIGameFramework()->IsGameStarted())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LogDebug("Game Framework Started");

        if (!luaDB->isRegistered())
        {
            // Do not register from this worker thread; it can race the game's Lua state.
            LogError("LuaDB was not registered by the CompleteInit hook; not registering from worker thread.");
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
        LogError("kcd2db main thread: %s", e.what());
        return 1;
    }
    catch (...)
    {
        LogError("kcd2db main thread: Unknown error");
        return 1;
    }
    return 0;
}


BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, [](LPVOID moduleHandle) -> DWORD
        {
            Log_init();
            log_startup_diagnostics(static_cast<HMODULE>(moduleHandle));
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
        }, hModule, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // Full hot unload is unsupported. On FreeLibrary, only restore our vtable slot
        // so it does not point at unloaded code; avoid game-owned objects during process teardown.
        if (lpReserved == nullptr)
        {
            RestoreCompleteInitHook();
        }
        Log_close();
    }
    return TRUE;
}
