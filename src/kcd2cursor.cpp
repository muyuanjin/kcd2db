#include <windows.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <exception>
#include <string>
#include <thread>

#define KCD2_ENV_IMPORT
#include <cryengine/IGame.h>
#include <cryengine/IScriptSystem.h>
#include <cryengine/env.h>

#include "game/GameEnvLocator.h"
#include "hooks/CursorHook.h"
#include "log/log.h"
#include "util/StringUtils.h"

#ifndef KCD2CURSOR_VERSION
#define KCD2CURSOR_VERSION "dev"
#endif

#ifndef KCD2CURSOR_BUILD_CONFIG
#define KCD2CURSOR_BUILD_CONFIG "unknown"
#endif

namespace
{
using CompleteInitFunc = bool(__thiscall*)(IGame*);

constexpr int kCompleteInitIndex = 4;

CompleteInitFunc gOriginalCompleteInit = nullptr;

class KCD2CursorApi final : public CScriptableBase
{
public:
    void RegisterLuaAPI()
    {
        if (m_registered)
        {
            LogDebug("KCD2Cursor API is already registered.");
            return;
        }

        CScriptableBase::Init(gEnv->pScriptSystem, gEnv->pSystem);
        SetGlobalName("KCD2Cursor");
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &KCD2CursorApi::
        SCRIPT_REG_TEMPLFUNC(Declare, "path");
        SCRIPT_REG_TEMPLFUNC(Lock, "path");
        SCRIPT_REG_TEMPLFUNC(Unlock, "");
        if (m_pMethodsTable)
        {
            m_pMethodsTable->SetValue("version", KCD2CURSOR_VERSION);
            m_pMethodsTable->SetValue("native", true);
            m_pMethodsTable->SetValue("backend", "native");
        }
        m_registered = true;
        LogInfo("KCD2Cursor Lua API loaded.");
    }

    bool IsRegistered() const
    {
        return m_registered;
    }

    int Declare(IFunctionHandler* handler)
    {
        const char* path = nullptr;
        if (!handler->GetParam(1, path) || !path || path[0] == '\0')
        {
            LogWarn("KCD2Cursor.Declare invalid cursor path.");
            return handler->EndFunction(false);
        }

        return handler->EndFunction(CursorHook::DeclareCursorPath(path));
    }

    int Lock(IFunctionHandler* handler)
    {
        const char* path = nullptr;
        if (!handler->GetParam(1, path) || !path || path[0] == '\0')
        {
            LogWarn("KCD2Cursor.Lock invalid cursor path.");
            return handler->EndFunction(false);
        }

        return handler->EndFunction(CursorHook::LockCursorPath(path));
    }

    int Unlock(IFunctionHandler* handler)
    {
        return handler->EndFunction(CursorHook::UnlockCursorPath());
    }

private:
    bool m_registered = false;
};

std::atomic<KCD2CursorApi*> gCursorApi{nullptr};

std::string GetCompilerVersion()
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

constexpr const char* GetTargetArch()
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

std::string GetModulePath(HMODULE module)
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
            return kcd2db::WideToUtf8(buffer.c_str());
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string GetCurrentDirectoryPath()
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
    return kcd2db::WideToUtf8(buffer.c_str());
}

void LogStartupDiagnostics(HMODULE selfModule)
{
    LogDebug("kcd2cursor version: %s, config: %s, compiler: %s, arch: %s",
             KCD2CURSOR_VERSION,
             KCD2CURSOR_BUILD_CONFIG,
             GetCompilerVersion().c_str(),
             GetTargetArch());
    LogDebug("kcd2cursor.asi path: %s", GetModulePath(selfModule).c_str());
    LogDebug("Process exe path: %s", GetModulePath(nullptr).c_str());
    LogDebug("Current working directory: %s", GetCurrentDirectoryPath().c_str());
    LogDebug("Command line: %s", kcd2db::WideToUtf8(GetCommandLineW()).c_str());
}

bool __thiscall HookedCompleteInit(IGame* game)
{
    LogDebug("KCD2Cursor HookedCompleteInit");
    KCD2CursorApi* api = gCursorApi.load(std::memory_order_acquire);
    if (api)
    {
        api->RegisterLuaAPI();
    }
    else
    {
        LogWarn("KCD2Cursor Lua API object is unavailable during CompleteInit.");
    }

    return gOriginalCompleteInit(game);
}

void RestoreCompleteInitHook()
{
    if (!gEnv || gEnv->pGame == nullptr || gOriginalCompleteInit == nullptr)
    {
        LogDebug("KCD2Cursor CompleteInit hook restore skipped; game or original function is unavailable.");
        return;
    }

    void** vTable = *reinterpret_cast<void***>(gEnv->pGame);
    void** slot = &vTable[kCompleteInitIndex];
    const auto hookPtr = reinterpret_cast<void*>(&HookedCompleteInit);

    if (*slot != hookPtr)
    {
        LogWarn("KCD2Cursor CompleteInit hook restore skipped; vtable slot no longer points to this module.");
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogWarn("KCD2Cursor CompleteInit hook restore failed; VirtualProtect returned %lu.", GetLastError());
        return;
    }

    *slot = std::bit_cast<void*>(gOriginalCompleteInit);
    DWORD restoredOldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), oldProtect, &restoredOldProtect))
    {
        LogWarn("KCD2Cursor CompleteInit hook restore succeeded, but restoring page protection failed with %lu.",
                GetLastError());
    }
    LogDebug("KCD2Cursor restored CompleteInit hook.");
}

