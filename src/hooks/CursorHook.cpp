#include "CursorHook.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include <libmem/libmem.h>

#include <cryengine/IConsole.h>
#include <cryengine/env.h>

#include "../log/log.h"
#include "../util/ModuleUtils.h"
#include "../util/StringUtils.h"

namespace CursorHook
{
namespace
{
constexpr std::uintptr_t kHardwareMouseOffset = 0x100;
constexpr std::uintptr_t kEnvScanStartOffset = 0x80;
constexpr std::uintptr_t kEnvScanEndOffset = 0x180;
constexpr int kSetCursorPathIndex = 18;
constexpr const char* kClientDll = "WHGame.DLL";
constexpr const char* kCursorCVarName = "r_MouseCursorTexture";
constexpr std::size_t kMaxSetCursorDetourPatchSize = 32;
constexpr std::size_t kSetCursorDetourPatchSizeNew = 17;
constexpr std::size_t kSetCursorDetourPatchSizeOld = 16;
constexpr std::ptrdiff_t kCursorTextureOffset = 0x28;
constexpr std::ptrdiff_t kCursorPathStringOffset = 0x48;
constexpr std::ptrdiff_t kCursorUseSystemCursorOffset = 0x50;
constexpr std::ptrdiff_t kCursorHotspotXOffset = 0x88;
constexpr std::ptrdiff_t kCursorHotspotYOffset = 0x8C;
constexpr unsigned int kTextureLoadFlags = 0x50000;
constexpr const char* kCursorPathSetterPatternNew =
    "48 89 5C 24 10 57 48 83 EC 50 "
    "83 B9 98 00 00 00 00 48 8B DA "
    "0F 29 74 24 40 48 8B F9 "
    "0F 29 7C 24 30 0F 28 F2 0F 28 FB "
    "0F 85 ? ? ? ? 48 8B 41 48";
constexpr const char* kCursorPathSetterPatternOld =
    "48 8B C4 48 89 58 10 48 89 70 18 57 48 83 EC 50 "
    "83 B9 98 00 00 00 00 48 8B DA "
    "0F 29 70 E8 48 8B F9 "
    "0F 29 78 D8 0F 28 F3 0F 28 FA "
    "0F 85 ? ? ? ? 48 8D 71 48";

using SetCursorPathFunc = bool(__thiscall*)(void*, const char*, float, float);
using CVarSetStringFunc = void(__thiscall*)(ICVar*, const char*);
using CryStringAssignFunc = void*(__fastcall*)(void*, const char*);
using LoadTextureFunc = void*(__fastcall*)(void*, const char*, unsigned int);

enum class InstallState
{
    Disabled,
    Pending,
    Installed,
    Failed,
};

std::atomic<InstallState> gInstallState{InstallState::Disabled};
std::atomic<void*> gSetCursorDetourTarget{nullptr};
std::atomic<void**> gHookedCursorCVarSetSlot{nullptr};
std::atomic<ICVar*> gCursorCVar{nullptr};
std::atomic<void*> gTextureSystem{nullptr};
std::atomic<std::size_t> gTextureLoadVtableOffset{0};
std::atomic<std::size_t> gCursorCVarSetStringVtableOffset{0};

SetCursorPathFunc gOriginalSetCursorPath = nullptr;
CVarSetStringFunc gOriginalCursorCVarSetString = nullptr;
CryStringAssignFunc gCryStringAssign = nullptr;
std::array<unsigned char, kMaxSetCursorDetourPatchSize> gSetCursorOriginalBytes{};
std::size_t gSetCursorDetourPatchSize = 0;
void* gSetCursorTrampoline = nullptr;

struct ResolvedCursorPathSetter
{
    std::uintptr_t address = 0;
    std::size_t patchSize = 0;
    std::size_t cvarSetStringVtableOffset = 0;
    const char* patternName = "";
};

struct TextureCacheBindings
{
    std::uintptr_t textureSystemGlobalAddress = 0;
    std::uintptr_t stringAssignAddress = 0;
    std::size_t loadVtableOffset = 0;
};

struct State
{
    std::string lastAllowedPath;
    const char* lastAllowedStablePath = nullptr;
    const char* lockedStablePath = nullptr;
    std::string lockedNormalizedPath;
    std::deque<std::string> stablePaths;
    std::unordered_map<std::string, const char*> stablePathByValue;
    std::unordered_map<std::string, const char*> declaredPathByNormalizedValue;
    bool loggedFirstAllowedPath = false;
    bool loggedFirstDeclaredPath = false;
    bool loggedFirstLockedPath = false;
    unsigned long long observedSetCursorCount = 0;
    unsigned long long blockedSetCursorCount = 0;
    unsigned long long observedCVarCount = 0;
    unsigned long long substitutedCVarCount = 0;
    unsigned long long cachedSetCursorCount = 0;
    unsigned long long textureLoadFailureCount = 0;
};

std::mutex gStateMutex;
std::mutex gTextureCacheMutex;
State gState;
std::unordered_map<std::string, void*> gTextureCache;
bool gConsoleSinkInstalled = false;
std::atomic<void*> gHardwareMouse{nullptr};
thread_local bool gInsideCursorCVarSetHook = false;

std::string NormalizePath(std::string_view path)
{
    std::string result;
    result.reserve(path.size());

    bool previousSlash = false;
    for (const unsigned char ch : path)
    {
        char out = ch == '\\' ? '/' : static_cast<char>(std::tolower(ch));
        if (out == '/')
        {
            if (previousSlash)
            {
                continue;
            }
            previousSlash = true;
        }
        else
        {
            previousSlash = false;
        }
        result.push_back(out);
    }

    while (!result.empty() && result.front() == '/')
    {
        result.erase(result.begin());
    }

    return result;
}

bool StartsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool HasDefaultAllowedPrefix(const std::string& normalizedPath)
{
    return StartsWith(normalizedPath, "data/textures/cursor");
}

bool IsAllowedPathLocked(const std::string& normalizedPath)
{
    return HasDefaultAllowedPrefix(normalizedPath) || gState.declaredPathByNormalizedValue.contains(normalizedPath);
}

bool IsEngineFallbackPath(const std::string& normalizedPath)
{
    return StartsWith(normalizedPath, "engineassets/textures/cursor")
        || StartsWith(normalizedPath, "%engine%/engineassets/textures/cursor")
        || normalizedPath.find("/engineassets/textures/cursor") != std::string::npos
        || normalizedPath == "textures/cursor_green.dds"
        || normalizedPath.ends_with("/cursor_green.dds");
}

const char* InternPathLocked(std::string_view path)
{
    const std::string key(path);
    if (const auto it = gState.stablePathByValue.find(key); it != gState.stablePathByValue.end())
    {
        return it->second;
    }

    gState.stablePaths.push_back(key);
    const char* stablePath = gState.stablePaths.back().c_str();
    gState.stablePathByValue.emplace(gState.stablePaths.back(), stablePath);
    return stablePath;
}

const char* RememberAllowedPathLocked(std::string_view path, const char* source)
{
    const char* stablePath = InternPathLocked(path);
    gState.lastAllowedPath = stablePath;
    gState.lastAllowedStablePath = stablePath;
    if (!gState.loggedFirstAllowedPath)
    {
        gState.loggedFirstAllowedPath = true;
        LogDebug("CursorHook accepted first cursor path via %s: %s", source, stablePath);
    }
    return stablePath;
}

const char* DeclareCursorPathLocked(std::string_view path, const std::string& normalizedPath, const char* source)
{
    const char* stablePath = InternPathLocked(path);
    gState.declaredPathByNormalizedValue[normalizedPath] = stablePath;
    if (!gState.loggedFirstDeclaredPath)
    {
        gState.loggedFirstDeclaredPath = true;
        LogDebug("CursorHook declared first cursor path via %s: %s", source, stablePath);
    }
    return stablePath;
}

const char* GetEffectiveFallbackPathLocked()
{
    return gState.lockedStablePath ? gState.lockedStablePath : gState.lastAllowedStablePath;
}

bool IsExecutableAddress(void* address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)))
    {
        return false;
    }

    constexpr DWORD kExecutableFlags = PAGE_EXECUTE
        | PAGE_EXECUTE_READ
        | PAGE_EXECUTE_READWRITE
        | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & kExecutableFlags) != 0;
}

