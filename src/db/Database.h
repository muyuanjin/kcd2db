//
// Created by muyuanjin on 2025/3/24.
//

#ifndef DATABASE_H
#define DATABASE_H
#include "../kcd2/IScriptSystem.h"


class Database : public CScriptableBase
{
public:
     explicit Database(SSystemGlobalEnvironment* env);
    virtual ~Database();
    void Release() const { delete this; };
    int Test(IFunctionHandler* pH);
    int GetVar(IFunctionHandler* pH, char* key);
    int SetVar(IFunctionHandler* pH, char* key, char* value);
private:
   void RegisterMethods();
};


#endif //DATABASE_H
