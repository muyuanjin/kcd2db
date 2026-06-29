#include "ModuleUtils.h"

#include "StringUtils.h"

#include <string>

namespace kcd2db
{
namespace
{
struct ModuleNameSearch
{
    std::string_view expectedName;
    lm_module_t* result = nullptr;
    bool found = false;
};

lm_bool_t LM_CALL FindModuleByNameCaseInsensitive(lm_module_t* module, lm_void_t* arg)
{
    auto* search = static_cast<ModuleNameSearch*>(arg);
    if (!module || search->found)
    {
        return LM_TRUE;
    }

    if (EqualsIgnoreCaseAscii(module->name, search->expectedName))
    {
        *search->result = *module;
        search->found = true;
    }
    return LM_TRUE;
}
}

bool FindModuleByName(std::string_view moduleName, lm_module_t& module)
{
    const std::string moduleNameString(moduleName);
    if (LM_FindModule(moduleNameString.c_str(), &module))
    {
        return true;
    }

    ModuleNameSearch search{
        .expectedName = moduleNameString,
        .result = &module,
        .found = false,
    };
    if (LM_EnumModules(FindModuleByNameCaseInsensitive, &search) != LM_TRUE)
    {
        return false;
    }
    return search.found;
}
}