bool FindClientModule(lm_module_t& module)
{
    return kcd2db::FindModuleByName(kClientDll, module);
}

void* ReadPointer(std::uintptr_t address)
{
    if (!address)
    {
        return nullptr;
    }

    void* value = nullptr;
    __try
    {
        value = *reinterpret_cast<void**>(address);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
    return value;
}

void* ReadObjectPointer(void* object, std::ptrdiff_t offset)
{
    return ReadPointer(reinterpret_cast<std::uintptr_t>(object) + offset);
}

float ReadObjectFloat(void* object, std::ptrdiff_t offset, float fallback)
{
    if (!object)
    {
        return fallback;
    }

    __try
    {
        return *reinterpret_cast<float*>(static_cast<unsigned char*>(object) + offset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return fallback;
    }
}

bool ReadProcessBytes(std::uintptr_t address, void* buffer, std::size_t size)
{
    if (!address || !buffer || size == 0)
    {
        return false;
    }

    __try
    {
        std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

std::optional<std::uintptr_t> ResolveRipRelativeTarget(std::uintptr_t instructionAddress, std::size_t instructionSize)
{
    int32_t rel = 0;
    if (!ReadProcessBytes(instructionAddress + instructionSize - sizeof(rel), &rel, sizeof(rel)))
    {
        return std::nullopt;
    }
    return instructionAddress + instructionSize + rel;
}

bool MatchesAt(std::span<const unsigned char> bytes, std::size_t offset, std::span<const unsigned char> pattern)
{
    return offset <= bytes.size()
        && pattern.size() <= bytes.size() - offset
        && std::equal(pattern.begin(), pattern.end(), bytes.begin() + offset);
}

uint32_t ReadU32At(std::span<const unsigned char> bytes, std::size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::optional<std::size_t> ResolveCursorCVarSetStringVtableOffset(std::uintptr_t cursorPathSetter)
{
    constexpr auto kNewCVarStringSetCallPattern = std::to_array<unsigned char>({
        0x48, 0x8B, 0xC8, // mov rcx, rax
        0x48, 0x8B, 0xD3, // mov rdx, rbx
        0x48, 0x8B, 0x00, // mov rax, [rax]
        0xFF, 0x50,          // call qword ptr [rax+imm8]
    });
    constexpr auto kOldCVarStringSetCallPattern = std::to_array<unsigned char>({
        0x48, 0x8B, 0xD3, // mov rdx, rbx
        0x48, 0x8B, 0xC8, // mov rcx, rax
        0x4C, 0x8B, 0x00, // mov r8, [rax]
        0x41, 0xFF, 0x50, // call qword ptr [r8+imm8]
    });

    std::array<unsigned char, 0x180> bytes{};
    if (!ReadProcessBytes(cursorPathSetter, bytes.data(), bytes.size()))
    {
        return std::nullopt;
    }

    for (std::size_t i = 0; i + 16 < bytes.size(); ++i)
    {
        // Do not derive this slot from external/cryengine/IConsole.h. The ICVar interface is
        // marked as interfuscator-shuffled there, and current WHGame binaries do not use that
        // declaration order as their vtable ABI. The SetCursor implementation itself proves the
        // string setter slot: it passes the cursor path in RDX/RBX and immediately calls ICVar's
        // virtual Set(const char*) entry.
        if (MatchesAt(bytes, i, kNewCVarStringSetCallPattern))
        {
            return static_cast<std::size_t>(bytes[i + kNewCVarStringSetCallPattern.size()]);
        }

        if (MatchesAt(bytes, i, kOldCVarStringSetCallPattern))
        {
            return static_cast<std::size_t>(bytes[i + kOldCVarStringSetCallPattern.size()]);
        }
    }

    return std::nullopt;
}

std::optional<std::uintptr_t> FindSystemCursorColdBranch(std::uintptr_t cursorPathSetter)
{
    constexpr auto kLongSystemCursorJumpPattern = std::to_array<unsigned char>({
        0x80, 0x7F, 0x50, 0x00, // cmp byte ptr [rdi+50h], 0
        0x0F, 0x84,                   // jz rel32
    });
    constexpr auto kShortSystemCursorJumpPattern = std::to_array<unsigned char>({
        0x80, 0x7F, 0x50, 0x00, // cmp byte ptr [rdi+50h], 0
        0x74,                            // jz rel8
    });

    std::array<unsigned char, 0x260> bytes{};
    if (!ReadProcessBytes(cursorPathSetter, bytes.data(), bytes.size()))
    {
        return std::nullopt;
    }

    for (std::size_t i = 0; i + 10 < bytes.size(); ++i)
    {
        if (MatchesAt(bytes, i, kLongSystemCursorJumpPattern))
        {
            return ResolveRipRelativeTarget(cursorPathSetter + i, 10);
        }

        if (MatchesAt(bytes, i, kShortSystemCursorJumpPattern))
        {
            const int8_t rel = static_cast<int8_t>(bytes[i + kShortSystemCursorJumpPattern.size()]);
            return cursorPathSetter + i + 6 + rel;
        }
    }

    return std::nullopt;
}

std::optional<TextureCacheBindings> ResolveTextureCacheBindingsFromSetter(std::uintptr_t cursorPathSetter)
{
    constexpr auto kRipRelativeMovRcXPattern = std::to_array<unsigned char>({0x48, 0x8B, 0x0D});
    constexpr auto kLoadTextureCallPattern = std::to_array<unsigned char>({0xFF, 0x90});
    constexpr auto kNewStringAssignCallSetupPattern = std::to_array<unsigned char>({
        0x48, 0x8D, 0x4F, 0x48, // lea rcx, [rdi+48h]
        0x48, 0x8B, 0xD3,          // mov rdx, rbx
    });
    constexpr auto kOldStringAssignCallPattern = std::to_array<unsigned char>({
        0x48, 0x8B, 0xD3, // mov rdx, rbx
        0x48, 0x8B, 0xCE, // mov rcx, rsi
        0xE8,                   // call rel32
    });

    const auto coldBranch = FindSystemCursorColdBranch(cursorPathSetter);
    if (!coldBranch)
    {
        return std::nullopt;
    }

    std::array<unsigned char, 0x100> bytes{};
    if (!ReadProcessBytes(*coldBranch, bytes.data(), bytes.size()))
    {
        return std::nullopt;
    }

    TextureCacheBindings bindings{};
    for (std::size_t i = 0; i + 7 < bytes.size(); ++i)
    {
        if (!bindings.textureSystemGlobalAddress
            && MatchesAt(bytes, i, kRipRelativeMovRcXPattern))
        {
            const auto target = ResolveRipRelativeTarget(*coldBranch + i, 7);
            if (target)
            {
                bindings.textureSystemGlobalAddress = *target;
            }
        }

        if (!bindings.loadVtableOffset
            && MatchesAt(bytes, i, kLoadTextureCallPattern) && i + 6 <= bytes.size())
        {
            bindings.loadVtableOffset = ReadU32At(bytes, i + kLoadTextureCallPattern.size());
        }

        if (!bindings.stringAssignAddress)
        {
            if (MatchesAt(bytes, i, kNewStringAssignCallSetupPattern))
            {
                for (std::size_t j = i + kNewStringAssignCallSetupPattern.size();
                     j + 5 < bytes.size() && j < i + 0x20;
                     ++j)
                {
                    if (bytes[j] == 0xE8)
                    {
                        const auto target = ResolveRipRelativeTarget(*coldBranch + j, 5);
                        if (target)
                        {
                            bindings.stringAssignAddress = *target;
                        }
                        break;
                    }
                }
            }
            else if (MatchesAt(bytes, i, kOldStringAssignCallPattern))
            {
                const auto target = ResolveRipRelativeTarget(
                    *coldBranch + i + kOldStringAssignCallPattern.size() - 1,
                    5);
                if (target)
                {
                    bindings.stringAssignAddress = *target;
                }
            }
        }

        if (bindings.textureSystemGlobalAddress && bindings.loadVtableOffset && bindings.stringAssignAddress)
        {
            return bindings;
        }
    }

    return std::nullopt;
}

bool ResolveTextureCacheBindings(std::uintptr_t cursorPathSetter)
{
    const auto bindings = ResolveTextureCacheBindingsFromSetter(cursorPathSetter);
    if (!bindings)
    {
        LogWarn("CursorHook texture cache disabled; could not resolve cache bindings from SetCursor.");
        return false;
    }

    void* textureSystem = ReadPointer(bindings->textureSystemGlobalAddress);
    if (!textureSystem)
    {
        LogWarn("CursorHook texture cache disabled; texture system global is null at 0x%llX.",
                static_cast<unsigned long long>(bindings->textureSystemGlobalAddress));
        return false;
    }

    void** textureSystemVtable = static_cast<void**>(ReadObjectPointer(textureSystem, 0));
    if (!textureSystemVtable)
    {
        LogWarn("CursorHook texture cache disabled; texture system vtable is unavailable.");
        return false;
    }

    void* loadTexture = ReadPointer(
        reinterpret_cast<std::uintptr_t>(textureSystemVtable) + bindings->loadVtableOffset);
    if (!loadTexture || !IsExecutableAddress(loadTexture))
    {
        LogWarn("CursorHook texture cache disabled; LoadTexture slot at +0x%llX is invalid: 0x%llX.",
                static_cast<unsigned long long>(bindings->loadVtableOffset),
                reinterpret_cast<unsigned long long>(loadTexture));
        return false;
    }

    auto* stringAssign = reinterpret_cast<void*>(bindings->stringAssignAddress);
    if (!IsExecutableAddress(stringAssign))
    {
        LogWarn("CursorHook texture cache disabled; string assign helper 0x%llX is invalid.",
                static_cast<unsigned long long>(bindings->stringAssignAddress));
        return false;
    }

    gTextureSystem.store(textureSystem, std::memory_order_release);
    gTextureLoadVtableOffset.store(bindings->loadVtableOffset, std::memory_order_release);
    gCryStringAssign = std::bit_cast<CryStringAssignFunc>(stringAssign);
    LogDebug("CursorHook resolved texture cache bindings: textureGlobal=0x%llX textureSystem=0x%llX loadOffset=0x%llX loadSlot=0x%llX stringAssign=0x%llX.",
             static_cast<unsigned long long>(bindings->textureSystemGlobalAddress),
             reinterpret_cast<unsigned long long>(textureSystem),
             static_cast<unsigned long long>(bindings->loadVtableOffset),
             reinterpret_cast<unsigned long long>(loadTexture),
             reinterpret_cast<unsigned long long>(stringAssign));
    return true;
}

LoadTextureFunc GetLoadTextureFunc(void* textureSystem)
{
    if (!textureSystem)
    {
        return nullptr;
    }

    void** vtable = static_cast<void**>(ReadObjectPointer(textureSystem, 0));
    if (!vtable)
    {
        return nullptr;
    }

    const std::size_t loadVtableOffset = gTextureLoadVtableOffset.load(std::memory_order_acquire);
    if (!loadVtableOffset)
    {
        return nullptr;
    }

    void* loadTexture = ReadPointer(reinterpret_cast<std::uintptr_t>(vtable) + loadVtableOffset);
    if (!loadTexture || !IsExecutableAddress(loadTexture))
    {
        return nullptr;
    }

    return std::bit_cast<LoadTextureFunc>(loadTexture);
}

void* LoadCursorTextureCached(const char* path, const std::string& normalizedPath)
{
    if (!path || !path[0])
    {
        return nullptr;
    }

    {
        std::lock_guard cacheLock(gTextureCacheMutex);
        if (const auto it = gTextureCache.find(normalizedPath); it != gTextureCache.end())
        {
            return it->second;
        }
    }

    void* textureSystem = gTextureSystem.load(std::memory_order_acquire);
    LoadTextureFunc loadTexture = GetLoadTextureFunc(textureSystem);
    if (!loadTexture)
    {
        return nullptr;
    }

    void* texture = loadTexture(textureSystem, path, kTextureLoadFlags);
    if (!texture)
    {
        std::lock_guard lock(gStateMutex);
        ++gState.textureLoadFailureCount;
        const unsigned long long failureCount = gState.textureLoadFailureCount;
        if (failureCount == 1)
        {
            LogWarn("CursorHook texture cache could not load cursor texture path=%s count=%llu.",
                    path,
                    failureCount);
        }
        return nullptr;
    }

    {
        std::lock_guard cacheLock(gTextureCacheMutex);
        const auto [it, inserted] = gTextureCache.emplace(normalizedPath, texture);
        if (!inserted)
        {
            return it->second;
        }
    }

    return texture;
}

bool IsHardwareMouseUsingSystemCursor(void* hardwareMouse)
{
    if (!hardwareMouse)
    {
        return true;
    }

    bool useSystemCursor = true;
    __try
    {
        useSystemCursor = *reinterpret_cast<bool*>(
            static_cast<unsigned char*>(hardwareMouse) + kCursorUseSystemCursorOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return true;
    }
    return useSystemCursor;
}

bool ApplyCachedCursorTexture(
    void* hardwareMouse,
    const char* path,
    const std::string& normalizedPath,
    float hotspotX,
    float hotspotY)
{
    if (!hardwareMouse || !gCryStringAssign || IsHardwareMouseUsingSystemCursor(hardwareMouse))
    {
        return false;
    }

    void* texture = LoadCursorTextureCached(path, normalizedPath);
    if (!texture)
    {
        return false;
    }

    auto* bytes = static_cast<unsigned char*>(hardwareMouse);
    gCryStringAssign(bytes + kCursorPathStringOffset, path);
    *reinterpret_cast<float*>(bytes + kCursorHotspotXOffset) = hotspotX;
    *reinterpret_cast<float*>(bytes + kCursorHotspotYOffset) = hotspotY;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(bytes + kCursorTextureOffset), texture);
    return true;
}

void* GetHardwareMouse(std::uintptr_t envAddress)
{
    if (!envAddress)
    {
        return nullptr;
    }

    return *reinterpret_cast<void**>(envAddress + kHardwareMouseOffset);
}

void* FindHardwareMouseByVtable(std::uintptr_t envAddress, std::uintptr_t cursorPathSetter)
{
    if (!envAddress || !cursorPathSetter)
    {
        return nullptr;
    }

    for (std::uintptr_t offset = kEnvScanStartOffset; offset <= kEnvScanEndOffset; offset += sizeof(void*))
    {
        void* candidate = *reinterpret_cast<void**>(envAddress + offset);
        if (!candidate)
        {
            continue;
        }

        MEMORY_BASIC_INFORMATION objectInfo{};
        if (!VirtualQuery(candidate, &objectInfo, sizeof(objectInfo)) || objectInfo.State != MEM_COMMIT)
        {
            continue;
        }

        void** vTable = nullptr;
        __try
        {
            vTable = *reinterpret_cast<void***>(candidate);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            continue;
        }

        if (!vTable)
        {
            continue;
        }

        MEMORY_BASIC_INFORMATION vtableInfo{};
        if (!VirtualQuery(vTable, &vtableInfo, sizeof(vtableInfo)) || vtableInfo.State != MEM_COMMIT)
        {
            continue;
        }

        void* slotValue = nullptr;
        __try
        {
            slotValue = vTable[kSetCursorPathIndex];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            continue;
        }

        if (reinterpret_cast<std::uintptr_t>(slotValue) == cursorPathSetter)
        {
            LogDebug("CursorHook found pHardwareMouse candidate at gEnv+0x%llX: object=0x%llX, vtable=0x%llX.",
                     static_cast<unsigned long long>(offset),
                     reinterpret_cast<unsigned long long>(candidate),
                     reinterpret_cast<unsigned long long>(vTable));
            return candidate;
        }
    }

    return nullptr;
}

std::optional<ResolvedCursorPathSetter> ResolveCursorPathSetter()
{
    lm_module_t module{};
    if (!FindClientModule(module))
    {
        LogWarn("CursorHook could not find %s while resolving cursor setter.", kClientDll);
        return std::nullopt;
    }

    constexpr std::array patterns{
        std::tuple{kCursorPathSetterPatternNew, kSetCursorDetourPatchSizeNew, "new"},
        std::tuple{kCursorPathSetterPatternOld, kSetCursorDetourPatchSizeOld, "old"},
    };

    for (const auto& [pattern, patchSize, patternName] : patterns)
    {
        const std::uintptr_t functionAddress = LM_SigScan(pattern, module.base, module.size);
        if (!functionAddress)
        {
            continue;
        }

        const auto cvarSetStringVtableOffset = ResolveCursorCVarSetStringVtableOffset(functionAddress);
        if (!cvarSetStringVtableOffset)
        {
            LogWarn("CursorHook resolved SetCursor with pattern=%s, but could not resolve ICVar::Set(const char*) slot from the function body.",
                    patternName);
            continue;
        }

        LogDebug("CursorHook resolved cursor path setter: moduleBase=0x%llX, function=0x%llX, rva=0x%llX, pattern=%s, patchSize=%zu, cvarSetStringOffset=0x%llX.",
                 static_cast<unsigned long long>(module.base),
                 static_cast<unsigned long long>(functionAddress),
                 static_cast<unsigned long long>(functionAddress - module.base),
                 patternName,
                 patchSize,
                 static_cast<unsigned long long>(*cvarSetStringVtableOffset));
        return ResolvedCursorPathSetter{
            .address = functionAddress,
            .patchSize = patchSize,
            .cvarSetStringVtableOffset = *cvarSetStringVtableOffset,
            .patternName = patternName,
        };
    }

    LogWarn("CursorHook could not resolve cursor path setter signature in %s.", kClientDll);
    return std::nullopt;
}

class CursorConsoleVarSink final : public IConsoleVarSink
{
public:
    bool OnBeforeVarChange(ICVar* pVar, const char* sNewValue) override
    {
        if (!pVar || !sNewValue || _stricmp(pVar->GetName(), kCursorCVarName) != 0)
        {
            return true;
        }

        const std::string normalized = NormalizePath(sNewValue);
        if (normalized.empty())
        {
            return true;
        }

        std::lock_guard lock(gStateMutex);
        ++gState.observedCVarCount;
        const bool allowedPath = IsAllowedPathLocked(normalized);

        if (allowedPath)
        {
            RememberAllowedPathLocked(sNewValue, "cvar-sink");
        }

        return true;
    }

    void OnAfterVarChange(ICVar* pVar) override
    {
        (void)pVar;
    }
};

CursorConsoleVarSink gConsoleVarSink;

void __thiscall HookedCursorCVarSetString(ICVar* cvar, const char* value)
{
    if (!gOriginalCursorCVarSetString)
    {
        return;
    }

    ICVar* cursorCVar = gCursorCVar.load(std::memory_order_acquire);
    if (gInsideCursorCVarSetHook || cvar != cursorCVar)
    {
        gOriginalCursorCVarSetString(cvar, value);
        return;
    }

    const char* safeValue = value ? value : "";
    const std::string normalized = NormalizePath(safeValue);
    bool substitute = false;
    bool allowedPath = false;
    bool fallbackPath = false;
    unsigned long long substitutedCount = 0;
    const char* substitutedPath = nullptr;

    {
        std::lock_guard lock(gStateMutex);
        ++gState.observedCVarCount;
        allowedPath = IsAllowedPathLocked(normalized);
        fallbackPath = IsEngineFallbackPath(normalized);

        if (allowedPath)
        {
            RememberAllowedPathLocked(safeValue, "cvar-set");
        }
        else if (fallbackPath && GetEffectiveFallbackPathLocked())
        {
            substitute = true;
            substitutedPath = GetEffectiveFallbackPathLocked();
            ++gState.substitutedCVarCount;
            substitutedCount = gState.substitutedCVarCount;
        }
    }

    if (substitute)
    {
        if (substitutedCount == 1)
        {
            LogDebug("CursorHook substituted ICVar::Set r_MouseCursorTexture fallback=%s with=%s count=%llu",
                     safeValue,
                     substitutedPath ? substitutedPath : "<last allowed>",
                     substitutedCount);
        }

        gInsideCursorCVarSetHook = true;
        gOriginalCursorCVarSetString(cvar, substitutedPath);
        gInsideCursorCVarSetHook = false;
        return;
    }

    gInsideCursorCVarSetHook = true;
    gOriginalCursorCVarSetString(cvar, value);
    gInsideCursorCVarSetHook = false;
}

void InstallConsoleVarSinkIfNeeded()
{
    if (!gEnv.pConsole || gConsoleSinkInstalled)
    {
        return;
    }

    gEnv.pConsole->AddConsoleVarSink(&gConsoleVarSink);
    gConsoleSinkInstalled = true;
    LogDebug("CursorHook installed r_MouseCursorTexture console var sink.");
}

void RemoveConsoleVarSinkIfNeeded()
{
    if (!gEnv.pConsole || !gConsoleSinkInstalled)
    {
        return;
    }

    gEnv.pConsole->RemoveConsoleVarSink(&gConsoleVarSink);
    gConsoleSinkInstalled = false;
    LogDebug("CursorHook removed r_MouseCursorTexture console var sink.");
}

void InstallCursorCVarSetHookIfNeeded()
{
    if (!gEnv.pConsole || gHookedCursorCVarSetSlot.load(std::memory_order_acquire))
    {
        return;
    }

    const std::size_t cvarSetStringVtableOffset =
        gCursorCVarSetStringVtableOffset.load(std::memory_order_acquire);
    if (!cvarSetStringVtableOffset || cvarSetStringVtableOffset % sizeof(void*) != 0)
    {
        LogWarn("CursorHook could not install ICVar::Set hook because the string setter slot was not resolved.");
        return;
    }

    ICVar* cursorCVar = gEnv.pConsole->GetCVar(kCursorCVarName);
    if (!cursorCVar)
    {
        LogWarn("CursorHook could not find %s for ICVar::Set hook.", kCursorCVarName);
        return;
    }

    void** vTable = *reinterpret_cast<void***>(cursorCVar);
    if (!vTable)
    {
        LogWarn("CursorHook could not install ICVar::Set hook because %s has no vtable.", kCursorCVarName);
        return;
    }

    void** slot = reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(vTable) + cvarSetStringVtableOffset);
    void* original = *slot;
    if (!original || !IsExecutableAddress(original))
    {
        LogWarn("CursorHook validation failed for %s ICVar::Set slot +0x%llX: original=0x%llX.",
                kCursorCVarName,
                static_cast<unsigned long long>(cvarSetStringVtableOffset),
                reinterpret_cast<unsigned long long>(original));
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogWarn("CursorHook failed to make %s ICVar::Set slot writable: %lu.", kCursorCVarName, GetLastError());
        return;
    }

    gOriginalCursorCVarSetString = std::bit_cast<CVarSetStringFunc>(original);
    gCursorCVar.store(cursorCVar, std::memory_order_release);

    if (InterlockedCompareExchangePointer(
            slot,
            reinterpret_cast<PVOID>(&HookedCursorCVarSetString),
            original) != original)
    {
        gOriginalCursorCVarSetString = nullptr;
        gCursorCVar.store(nullptr, std::memory_order_release);
        DWORD restoredOldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), oldProtect, &restoredOldProtect))
        {
            LogWarn("CursorHook ICVar::Set install abort restored pointer state, but protection restore failed: %lu.",
                    GetLastError());
        }
        LogWarn("CursorHook ICVar::Set install skipped because vtable slot changed concurrently.");
        return;
    }

    gHookedCursorCVarSetSlot.store(slot, std::memory_order_release);

    DWORD restoredOldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), oldProtect, &restoredOldProtect))
    {
        LogWarn("CursorHook installed ICVar::Set hook, but restoring page protection failed: %lu.", GetLastError());
    }

    LogDebug("CursorHook installed r_MouseCursorTexture ICVar::Set hook: slotIndex=%zu, slotOffset=0x%llX, original=0x%llX.",
             cvarSetStringVtableOffset / sizeof(void*),
             static_cast<unsigned long long>(cvarSetStringVtableOffset),
             reinterpret_cast<unsigned long long>(original));
}

