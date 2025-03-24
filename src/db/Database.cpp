//
// Created by muyuanjin on 2025/3/24.
//

#include "Database.h"

#include <sstream>


// 初始化数据库
Database::Database(SSystemGlobalEnvironment* env)
{
    CScriptableBase::Init(env->pScriptSystem, env->pSystem);
    SetGlobalName("LuaDB");

    // 初始化SQLite
    m_dbPath = "./luadata.db";
    m_db = std::make_unique<SQLite::Database>(m_dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

    // 创建表结构 - 添加savefile字段用于区分全局数据和存档数据
    ExecuteTransaction([&](SQLite::Database& db)
    {
        db.exec("CREATE TABLE IF NOT EXISTS Store ("
            "key TEXT NOT NULL,"
            "savefile TEXT,"  // 存档ID，为空表示全局数据
            "type INTEGER,"
            "value TEXT,"
            "PRIMARY KEY (key, savefile))");
    });

    // 初始化当前存档为空，数据变更标记为false
    m_currentSaveGame = "";
    m_dataChanged = false;

    LoadFromDB();
    RegisterMethods();
    env->pGame->GetIGameFramework()->RegisterListener(this, "LuaDB", FRAMEWORKLISTENERPRIORITY_DEFAULT);
}

Database::~Database()
{
    SaveToDB();
}

void Database::OnSaveGame(ISaveGame* pSaveGame)
{
    const std::string newSaveFileName = pSaveGame->GetFileName();
    const std::string oldSaveFileName = m_currentSaveGame;
    
    Log("Save Game : %s (Previous: %s)", newSaveFileName.c_str(), oldSaveFileName.c_str());
    
    // 如果是从旧存档保存到新存档，复制数据
    if (!oldSaveFileName.empty() && oldSaveFileName != newSaveFileName) {
        Log("Copying data from previous save '%s' to new save '%s'", 
            oldSaveFileName.c_str(), newSaveFileName.c_str());
            
        ExecuteTransaction([&](SQLite::Database& db)
        {
            // 复制旧存档的数据到新存档（除非已在新存档中存在）
            db.exec("INSERT OR IGNORE INTO Store (key, savefile, type, value) "
                    "SELECT key, '" + newSaveFileName + "', type, value "
                    "FROM Store WHERE savefile = '" + oldSaveFileName + "'");
        });
        
        // 重新加载数据以反映变化
        m_currentSaveGame = newSaveFileName;
        LoadFromDB();
    } else {
        // 更新当前存档文件名
        m_currentSaveGame = newSaveFileName;
        
        // 标记数据有变动，需要保存
        MarkDataChanged();
        
        // 保存当前数据到数据库
        SaveToDB();
    }
}

void Database::OnLoadGame(ILoadGame* pLoadGame)
{
    const std::string loadFileName = pLoadGame->GetFileName();
    Log("Load Game : %s", loadFileName.c_str());
    
    // 更新当前存档文件名
    m_currentSaveGame = loadFileName;
    
    // 重新加载数据
    LoadFromDB();
}

// 数据变化检测
bool Database::HasDataChanged() const
{
    std::lock_guard lock(m_mutex);
    return m_dataChanged;
}

// 标记数据变更
void Database::MarkDataChanged()
{
    std::lock_guard lock(m_mutex);
    m_dataChanged = true;
}

void Database::LoadFromDB()
{
    // 清空当前缓存
    m_cache.clear();
    
    std::lock_guard lock(m_mutex);
    int count = 0;
    
    // 加载全局数据（savefile为空的数据）
    SQLite::Statement queryGlobal(*m_db, "SELECT key, type, value FROM Store WHERE savefile = ''");
    while (queryGlobal.executeStep())
    {
        std::string key = queryGlobal.getColumn(0).getText();
        const int type = queryGlobal.getColumn(1).getInt();
        std::string value = queryGlobal.getColumn(2).getText();

        Log("Loading global key: %s, type: %d, value: %s", key.c_str(), type, value.c_str());

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

        // 使用"global:"前缀存储全局数据在内存中
        std::string globalKey = "global:" + key;
        m_cache[globalKey] = any;
        count++;
    }
    
    // 如果有当前存档，加载存档相关数据
    if (!m_currentSaveGame.empty())
    {
        SQLite::Statement querySave(*m_db, "SELECT key, type, value FROM Store WHERE savefile = ?");
        querySave.bind(1, m_currentSaveGame);
        
        while (querySave.executeStep())
        {
            std::string key = querySave.getColumn(0).getText();
            const int type = querySave.getColumn(1).getInt();
            std::string value = querySave.getColumn(2).getText();

            Log("Loading save-specific key: %s, type: %d, value: %s", key.c_str(), type, value.c_str());

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

            // 存档特定数据直接使用键名存储在内存中
            m_cache[key] = any;
            count++;
        }
    }
    
    std::ostringstream oss;
    oss << "Loaded " << count << " entries from database.";
    gEnv->pConsole->PrintLine(oss.str().c_str());
    
    // 重置数据变更标记
    m_dataChanged = false;
}

// 原子化操作
void Database::ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const
{
    SQLite::Transaction transaction(*m_db);
    task(*m_db);
    transaction.commit();
}

// 数据设置（线程安全）- 与当前存档关联
int Database::Set(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    ScriptAnyValue value;

    if (pH->GetParamCount() < 2 ||
        !pH->GetParam(1, keyParam) ||
        !pH->GetParamAny(2, value))
    {
        return pH->EndFunction(false);
    }

    const std::string key = keyParam ? keyParam : "";

    std::lock_guard lock(m_mutex);
    m_cache[key] = value;
    MarkDataChanged();

    return pH->EndFunction(true);
}

// 数据获取（带类型擦除）- 与当前存档关联
int Database::Get(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction();
    }

    const std::string key = keyParam ? keyParam : "";
    
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

// 删除数据 - 与当前存档关联
int Database::Delete(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction(false);
    }
    
    std::string key = keyParam ? keyParam : "";

    std::lock_guard lock(m_mutex);
    bool result = m_cache.erase(key) > 0;
    if (result) {
        MarkDataChanged();
    }
    return pH->EndFunction(result);
}

