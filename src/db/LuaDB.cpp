//
// Created by muyuanjin on 2025/3/24.
//

#include "LuaDB.h"
#include "../kcd2/IConsole.h"
#include "../kcd2/IGame.h"
#include <ranges>
#include <sstream>
#include <unordered_set>
#include <string>
#include "../lua/db.h"

std::string substring(const std::string& input, const long long max_length = 100)
{
    if (input.length() < max_length)
    {
        return input;
    }
    return input.substr(0, max_length) + "...";
}

std::string formatValue(const ScriptValue& value)
{
    switch (value.type())
    {
    case ScriptValue::Type::BOOL: return value.as_bool() ? "true" : "false";
    case ScriptValue::Type::STRING: return substring(value.as_string());
    case ScriptValue::Type::NUMBER: return std::to_string(value.as_number());
    default: return "[Unknown]";
    }
}

ScriptValue parseValue(const int type, const std::string& value)
{
    try
    {
        switch (type)
        {
        case ANY_TBOOLEAN:
            return ScriptValue(std::stoi(value) != 0);
        case ANY_TNUMBER:
            return ScriptValue(std::stof(value));
        case ANY_TSTRING:
            return ScriptValue(value);
        default:
            return {};
        }
    }
    catch (...)
    {
        LogWarn("Failed to parse value: %s", value.c_str());
        return {};
    }
}

std::string serializeValue(const ScriptValue& any)
{
    switch (any.type())
    {
    case ScriptValue::Type::BOOL:
        return any.as_bool() ? "1" : "0";
    case ScriptValue::Type::NUMBER:
        return std::to_string(any.as_number());
    case ScriptValue::Type::STRING:
        return any.as_string();
    default:
        return "";
    }
}

int BatchOperation(SQLite::Database& db, const std::unordered_map<std::string, ScriptValue>& cache,
                   const std::string& savefile)
{
    static constexpr auto UPSERT_SQL = R"sql(
        INSERT INTO Store (key, savefile, type, value, updated_at)
        VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
        ON CONFLICT DO UPDATE SET
            type = excluded.type,
            value = excluded.value,
            updated_at = excluded.updated_at
    )sql";

    SQLite::Statement stmt(db, UPSERT_SQL);
    int count = 0;

    for (auto it = cache.begin(); it != cache.end();)
    {
        constexpr size_t BATCH_SIZE = 100;
        SQLite::Transaction transaction(db);

        for (size_t batchCount = 0; it != cache.end() && batchCount < BATCH_SIZE; ++it, ++batchCount)
        {
            const auto& [k, v] = *it;
            try
            {
                stmt.bind(1, k);
                stmt.bind(2, savefile);
                stmt.bind(3, v.anyType());
                stmt.bind(4, serializeValue(v));
                stmt.exec();
                stmt.reset();
                ++count;
            }
            catch (const std::exception& e)
            {
                LogError("Failed to batch insert for %s: %s", k.c_str(), e.what());
            }
        }
        transaction.commit();
    }
    return count;
}

void CheckAndVacuum(SQLite::Database& db)
{
    bool shouldVacuum = false;
    // 查询最后执行时间
    {
        if (SQLite::Statement query(db, "SELECT value FROM Meta WHERE key = 'last_vacuum_time'"); query.executeStep())
        {
            time_t lastVacuumTime = 0;
            const std::string valueStr = query.getColumn(0).getString();
            lastVacuumTime = std::stol(valueStr);
            if (const time_t now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                now_t - lastVacuumTime > 7 * 24 * 3600)
            {
                shouldVacuum = true;
            }
        }
        else
        {
            shouldVacuum = true; // 无记录时触发第一次VACUUM
        }
    }

    // 执行优化逻辑
    if (shouldVacuum)
    {
        try
        {
            // VACUUM命令不能在事务中执行
            db.exec("VACUUM");
            // 更新执行时间戳
            const time_t now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            SQLite::Statement update(db,
                                     "INSERT OR REPLACE INTO Meta (key, value) VALUES ('last_vacuum_time', ?)");
            update.bind(1, std::to_string(now_t));
            update.exec();
        }
        catch (const std::exception& e)
        {
            LogError("VACUUM failed: %s", e.what());
        }
    }
}

