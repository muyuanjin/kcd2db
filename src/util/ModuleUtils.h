#pragma once

#include <string_view>

#include <libmem/libmem.h>

namespace kcd2db
{
bool FindModuleByName(std::string_view moduleName, lm_module_t& module);
}
