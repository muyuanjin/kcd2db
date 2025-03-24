//
// Created by muyuanjin on 2025/3/24.
//

#include "Database.h"

#include <sstream>


// 初始化数据库
Database::Database( SSystemGlobalEnvironment* env)
{
    CScriptableBase::Init(env->pScriptSystem, env->pSystem);
    SetGlobalName("LuaDB");

    // 初始化SQLite
    m_dbPath = "./luadata.db";
    m_db = std::make_unique<SQLite::Database>(m_dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

    // 创建表结构
    ExecuteTransaction([&](SQLite::Database& db)
    {
        db.exec("CREATE TABLE IF NOT EXISTS Store ("
            "key TEXT PRIMARY KEY,"
            "type INTEGER,"
            "value TEXT)");
    });

    LoadFromDB();
    RegisterMethods();
}

Database::~Database()
{
    SaveToDB();
}

void Database::LoadFromDB()
{
    SQLite::Statement query(*m_db, "SELECT key, type, value FROM Store");
    std::lock_guard lock(m_mutex);
    int count = 0;
    while (query.executeStep())
    {
        std::string key = query.getColumn(0).getText();
        const int type = query.getColumn(1).getInt();
        std::string value = query.getColumn(2).getText();

        Log("Loading key: %s, type: %d, value: %s", key.c_str(), type, value.c_str());

        ScriptAnyValue any;
        switch (type)
        {
        case ANY_TBOOLEAN:
            any.type = ANY_TBOOLEAN;
            any.b = std::stoi(value) != 0;
            break;
        case ANY_TNUMBER:
            any.type = ANY_TNUMBER;
            any.number = std::stod(value);
            break;
        case ANY_TSTRING:
            any.type = ANY_TSTRING;
            any.str = _strdup(value.c_str());
            break;
        default:
            continue;
        }


        m_cache[key] = any;
        count++;
    }
    std::ostringstream oss;
    oss << "Loaded " << count << " entries from database.";
    gEnv->pConsole->PrintLine(oss.str().c_str());
}

// 原子化操作
void Database::ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const
{
    SQLite::Transaction transaction(*m_db);
    task(*m_db);
    transaction.commit();
}

// 数据设置（线程安全）
int Database::Set(IFunctionHandler* pH)
{
    const char* key = nullptr;
    ScriptAnyValue value;

    if (pH->GetParamCount() < 2 ||
        !pH->GetParam(1, key) ||
        !pH->GetParamAny(2, value))
    {
        return pH->EndFunction(false);
    }

    std::lock_guard lock(m_mutex);
    m_cache[key] = value;

    return pH->EndFunction(true);
}

// 数据获取（带类型擦除）
int Database::Get(IFunctionHandler* pH)
{
    const char* keyParam  = nullptr;
    if (!pH->GetParam(1, keyParam ))
    {
        return pH->EndFunction();
    }
    const std::string key(keyParam ? keyParam : "");
    std::lock_guard lock(m_mutex);
    const auto it = m_cache.find(key);
    if (it == m_cache.end())
    {
        return pH->EndFunction();
    }

    switch (it->second.type)
    {
    case ANY_TBOOLEAN:
        return pH->EndFunction(it->second.b);
    case ANY_TNUMBER:
        return pH->EndFunction(it->second.number);
    case ANY_TSTRING:
        return pH->EndFunction(it->second.str);
    default:
        return pH->EndFunction();
    }
}

int Database::Delete(IFunctionHandler* pH)
{
    const char* key = nullptr;
    if (!pH->GetParam(1, key))
    {
        return pH->EndFunction(false);
    }

    std::lock_guard lock(m_mutex);
    m_cache.erase(key);
    return pH->EndFunction(true);
}

int Database::Exists(IFunctionHandler* pH)
{
    const char* key = nullptr;
    if (!pH->GetParam(1, key))
    {
        return pH->EndFunction(false);
    }

    std::lock_guard lock(m_mutex);
    return pH->EndFunction(m_cache.find(key) != m_cache.end());
}

int Database::Flush(IFunctionHandler* pH)
{
    std::lock_guard lock(m_mutex);
    SaveToDB();
    return pH->EndFunction();
}

int Database::Dump(IFunctionHandler* pH)
{
    std::lock_guard lock(m_mutex);  // 明确模板参数类型
    for (const auto& [key, value] : m_cache)
    {
        switch (value.type)
        {
        case ANY_TBOOLEAN:
            {
                std::ostringstream oss;
                oss << "Boolean Value ["<< key <<"] : " << (value.b ? "true" : "false");
                gEnv->pConsole->PrintLine(oss.str().c_str());
            }
            break;
        case ANY_TNUMBER:
            {
                std::ostringstream oss;
                oss << "Number Value ["<< key <<"] : " << value.number;
                gEnv->pConsole->PrintLine(oss.str().c_str());
            }
            break;
        case ANY_TSTRING:
            {
                std::ostringstream oss;
                oss << "String Value ["<< key <<"] : " << (value.str ? value.str : "");
                gEnv->pConsole->PrintLine(oss.str().c_str());
            }
            break;
        default:
            continue;
        }
    }
    return pH->EndFunction();
}
// 持久化到数据库
void Database::SaveToDB()
{
    ExecuteTransaction([&](const SQLite::Database& db)
    {
        SQLite::Statement clear(db, "DELETE FROM Store");
        clear.exec();

        SQLite::Statement insert(db,
                                 "INSERT INTO Store (key, type, value) VALUES (?, ?, ?)");

        for (const auto& [key, value] : m_cache)
        {
            insert.bind(1, key);
            insert.bind(2, value.type);

            switch (value.type)
            {
            case ANY_TBOOLEAN:
                insert.bind(3, value.b ? "1" : "0");
                break;
            case ANY_TNUMBER:
                insert.bind(3, std::to_string(value.number));
                break;
            case ANY_TSTRING:
                insert.bind(3, value.str ? value.str : "");
                break;
            default: continue;
            }

            insert.exec();
            insert.reset();
        }
    });
}


// 方法注册
void Database::RegisterMethods()
{
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &Database::

    SCRIPT_REG_TEMPLFUNC(Set, "key, value");
    SCRIPT_REG_TEMPLFUNC(Get, "key");
    SCRIPT_REG_TEMPLFUNC(Delete, "key");
    SCRIPT_REG_TEMPLFUNC(Exists, "key");
    SCRIPT_REG_TEMPLFUNC(Flush, "");
    SCRIPT_REG_TEMPLFUNC(Dump, "");
}