bool InstallCompleteInitHook(IGame* game)
{
    void** vTable = *reinterpret_cast<void***>(game);
    void** completeInitSlot = &vTable[kCompleteInitIndex];
    DWORD initialOldProtect = 0;
    if (!VirtualProtect(completeInitSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &initialOldProtect))
    {
        LogError("KCD2Cursor failed to make CompleteInit vtable slot writable at 0x%llX: %lu.",
                 reinterpret_cast<std::uintptr_t>(completeInitSlot),
                 GetLastError());
        return false;
    }

    do
    {
        DWORD tempOldProtect = 0;
        if (!VirtualProtect(completeInitSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &tempOldProtect))
        {
            LogError("KCD2Cursor failed to keep CompleteInit vtable slot writable at 0x%llX: %lu.",
                     reinterpret_cast<std::uintptr_t>(completeInitSlot),
                     GetLastError());
            DWORD restoredOldProtect = 0;
            if (!VirtualProtect(completeInitSlot, sizeof(void*), initialOldProtect, &restoredOldProtect))
            {
                LogWarn("KCD2Cursor failed to restore CompleteInit vtable slot protection after hook install abort: %lu.",
                        GetLastError());
            }
            return false;
        }
        static_assert(sizeof(CompleteInitFunc) == sizeof(void*), "Function pointer size mismatch");
        gOriginalCompleteInit = std::bit_cast<CompleteInitFunc>(*completeInitSlot);
    }
    while (InterlockedCompareExchangePointer(
        completeInitSlot,
        reinterpret_cast<PVOID>(&HookedCompleteInit),
        reinterpret_cast<PVOID>(gOriginalCompleteInit)) != gOriginalCompleteInit);

    DWORD restoredOldProtect = 0;
    if (!VirtualProtect(completeInitSlot, sizeof(void*), initialOldProtect, &restoredOldProtect))
    {
        LogWarn("KCD2Cursor CompleteInit hook installed, but restoring page protection failed with %lu.", GetLastError());
    }

    LogDebug("KCD2Cursor hooked CompleteInit function.");
    return true;
}

void Start()
{
    LogDebug("KCD2Cursor main thread started.");
    const auto envAddress = kcd2db::FindEnvAddress();
    if (!envAddress)
    {
        LogError("KCD2Cursor failed to find environment address.");
        return;
    }

    LogDebug("KCD2Cursor found environment address: 0x%llX", *envAddress);
    const auto* envPtr = reinterpret_cast<SSystemGlobalEnvironment*>(envAddress.value());

    while (envPtr->pGame == nullptr)
    {
        std::this_thread::yield();
    }
    LogDebug("KCD2Cursor game started.");

    gEnv = *envPtr;
    gCursorApi.store(new KCD2CursorApi(), std::memory_order_release);
    CursorHook::PrepareInstall();

    if (!InstallCompleteInitHook(gEnv->pGame))
    {
        return;
    }

    CursorHook::Install(reinterpret_cast<std::uintptr_t>(envPtr));

    while (envPtr->pGame->GetIGameFramework() == nullptr
        || !envPtr->pGame->GetIGameFramework()->IsGameStarted())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LogDebug("KCD2Cursor game framework started.");

    KCD2CursorApi* api = gCursorApi.load(std::memory_order_acquire);
    if (api && !api->IsRegistered())
    {
        LogError("KCD2Cursor Lua API was not registered by the CompleteInit hook.");
    }
}

DWORD WINAPI MainThread(LPVOID)
{
    try
    {
        Start();
    }
    catch (const std::exception& e)
    {
        LogError("kcd2cursor main thread: %s", e.what());
        return 1;
    }
    catch (...)
    {
        LogError("kcd2cursor main thread: Unknown error");
        return 1;
    }
    return 0;
}
}

BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HANDLE initThread = CreateThread(nullptr, 0, [](LPVOID moduleHandle) -> DWORD
        {
            Log_init();
            LogStartupDiagnostics(static_cast<HMODULE>(moduleHandle));
            LogDebug("KCD2Cursor DLL attached.");
            if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
            {
                CloseHandle(thread);
            }
            else
            {
                LogError("KCD2Cursor failed to create main thread.");
            }
            return 0;
        }, hModule, 0, nullptr);
        if (initThread)
        {
            CloseHandle(initThread);
        }
        else
        {
            LogError("KCD2Cursor failed to create initialization thread: %lu", GetLastError());
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (lpReserved == nullptr)
        {
            CursorHook::Restore();
            RestoreCompleteInitHook();
        }
        Log_close();
    }
    return TRUE;
}