bool __thiscall HookedSetCursorPath(void* hardwareMouse, const char* path, const float hotspotX, const float hotspotY)
{
    if (!gOriginalSetCursorPath)
    {
        return false;
    }

    const char* safePath = path ? path : "";
    const char* forwardedPath = path;
    const char* keptPath = nullptr;
    std::string normalizedPath;
    bool block = false;
    bool allowedPath = false;
    bool fallbackPath = false;
    unsigned long long blockedCount = 0;
    unsigned long long cachedCount = 0;

    {
        std::lock_guard lock(gStateMutex);
        ++gState.observedSetCursorCount;

        normalizedPath = NormalizePath(safePath);
        allowedPath = IsAllowedPathLocked(normalizedPath);
        fallbackPath = IsEngineFallbackPath(normalizedPath);

        if (allowedPath)
        {
            forwardedPath = RememberAllowedPathLocked(safePath, "SetCursor");
        }
        else if (fallbackPath && GetEffectiveFallbackPathLocked())
        {
            block = true;
            keptPath = GetEffectiveFallbackPathLocked();
            ++gState.blockedSetCursorCount;
            blockedCount = gState.blockedSetCursorCount;
        }
    }

    if (block)
    {
        if (blockedCount == 1)
        {
            LogDebug("CursorHook blocked engine cursor fallback path=%s; keeping=%s count=%llu",
                     safePath,
                     keptPath ? keptPath : "<last allowed>",
                     blockedCount);
        }
        return true;
    }

    if (allowedPath && ApplyCachedCursorTexture(hardwareMouse, forwardedPath, normalizedPath, hotspotX, hotspotY))
    {
        {
            std::lock_guard lock(gStateMutex);
            ++gState.cachedSetCursorCount;
            cachedCount = gState.cachedSetCursorCount;
        }
        if (cachedCount == 1)
        {
            LogDebug("CursorHook applied cached cursor texture path=%s texture=0x%llX count=%llu",
                     forwardedPath ? forwardedPath : "<null>",
                     reinterpret_cast<unsigned long long>(
                         ReadObjectPointer(hardwareMouse, kCursorTextureOffset)),
                     cachedCount);
        }
        return true;
    }

    return gOriginalSetCursorPath(hardwareMouse, forwardedPath, hotspotX, hotspotY);
}

