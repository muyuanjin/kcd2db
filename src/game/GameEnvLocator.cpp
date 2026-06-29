#include "GameEnvLocator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <libmem/libmem.h>

#include "../log/log.h"
#include "../util/ModuleUtils.h"
#include "../util/StringUtils.h"

namespace kcd2db
{
namespace
{
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr auto kClientDll = "WHGame.DLL";
constexpr auto kInitialModuleDiagnosticDelay = std::chrono::seconds(2);
constexpr auto kMaxModuleDiagnosticDelay = std::chrono::minutes(2);

std::string BytesToPattern(const unsigned char* bytes, const size_t size)
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

bool ContainsDiagnosticToken(const lm_module_t& module)
{
    const std::string text = ToLowerAscii(std::string(module.name) + " " + module.path);
    constexpr std::array tokens = {"whgame", "kingdom", "cry", "game"};
    return std::any_of(tokens.begin(), tokens.end(), [&](const char* token)
    {
        return text.find(token) != std::string::npos;
    });
}

std::string FormatModule(const lm_module_t& module)
{
    std::ostringstream oss;
    oss << "name=" << module.name
        << ", path=" << module.path
        << ", base=0x" << std::hex << std::uppercase << module.base
        << ", size=0x" << module.size;
    return oss.str();
}

void LogModuleInfo(const char* prefix, const lm_module_t& module)
{
    const std::string info = FormatModule(module);
    LogDebug("%s: %s", prefix, info.c_str());
}

struct ModuleDiagnostics
{
    std::vector<lm_module_t> matches;
    std::vector<std::string> fallbackNames;
    std::unordered_set<std::string>* seenModules = nullptr;
    size_t newModuleCount = 0;
    bool enumSucceeded = false;
};

struct ModuleDiagnosticState
{
    std::unordered_set<std::string> seenModules;
    bool enumFailureLogged = false;
};

bool FindTargetModule(lm_module_t& module)
{
    return FindModuleByName(kClientDll, module);
}

lm_bool_t LM_CALL CollectModuleDiagnostics(lm_module_t* module, lm_void_t* arg)
{
    auto* diagnostics = static_cast<ModuleDiagnostics*>(arg);
    if (!module)
    {
        return LM_TRUE;
    }

    const std::string key = ToLowerAscii(std::string(module->name) + "\n" + module->path);
    if (diagnostics->seenModules && !diagnostics->seenModules->insert(key).second)
    {
        return LM_TRUE;
    }

    ++diagnostics->newModuleCount;
    if (ContainsDiagnosticToken(*module))
    {
        diagnostics->matches.push_back(*module);
    }
    if (diagnostics->fallbackNames.size() < 30)
    {
        diagnostics->fallbackNames.emplace_back(module->name);
    }
    return LM_TRUE;
}

void LogModuleDiagnostics(ModuleDiagnosticState& state)
{
    ModuleDiagnostics diagnostics;
    diagnostics.seenModules = &state.seenModules;
    diagnostics.enumSucceeded = LM_EnumModules(CollectModuleDiagnostics, &diagnostics) == LM_TRUE;
    if (!diagnostics.enumSucceeded)
    {
        if (!state.enumFailureLogged)
        {
            LogWarn("LM_EnumModules failed while waiting for %s.", kClientDll);
            state.enumFailureLogged = true;
        }
        return;
    }
    state.enumFailureLogged = false;

    if (diagnostics.newModuleCount == 0)
    {
        return;
    }

    LogWarn("LM_FindModule(\"%s\") has not matched yet; enumerating %zu newly observed module(s).",
            kClientDll,
            diagnostics.newModuleCount);

    if (!diagnostics.matches.empty())
    {
        LogWarn("New relevant loaded modules matching WHGame/Kingdom/Cry/Game:");
        for (const auto& module : diagnostics.matches)
        {
            const std::string info = FormatModule(module);
            LogWarn("  %s", info.c_str());
        }
        return;
    }

    LogWarn("No new relevant modules matched WHGame/Kingdom/Cry/Game. First %zu newly observed module(s):",
            diagnostics.fallbackNames.size());
    for (const auto& name : diagnostics.fallbackNames)
    {
        LogWarn("  %s", name.c_str());
    }
}

void LogScanModuleError(const char* message, const lm_module_t& module)
{
    LogError("%s Module: %s", message, FormatModule(module).c_str());
}

void LogLeaContextWindow(uintptr_t leaInstructionAddr)
{
    constexpr size_t kWindowBefore = 16;
    constexpr size_t kWindowAfter = 16;
    constexpr size_t kWindowSize = kWindowBefore + 1 + kWindowAfter;

    std::array<unsigned char, kWindowSize> bytes{};
    const uintptr_t start = leaInstructionAddr - kWindowBefore;
    if (LM_ReadMemory(start, reinterpret_cast<lm_byte_t*>(bytes.data()), bytes.size()))
    {
        const std::string window = BytesToPattern(bytes.data(), bytes.size());
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

std::optional<std::uintptr_t> FindEnvAddress()
{
    lm_module_t module;
    using namespace std::chrono;
    const auto waitStart = steady_clock::now();
    auto moduleDiagnosticDelay = kInitialModuleDiagnosticDelay;
    auto nextDiagnosticTime = waitStart + moduleDiagnosticDelay;
    bool wroteInitialWaitLog = false;
    ModuleDiagnosticState moduleDiagnosticState;
    while (!FindTargetModule(module))
    {
        const auto now = steady_clock::now();
        if (!wroteInitialWaitLog)
        {
            LogInfo("Waiting for module %s...", kClientDll);
            wroteInitialWaitLog = true;
        }
        if (now >= nextDiagnosticTime)
        {
            LogModuleDiagnostics(moduleDiagnosticState);
            moduleDiagnosticDelay *= 2;
            if (moduleDiagnosticDelay > kMaxModuleDiagnosticDelay)
            {
                moduleDiagnosticDelay = kMaxModuleDiagnosticDelay;
            }
            nextDiagnosticTime = now + moduleDiagnosticDelay;
        }
        std::this_thread::sleep_for(milliseconds(100));
    }
    LogModuleInfo("Found target module", module);

    constexpr std::string_view searchString = "exec autoexec.cfg";
    const auto stringPattern = BytesToPattern(
        reinterpret_cast<const unsigned char*>(searchString.data()),
        searchString.length());
    LogDebug("Scanning for anchor string pattern: %s", stringPattern.c_str());
    const uintptr_t stringAddr = LM_SigScan(stringPattern.c_str(), module.base, module.size);
    if (!stringAddr)
    {
        LogScanModuleError("Could not find the anchor string 'exec autoexec.cfg'.", module);
        return std::nullopt;
    }
    LogDebug("Found anchor string at: 0x%llX", stringAddr);

    uintptr_t leaInstructionAddr = 0;
    uintptr_t scanStart = module.base;
    while (scanStart < module.base + module.size)
    {
        const uintptr_t potentialLea = LM_SigScan("48 8D 15 ? ? ? ?", scanStart, module.size - (scanStart - module.base));
        if (!potentialLea)
        {
            LogScanModuleError("Could not find any cross-references (LEA) to the anchor string.", module);
            return std::nullopt;
        }
        int32_t ripOffset = 0;
        if (!LM_ReadMemory(potentialLea + 3, reinterpret_cast<lm_byte_t*>(&ripOffset), sizeof(ripOffset)))
        {
            LogWarn("Failed to read RIP offset from potential LEA at 0x%llX; continuing scan.", potentialLea);
            scanStart = potentialLea + 1;
            continue;
        }
        if (const uintptr_t referencedAddr = potentialLea + 7 + ripOffset; referencedAddr == stringAddr)
        {
            leaInstructionAddr = potentialLea;
            LogDebug("Found LEA instruction referencing the string at: 0x%llX", leaInstructionAddr);
            break;
        }
        scanStart = potentialLea + 1;
    }
    if (!leaInstructionAddr)
    {
        LogScanModuleError("Failed to locate the correct LEA instruction after scanning.", module);
        return std::nullopt;
    }

    uintptr_t pConsoleMovAddr = 0;
    unsigned char v1_6_context_bytes[7];
    if (LM_ReadMemory(leaInstructionAddr - 7, v1_6_context_bytes, 7) &&
        memcmp(v1_6_context_bytes, "\x4C\x8B\x92\x18\x01\x00\x00", 7) == 0)
    {
        LogDebug("Version context matches 'New' (V1.4+) pattern.");
        pConsoleMovAddr = leaInstructionAddr - 0x17;
    }
    else
    {
        unsigned char oldVersionOpcode[3];
        if (LM_ReadMemory(leaInstructionAddr - 7, oldVersionOpcode, 3) &&
            memcmp(oldVersionOpcode, "\x48\x8B\x0D", 3) == 0)
        {
            LogDebug("Version context matches 'Old' (V1.3, V1.2, etc.) pattern.");
            pConsoleMovAddr = leaInstructionAddr - 7;
        }
    }
    if (!pConsoleMovAddr)
    {
        LogScanModuleError("Could not identify any known version context around the LEA instruction.", module);
        LogLeaContextWindow(leaInstructionAddr);
        return std::nullopt;
    }
    LogDebug("Found pConsole MOV instruction at: 0x%llX", pConsoleMovAddr);

    int32_t ripOffset = 0;
    if (!LM_ReadMemory(pConsoleMovAddr + 3, reinterpret_cast<lm_byte_t*>(&ripOffset), sizeof(ripOffset)))
    {
        LogScanModuleError("Failed to read RIP offset from the MOV instruction.", module);
        return std::nullopt;
    }
    const uintptr_t consolePtrAddr = pConsoleMovAddr + 7 + ripOffset;
    return consolePtrAddr - 0xA8;
}
}