LuaDB::~LuaDB()
{
    LogDebug("LuaDB destructor called");
}

// Database.cpp 优化版本
LuaDB::LuaDB() :
    m_db(std::make_unique<SQLite::Database>("./kcd2db.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)),
    m_lastSaveTime(std::chrono::steady_clock::now())
{
    ExecuteTransaction([](SQLite::Database& db)
    {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS Store (
                key TEXT NOT NULL,
                savefile TEXT,
                type INTEGER,
                value TEXT,
                created_at INTEGER DEFAULT CURRENT_TIMESTAMP,
                updated_at INTEGER DEFAULT CURRENT_TIMESTAMP,
                UNIQUE (key, savefile)
            )
        )");
        db.exec("CREATE INDEX IF NOT EXISTS idx_store_savefile ON Store(savefile)");
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS Meta (
                key TEXT PRIMARY KEY,
                value TEXT
            )
        )");
    });

    SyncCacheWithDatabase();

    CheckAndVacuum(*m_db);
    LogDebug("LuaDB vacuum check completed");
}

bool LuaDB::isRegistered() const
{
    std::lock_guard lock(m_mutex);
    return m_pSS != nullptr;
}

void LuaDB::RegisterLuaAPI()
{
    std::lock_guard lock(m_mutex);
    CScriptableBase::Init(gEnv->pScriptSystem, gEnv->pSystem);
    SetGlobalName("LuaDB");
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &LuaDB::
    LogDebug("LuaDB data initialized");
    // 存档关联的数据方法
    SCRIPT_REG_TEMPLFUNC(Set, "key, value");
    LogDebug("Registered LuaDB method Set");
    SCRIPT_REG_TEMPLFUNC(Get, "key");
    LogDebug("Registered LuaDB method Get");
    SCRIPT_REG_TEMPLFUNC(Del, "key");
    LogDebug("Registered LuaDB method Del");
    SCRIPT_REG_TEMPLFUNC(Exi, "key");
    LogDebug("Registered LuaDB method Exi");
    SCRIPT_REG_TEMPLFUNC(All, "");
    LogDebug("Registered LuaDB method All");

    // 全局数据方法（跨存档）
    SCRIPT_REG_TEMPLFUNC(SetG, "key, value");
    LogDebug("Registered LuaDB method SetG");
    SCRIPT_REG_TEMPLFUNC(GetG, "key");
    LogDebug("Registered LuaDB method GetG");
    SCRIPT_REG_TEMPLFUNC(DelG, "key");
    LogDebug("Registered LuaDB method DelG");
    SCRIPT_REG_TEMPLFUNC(ExiG, "key");
    LogDebug("Registered LuaDB method ExiG");
    SCRIPT_REG_TEMPLFUNC(AllG, "");
    LogDebug("Registered LuaDB method AllG");

    // 工具方法
    SCRIPT_REG_TEMPLFUNC(Dump, "");
    LogDebug("Registered LuaDB method Dump");

    m_pSS->ExecuteBuffer(db_lua, strlen(db_lua), "db.lua");
    LogInfo("DB lua API loaded");
    gEnv->pGame->GetIGameFramework()->RegisterListener(this, "LuaDB", FRAMEWORKLISTENERPRIORITY_DEFAULT);
    LogInfo("LuaDB registered as game framework listener");
    LogInfo("LuaDB loading completed.");
}