bool WriteAbsoluteJump(
    void* target,
    void* destination,
    std::size_t patchSize,
    std::span<const unsigned char> expectedBytes = {})
{
    if (!target || !destination || patchSize < 13)
    {
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogWarn("CursorHook detour VirtualProtect failed at 0x%llX: %lu.",
                reinterpret_cast<unsigned long long>(target),
                GetLastError());
        return false;
    }

    bool expected = true;
    if (!expectedBytes.empty())
    {
        expected = std::memcmp(target, expectedBytes.data(), expectedBytes.size()) == 0;
    }

    if (expected)
    {
        auto* bytes = static_cast<unsigned char*>(target);
        bytes[0] = 0x49; // mov r11, imm64
        bytes[1] = 0xBB;
        *reinterpret_cast<void**>(bytes + 2) = destination;
        bytes[10] = 0x41; // jmp r11
        bytes[11] = 0xFF;
        bytes[12] = 0xE3;
        for (std::size_t i = 13; i < patchSize; ++i)
        {
            bytes[i] = 0x90;
        }
        FlushInstructionCache(GetCurrentProcess(), target, patchSize);
    }

    DWORD restoredOldProtect = 0;
    if (!VirtualProtect(target, patchSize, oldProtect, &restoredOldProtect))
    {
        LogWarn("CursorHook detour protection restore failed at 0x%llX: %lu.",
                reinterpret_cast<unsigned long long>(target),
                GetLastError());
    }

    return expected;
}

