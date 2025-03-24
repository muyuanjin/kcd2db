//
// Created by muyuanjin on 2025/3/24.
//

#include "Database.h"


Database::Database(SSystemGlobalEnvironment* env)
{
    Log("Database constructor");
    Init(env->pScriptSystem, env->pSystem);
    Log("Database constructor 2");
    SetGlobalName("LuaDB");
    Log("Database constructor 3");
    RegisterMethods();
    Log("Database constructor 4");
}

Database::~Database()
= default;

int Database::Test(IFunctionHandler* pH)
{
    gEnv->pConsole->ExecuteString("#dump(LuaDB)");
    Log("Test function m_pSS 0x%llX",m_pSS);
    Log("Test function m_pMethodsTable 0x%llX",m_pMethodsTable);
    return pH? pH->EndFunction(): 0;
}

int Database::GetVar(IFunctionHandler* pH,  char* key)
{
    return pH->EndFunction("nullptr");
}

int Database::SetVar(IFunctionHandler* pH,  char* key,  char* value)
{
    return pH->EndFunction();
}

void Database::RegisterMethods()
{
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &Database::

    SCRIPT_REG_FUNC(Test);
    SCRIPT_REG_TEMPLFUNC(GetVar, "key");
    SCRIPT_REG_TEMPLFUNC(SetVar, "key, value");
}

