#include <windows.h>
#include <cstdio>
#include <mutex>
#include <optional>
#include <cassert>
#include <libmem/libmem.h>

#define KCD2_ENV_IMPORT
#include "db/Database.h"
#include "kcd2/env.h"
#include "log/log.h"

std::optional<uintptr_t> find_env_addr()
{
    lm_module_t module;
    if (constexpr auto CLIENT_DLL = "WHGame.DLL"; !LM_FindModule(CLIENT_DLL, &module))
    {
        Log("Failed to find module %s\n", CLIENT_DLL);
        return std::nullopt;
    }

    const auto pattern = "48 8B 0D ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 45 33 C9 45 33 C0 4C 8B 11";
    const auto scan_address = LM_SigScan(pattern, module.base, module.size);
    if (!scan_address)
    {
        Log("Signature scan failed");
        return std::nullopt;
    }
    int32_t rip_offset;
    if (!LM_ReadMemory(scan_address + 3, reinterpret_cast<lm_byte_t*>(&rip_offset), sizeof(rip_offset)))
    {
        Log("Failed to read RIP offset");
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

// 主要逻辑线程函数
DWORD WINAPI main_thread(LPVOID)
{
    Log("Main thread started");
    if (const auto env_addr = find_env_addr())
    {
        if (!env_addr.has_value())
        {
            Log("Failed to find environment address");
            return 0;
        }
        Log("Found environment address: 0x%llX", *env_addr);
        auto* env_ptr = reinterpret_cast<SSystemGlobalEnvironment*>(env_addr.value());
        while (env_ptr -> pGame == nullptr)
        {
            Log("Waiting for game to be running...");
            Sleep(1000);
        }
        gEnv = *env_ptr;
        // MessageBoxA(nullptr, "Hello, World!", "KCD2DB", MB_OK);
        gEnv->pConsole->ExecuteString("#dump(1)");
        gEnv->pScriptSystem->SetGlobalAny("TestDB", "gEnv");
        gEnv->pScriptSystem->SetGlobalValue("TestDB2", "gEnv2");
        auto db = new Database(env_ptr);
        Log("Database initialized");
        while (true)
        {
            Log("Waiting for connection...%s", db);
            Sleep(10000);
        }
    }
    else
    {
        Log("Failed to find environment address");
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
            Log("DLL attached");
            // 创建主工作线程
            if (HANDLE hThread = CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr))
            {
                CloseHandle(hThread);
            }
            else
            {
                Log("Failed to create main thread");
            }
            return 0;
        }, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log_close();
    }
    return TRUE;
}