void LuaDB::SyncCacheWithDatabase()
{
    std::lock_guard lock(m_mutex);
    auto LoadCache = [&](auto& cache, const std::string& savefile)
    {
        static constexpr auto SELECT_SQL = R"(
            SELECT key, type, value FROM Store
            WHERE savefile = ? ORDER BY rowid
        )";

        SQLite::Statement stmt(*m_db, SELECT_SQL);
        stmt.bind(1, savefile);

        while (stmt.executeStep())
        {
            SQLite::Column keyCol = stmt.getColumn(0);
            SQLite::Column typeCol = stmt.getColumn(1);
            SQLite::Column valueCol = stmt.getColumn(2);
            cache.emplace(
                keyCol.getString(),
                parseValue(typeCol.getInt(), valueCol.getString())
            );
        }
        LogInfo("Loaded %d entries from %s", cache.size(), savefile.empty() ? "[Global]" : savefile.c_str());
    };

    m_globalCache.clear();
    LoadCache(m_globalCache, "");
    if (!m_currentSaveGame.empty())
    {
        m_saveCache.clear();
        LoadCache(m_saveCache, m_currentSaveGame);
    }
}

int LuaDB::GenericAccess(IFunctionHandler* pH, const AccessType action, const bool isGlobal)
{
    constexpr auto ArgError = [](auto* handler) { return handler->EndFunction(false); };
    try
    {
        const char* key = nullptr;
        ScriptAnyValue value;

        if ((action != AccessType::All && !pH->GetParam(1, key)) || (action == AccessType::Set && !pH->
            GetParamAny(2, value)))
        {
            LogWarn("Invalid arguments");
            return ArgError(pH);
        }
        std::lock_guard lock(m_mutex);
        auto& cache = isGlobal ? m_globalCache : m_saveCache;

        switch (action)
        {
        case AccessType::Set:
            {
                const auto it = ScriptValue(value);
                cache[key] = it;
                LogDebug(isGlobal ? "Set Global %s = %s" : "Set %s = %s", key, formatValue(it).c_str());
                if (isGlobal)
                {
                    m_globalDirty = true;
                }
                return pH->EndFunction(true);
            }
        case AccessType::Get:
            {
                const auto it = cache.find(key);
                return it != cache.end() ? pH->EndFunction(it->second.toAnyValue()) : pH->EndFunction();
            }
        case AccessType::Del:
            {
                const bool erased = cache.erase(key) > 0;
                LogDebug(isGlobal ? "Delete Global %s: %s" : "Delete %s: %s", key, erased ? "OK" : "Not found");
                if (isGlobal && erased)
                {
                    m_globalDirty = true;
                }
                return pH->EndFunction(erased);
            }
        case AccessType::Exi:
            return pH->EndFunction(cache.contains(key));
        case AccessType::All:
            {
                const auto table = m_pSS->CreateTable();
                for (const auto& [k, v] : cache)
                {
                    table->SetValue(k.c_str(), v.toAnyValue());
                }
                return pH->EndFunction(table);
            }
        }
    }
    catch (const std::exception& e)
    {
        LogError("LuaDB: Exception in GenericAccess: %s", e.what());
    }
    catch (...)
    {
        LogError("LuaDB: Unknown exception in GenericAccess");
    }
    return ArgError(pH);
}

void LuaDB::OnLoadGame(ILoadGame* pLoadGame)
{
    const std::string loadFileName = pLoadGame->GetFileName();
    LogInfo("Load Game : %s", loadFileName.c_str());
    // 更新当前存档名
    m_currentSaveGame = loadFileName;
    SyncCacheWithDatabase();
}

void LuaDB::OnSaveGame(ISaveGame* pSaveGame)
{
    const auto newSave = pSaveGame->GetFileName();
    LogInfo("Save Game : %s", newSave);
    // 将 m_saveCache 写入数据库
    std::lock_guard lock(m_mutex);
    if (!m_saveCache.empty())
    {
        if (const int successCount = BatchOperation(*m_db, m_saveCache, newSave);
            successCount == m_saveCache.size())
        {
            LogInfo("Data saved: %d entries", m_saveCache.size());
        }
        else
        {
            LogError("Save data error: %d/%d entries saved", successCount, m_saveCache.size());
        }
    }
    m_currentSaveGame = newSave;
}