bool InstallSetCursorEntryDetour(std::uintptr_t cursorPathSetter, std::size_t patchSize)
{
    if (!cursorPathSetter || patchSize < 13 || patchSize > gSetCursorOriginalBytes.size()
        || gSetCursorDetourTarget.load(std::memory_order_acquire))
    {
        return false;
    }

    auto* target = reinterpret_cast<unsigned char*>(cursorPathSetter);
    std::memcpy(gSetCursorOriginalBytes.data(), target, patchSize);
    gSetCursorDetourPatchSize = patchSize;

    const std::size_t trampolineSize = patchSize + 13;
    void* trampoline = VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline)
    {
        LogWarn("CursorHook failed to allocate SetCursor trampoline: %lu.", GetLastError());
        return false;
    }

    auto* trampolineBytes = static_cast<unsigned char*>(trampoline);
    std::memcpy(trampolineBytes, target, patchSize);
    trampolineBytes[patchSize] = 0x49; // mov r11, imm64
    trampolineBytes[patchSize + 1] = 0xBB;
    *reinterpret_cast<void**>(trampolineBytes + patchSize + 2) =
        reinterpret_cast<void*>(cursorPathSetter + patchSize);
    trampolineBytes[patchSize + 10] = 0x41; // jmp r11
    trampolineBytes[patchSize + 11] = 0xFF;
    trampolineBytes[patchSize + 12] = 0xE3;
    FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);

    gOriginalSetCursorPath = std::bit_cast<SetCursorPathFunc>(trampoline);
    gSetCursorTrampoline = trampoline;

    if (!WriteAbsoluteJump(
            target,
            reinterpret_cast<void*>(&HookedSetCursorPath),
            patchSize,
            std::span<const unsigned char>(gSetCursorOriginalBytes.data(), patchSize)))
    {
        gOriginalSetCursorPath = nullptr;
        gSetCursorTrampoline = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        LogWarn("CursorHook SetCursor entry detour skipped because target bytes changed.");
        return false;
    }

    gSetCursorDetourTarget.store(target, std::memory_order_release);
    LogDebug("CursorHook installed SetCursor entry detour: target=0x%llX trampoline=0x%llX patchSize=%zu.",
             reinterpret_cast<unsigned long long>(target),
             reinterpret_cast<unsigned long long>(trampoline),
             patchSize);
    return true;
}

