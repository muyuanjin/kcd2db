// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <cstring>

// Helper function to load data from unaligned memory
template<typename T>
inline void LoadUnaligned(const void* src, T& dst)
{
    std::memcpy(&dst, src, sizeof(T));
}

#define CHECK_REFCOUNT_CRASH(x)

// Memory statistics types and macros (minimal implementation)
enum EMemStatContextTypes
{
    MSC_ScriptCall = 0
};

// Dummy implementation of memory statistics functions
#define MEMSTAT_CONTEXT_FMT(type, id, format, ...) 
#define MEMSTAT_CONTEXT(type, id)