void LuaDB::OnPostUpdate(float /*fDeltaTime*/)
{
    using namespace std::chrono;
    constexpr seconds SAVE_INTERVAL{1}; // 1秒间隔

    std::lock_guard lock(m_mutex);
    if (!m_globalDirty) return;
    if (const auto now = steady_clock::now(); now - m_lastSaveTime < SAVE_INTERVAL) return;

    try
    {
        int successCount = 0;
        ExecuteTransaction([this, &successCount](const SQLite::Database& db)
        {
            // 创建临时表来保存当前所有的键
            SQLite::Statement createTempStmt(db, "CREATE TEMP TABLE TempKeys (key TEXT PRIMARY KEY)");
            createTempStmt.exec();
            // 清空临时表（如果之前存在）
            SQLite::Statement clearTempStmt(db, "DELETE FROM TempKeys");
            clearTempStmt.exec();
            // 将当前缓存中的键插入临时表
            SQLite::Statement insertTempStmt(db, "INSERT INTO TempKeys (key) VALUES (?)");
            for (const auto& k : m_globalCache | std::views::keys)
            {
                insertTempStmt.bind(1, k);
                insertTempStmt.exec();
                insertTempStmt.reset();
            }
            // 删除掉不在当前缓存中的键对应的记录
            SQLite::Statement deleteStmt(db,
                                         "DELETE FROM Store WHERE savefile = '' AND key NOT IN (SELECT key FROM TempKeys)");
            deleteStmt.exec();
            // 删除临时表
            SQLite::Statement dropTempStmt(db, "DROP TABLE TempKeys");
            dropTempStmt.exec();

            // 插入或更新现有的键
            SQLite::Statement stmt(db,
                                   "INSERT INTO Store (key, savefile, type, value) "
                                   "VALUES (?, '', ?, ?) "
                                   "ON CONFLICT(key, savefile) DO UPDATE SET "
                                   "type=excluded.type, value=excluded.value, updated_at=CURRENT_TIMESTAMP");
            for (const auto& [k, v] : m_globalCache)
            {
                try
                {
                    stmt.bind(1, k);
                    stmt.bind(2, v.anyType());
                    stmt.bind(3, serializeValue(v));
                    stmt.exec();
                    stmt.reset();
                    successCount++;
                }
                catch (const std::exception& e)
                {
                    LogError("Global save failed for key %s: %s", k.c_str(), e.what());
                }
            }
        });
        m_lastSaveTime = steady_clock::now();
        if (successCount == m_globalCache.size())
        {
            LogInfo("Global data saved: %d entries", m_globalCache.size());
        }
        else
        {
            LogError("Global save failed: %d/%d entries saved", successCount, m_globalCache.size());
        }
    }
    catch (const std::exception& e)
    {
        LogError("Global save failed: %s", e.what());
    }
    catch (...)
    {
        LogError("Global save failed: Unknown error");
    }
    m_globalDirty = false; // 重置标志,无论成功与否
}


void LuaDB::ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const
{
    SQLite::Transaction transaction(*m_db);
    task(*m_db);
    transaction.commit();
}

int LuaDB::Dump(IFunctionHandler* pH)
{
    std::lock_guard lock(m_mutex);

    // Helper function to dump a specific cache
    auto dumpCache = [&](auto& cache, const std::string& cacheType)
    {
        gEnv->pConsole->PrintLine(("$6--- " + cacheType + " ---").c_str());
        for (const auto& [key, value] : cache)
        {
            switch (value.type())
            {
            case ScriptValue::Type::BOOL:
                {
                    std::ostringstream oss;
                    oss << "  $5" << key << "  $8Boolean  $3" << (value.as_bool() ? "true" : "false");
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            case ScriptValue::Type::NUMBER:
                {
                    std::ostringstream oss;
                    oss << "  $5" << key << "  $8Number  $3" << value.as_number();
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            case ScriptValue::Type::STRING:
                {
                    std::ostringstream oss;
                    oss << "  $5" << key << "  $8String  $3" << substring(value.as_string());
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            default:
                break;
            }
        }
    };

    dumpCache(m_globalCache, "[Global Data]");
    std::ostringstream oss;
    oss << "[Save Data For : " << (m_currentSaveGame.empty() ? "No active save file" : m_currentSaveGame) << "]";
    dumpCache(m_saveCache, oss.str());
    return pH->EndFunction();
}