bool IsUsableCursorPath(const char* path, std::string& normalizedPath)
{
    if (!path || !path[0])
    {
        return false;
    }

    normalizedPath = NormalizePath(path);
    return !normalizedPath.empty() && !IsEngineFallbackPath(normalizedPath);
}

void MarkInstallFailed()
{
    gInstallState.store(InstallState::Failed, std::memory_order_release);
}
}

void PrepareInstall()
{
    InstallState expected = InstallState::Disabled;
    gInstallState.compare_exchange_strong(
        expected,
        InstallState::Pending,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

void Install(std::uintptr_t envAddress)
{
    PrepareInstall();
    if (gInstallState.load(std::memory_order_acquire) != InstallState::Pending)
    {
        return;
    }
    LogInfo("KCD2Cursor hook enabled.");

    const auto cursorPathSetter = ResolveCursorPathSetter();
    if (!cursorPathSetter)
    {
        MarkInstallFailed();
        return;
    }

    void* hardwareMouse = nullptr;
    for (int attempt = 0; attempt < 200 && !hardwareMouse; ++attempt)
    {
        hardwareMouse = FindHardwareMouseByVtable(envAddress, cursorPathSetter->address);
        if (!hardwareMouse)
        {
            Sleep(50);
        }
    }

    if (!hardwareMouse)
    {
        void* offsetFallback = GetHardwareMouse(envAddress);
        LogWarn("CursorHook requested, but pHardwareMouse could not be validated by vtable scan. gEnv+0x%llX=0x%llX.",
                static_cast<unsigned long long>(kHardwareMouseOffset),
                reinterpret_cast<unsigned long long>(offsetFallback));
        MarkInstallFailed();
        return;
    }

    gHardwareMouse.store(hardwareMouse, std::memory_order_release);
    ResolveTextureCacheBindings(cursorPathSetter->address);
    gCursorCVarSetStringVtableOffset.store(
        cursorPathSetter->cvarSetStringVtableOffset,
        std::memory_order_release);

    if (!InstallSetCursorEntryDetour(cursorPathSetter->address, cursorPathSetter->patchSize))
    {
        gCursorCVarSetStringVtableOffset.store(0, std::memory_order_release);
        MarkInstallFailed();
        return;
    }

    gInstallState.store(InstallState::Installed, std::memory_order_release);

    InstallConsoleVarSinkIfNeeded();
    InstallCursorCVarSetHookIfNeeded();
}

bool IsInstalled()
{
    return gSetCursorDetourTarget.load(std::memory_order_acquire) != nullptr;
}

bool DeclareCursorPath(const char* path)
{
    const bool canDeclare =
        IsInstalled() || gInstallState.load(std::memory_order_acquire) == InstallState::Pending;
    if (!canDeclare)
    {
        return false;
    }

    std::string normalizedPath;
    if (!IsUsableCursorPath(path, normalizedPath))
    {
        return false;
    }

    {
        std::lock_guard lock(gStateMutex);
        DeclareCursorPathLocked(path, normalizedPath, "Lua");
    }
    return true;
}

bool LockCursorPath(const char* path)
{
    if (!IsInstalled())
    {
        return false;
    }

    std::string normalizedPath;
    if (!IsUsableCursorPath(path, normalizedPath))
    {
        return false;
    }

    const char* stablePath = nullptr;
    {
        std::lock_guard lock(gStateMutex);
        stablePath = InternPathLocked(path);
    }

    void* hardwareMouse = gHardwareMouse.load(std::memory_order_acquire);
    const float hotspotX = ReadObjectFloat(hardwareMouse, kCursorHotspotXOffset, 0.0f);
    const float hotspotY = ReadObjectFloat(hardwareMouse, kCursorHotspotYOffset, 0.0f);
    if (!hardwareMouse || !ApplyCachedCursorTexture(hardwareMouse, stablePath, normalizedPath, hotspotX, hotspotY))
    {
        LogWarn("CursorHook could not apply locked cursor path: %s", stablePath ? stablePath : path);
        return false;
    }

    {
        std::lock_guard lock(gStateMutex);
        DeclareCursorPathLocked(stablePath, normalizedPath, "Lua lock");
        RememberAllowedPathLocked(stablePath, "Lua lock");
        gState.lockedStablePath = stablePath;
        gState.lockedNormalizedPath = normalizedPath;
        if (!gState.loggedFirstLockedPath)
        {
            gState.loggedFirstLockedPath = true;
            LogDebug("CursorHook locked first cursor path: %s", stablePath);
        }
    }
    return true;
}

bool UnlockCursorPath()
{
    if (!IsInstalled())
    {
        return false;
    }

    std::lock_guard lock(gStateMutex);
    const bool wasLocked = gState.lockedStablePath != nullptr;
    gState.lockedStablePath = nullptr;
    gState.lockedNormalizedPath.clear();
    return wasLocked;
}

void Restore()
{
    void** cvarSetSlot = gHookedCursorCVarSetSlot.load(std::memory_order_acquire);
    if (cvarSetSlot && gOriginalCursorCVarSetString)
    {
        const auto hookPtr = reinterpret_cast<void*>(&HookedCursorCVarSetString);
        if (*cvarSetSlot == hookPtr)
        {
            DWORD oldProtect = 0;
            if (VirtualProtect(cvarSetSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                *cvarSetSlot = std::bit_cast<void*>(gOriginalCursorCVarSetString);
                gHookedCursorCVarSetSlot.store(nullptr, std::memory_order_release);
                gOriginalCursorCVarSetString = nullptr;
                gCursorCVar.store(nullptr, std::memory_order_release);
                gCursorCVarSetStringVtableOffset.store(0, std::memory_order_release);

                DWORD restoredOldProtect = 0;
                if (!VirtualProtect(cvarSetSlot, sizeof(void*), oldProtect, &restoredOldProtect))
                {
                    LogWarn("CursorHook restored ICVar::Set pointer, but protection restore failed: %lu.", GetLastError());
                }
            }
            else
            {
                LogWarn("CursorHook ICVar::Set restore failed; VirtualProtect returned %lu.", GetLastError());
            }
        }
        else
        {
            LogWarn("CursorHook ICVar::Set restore skipped; vtable slot no longer points to this module.");
        }
    }
    gCursorCVarSetStringVtableOffset.store(0, std::memory_order_release);

    RemoveConsoleVarSinkIfNeeded();

    gHardwareMouse.store(nullptr, std::memory_order_release);
    gTextureSystem.store(nullptr, std::memory_order_release);
    gTextureLoadVtableOffset.store(0, std::memory_order_release);
    gCryStringAssign = nullptr;
    {
        std::lock_guard cacheLock(gTextureCacheMutex);
        gTextureCache.clear();
    }

    void* detourTarget = gSetCursorDetourTarget.load(std::memory_order_acquire);
    if (detourTarget)
    {
        const std::size_t patchSize = gSetCursorDetourPatchSize;
        DWORD oldProtect = 0;
        if (patchSize > 0 && VirtualProtect(detourTarget, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            std::memcpy(detourTarget, gSetCursorOriginalBytes.data(), patchSize);
            FlushInstructionCache(GetCurrentProcess(), detourTarget, patchSize);
            gSetCursorDetourTarget.store(nullptr, std::memory_order_release);
            gInstallState.store(InstallState::Disabled, std::memory_order_release);
            gOriginalSetCursorPath = nullptr;
            gSetCursorDetourPatchSize = 0;

            DWORD restoredOldProtect = 0;
            if (!VirtualProtect(detourTarget, patchSize, oldProtect, &restoredOldProtect))
            {
                LogWarn("CursorHook restored SetCursor entry bytes, but protection restore failed: %lu.", GetLastError());
            }
        }
        else
        {
            LogWarn("CursorHook SetCursor entry restore failed; VirtualProtect returned %lu.", GetLastError());
        }

        if (gSetCursorTrampoline)
        {
            VirtualFree(gSetCursorTrampoline, 0, MEM_RELEASE);
            gSetCursorTrampoline = nullptr;
        }
    }

    LogDebug("CursorHook restored.");
}
}