// 检查数据是否存在 - 与当前存档关联
int Database::Exists(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction(false);
    }
    
    std::string key = keyParam ? keyParam : "";

    std::lock_guard lock(m_mutex);
    return pH->EndFunction(m_cache.find(key) != m_cache.end());
}

// 全局数据设置
int Database::SetGlobal(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    ScriptAnyValue value;

    if (pH->GetParamCount() < 2 ||
        !pH->GetParam(1, keyParam) ||
        !pH->GetParamAny(2, value))
    {
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";
    std::string globalKey = "global:" + key;
    
    std::lock_guard lock(m_mutex);
    m_cache[globalKey] = value;
    MarkDataChanged();

    return pH->EndFunction(true);
}

// 全局数据获取
int Database::GetGlobal(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction();
    }
    
    std::string key = keyParam ? keyParam : "";
    std::string globalKey = "global:" + key;
    
    std::lock_guard lock(m_mutex);
    const auto it = m_cache.find(globalKey);
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

// 删除全局数据
int Database::DeleteGlobal(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";
    std::string globalKey = "global:" + key;
    
    std::lock_guard lock(m_mutex);
    bool result = m_cache.erase(globalKey) > 0;
    if (result) {
        MarkDataChanged();
    }
    return pH->EndFunction(result);
}

// 检查全局数据是否存在
int Database::ExistsGlobal(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";
    std::string globalKey = "global:" + key;
    
    std::lock_guard lock(m_mutex);
    return pH->EndFunction(m_cache.find(globalKey) != m_cache.end());
}

int Database::Flush(IFunctionHandler* pH)
{
    std::lock_guard lock(m_mutex);
    SaveToDB();
    return pH->EndFunction();
}

int Database::Dump(IFunctionHandler* pH)
{
    std::lock_guard lock(m_mutex);
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
    if (!m_dataChanged) {
        Log("No data changes detected, skipping database save");
        return;
    }
    
    int savedCount = 0;
    
    ExecuteTransaction([&](SQLite::Database& db)
    {
        // 保存全局数据和当前存档数据
        SQLite::Statement insert(db, 
            "INSERT OR REPLACE INTO Store (key, savefile, type, value) VALUES (?, ?, ?, ?)");

        for (const auto& [key, value] : m_cache)
        {
            std::string actualKey;
            std::string saveId;
            
            // 判断是否是全局数据
            if (key.compare(0, 7, "global:") == 0) {
                actualKey = key.substr(7);  // 去掉前缀
                saveId = "";                // 全局数据的savefile为空
            } else {
                actualKey = key;            // 存档数据直接使用键名
                saveId = m_currentSaveGame; // 当前存档ID
            }
            
            insert.bind(1, actualKey);
            insert.bind(2, saveId);
            insert.bind(3, value.type);

            switch (value.type)
            {
            case ANY_TBOOLEAN:
                insert.bind(4, value.b ? "1" : "0");
                break;
            case ANY_TNUMBER:
                insert.bind(4, std::to_string(value.number));
                break;
            case ANY_TSTRING:
                insert.bind(4, value.str ? value.str : "");
                break;
            default: continue;
            }

            insert.exec();
            insert.reset();
            savedCount++;
        }
    });
    
    Log("Saved %d entries to database", savedCount);
    
    // 重置数据变更标记
    m_dataChanged = false;
}

// 方法注册
void Database::RegisterMethods()
{
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &Database::

    // 存档关联的数据方法
    SCRIPT_REG_TEMPLFUNC(Set, "key, value");
    SCRIPT_REG_TEMPLFUNC(Get, "key");
    SCRIPT_REG_TEMPLFUNC(Delete, "key");
    SCRIPT_REG_TEMPLFUNC(Exists, "key");
    
    // 全局数据方法（跨存档）
    SCRIPT_REG_TEMPLFUNC(SetGlobal, "key, value");
    SCRIPT_REG_TEMPLFUNC(GetGlobal, "key");
    SCRIPT_REG_TEMPLFUNC(DeleteGlobal, "key");
    SCRIPT_REG_TEMPLFUNC(ExistsGlobal, "key");
    
    // 工具方法
    SCRIPT_REG_TEMPLFUNC(Flush, "");
    SCRIPT_REG_TEMPLFUNC(Dump, "");
}
