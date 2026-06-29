#pragma once

#include <cstdint>

namespace CursorHook
{
void Install(std::uintptr_t envAddress);
void Restore();
}
