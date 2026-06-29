#pragma once

#include <cstdint>

namespace CursorHook
{
void PrepareInstall();
void Install(std::uintptr_t envAddress);
void Restore();
bool IsInstalled();
bool DeclareCursorPath(const char* path);
bool LockCursorPath(const char* path);
bool UnlockCursorPath();
}
