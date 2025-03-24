//
// Created by muyuanjin on 2025/3/24.
//

#ifndef DATABASE_H
#define DATABASE_H
#include "../kcd2/IScriptSystem.h"


class Database : public CScriptableBase
{
public:
    Database(SSystemGlobalEnvironment* env)
    {
        Init(env->pScriptSystem,env->pSystem);
    }
};


#endif //DATABASE_H
