//
// Created by muyuanjin on 2025/3/24.
//

#include "Database.h"
#include "../kcd2/IConsole.h"
#include "../kcd2/IGame.h"
#include <optional>
#include <sstream>
#include <unordered_set>


// Database.cpp 优化版本
Database::Database(SSystemGlobalEnvironment* env) :
    m_db(std::make_unique<SQLite::Database>("./kcd2db.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)),
    m_lastSaveTime(std::chrono::steady_clock::now())
{
    CScriptableBase::Init(env->pScriptSystem, env->pSystem);
    SetGlobalName("LuaDB");

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
    });

    env->pGame->GetIGameFramework()->RegisterListener(this, "LuaDB", FRAMEWORKLISTENERPRIORITY_DEFAULT);
    SyncCacheWithDatabase();
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &Database::

    // 存档关联的数据方法
    SCRIPT_REG_TEMPLFUNC(Set, "key, value");
    SCRIPT_REG_TEMPLFUNC(Get, "key");
    SCRIPT_REG_TEMPLFUNC(Del, "key");
    SCRIPT_REG_TEMPLFUNC(Exi, "key");

    // 全局数据方法（跨存档）
    SCRIPT_REG_TEMPLFUNC(SetG, "key, value");
    SCRIPT_REG_TEMPLFUNC(GetG, "key");
    SCRIPT_REG_TEMPLFUNC(DelG, "key");
    SCRIPT_REG_TEMPLFUNC(ExiG, "key");

    // 工具方法
    SCRIPT_REG_TEMPLFUNC(Dump, "");
}

Database::~Database()
{
    gEnv->pGame->GetIGameFramework()->UnregisterListener(this);
    m_db->exec("VACUUM");
}

void Database::SyncCacheWithDatabase()
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
            if (auto val = ParseAnyValue(stmt.getColumn(1), stmt.getColumn(2)))
            {
                cache.emplace(stmt.getColumn(0), std::move(*val));
            }
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

int Database::GenericAccess(IFunctionHandler* pH, const AccessType action, const bool isGlobal)
{
    constexpr auto ArgError = [](auto* handler) { return handler->EndFunction(false); };
    const char* key = nullptr;
    ScriptAnyValue value;

    if (!pH->GetParam(1, key) || (action == AccessType::Set && !pH->GetParamAny(2, value)))
    {
        LogWarn("Invalid arguments");
        return ArgError(pH);
    }

    std::lock_guard lock(m_mutex);
    auto& cache = isGlobal ? m_globalCache : m_saveCache;

    if (!isGlobal && m_currentSaveGame.empty())
    {
        LogWarn("Using save-specific API without active save, use global API instead");
        return ArgError(pH);
    }

    switch (action)
    {
    case AccessType::Set:
        cache[key] = value;
        if (isGlobal)
        {
            m_globalDirty = true;
        }
        return pH->EndFunction(true);
    case AccessType::Get:
        {
            const auto it = cache.find(key);
            return it != cache.end() ? pH->EndFunction(it->second) : pH->EndFunction();
        }
    case AccessType::Del:
        {
            const bool erased = cache.erase(key) > 0;
            if (isGlobal && erased)
            {
                m_globalDirty = true;
            }
            return pH->EndFunction(erased);
        }
    case AccessType::Exi:
        return pH->EndFunction(cache.contains(key));
    }
    return ArgError(pH);
}

void Database::OnLoadGame(ILoadGame* pLoadGame)
{
    const std::string loadFileName = pLoadGame->GetFileName();
    LogInfo("Load Game : %s", loadFileName.c_str());
    // 更新当前存档名
    m_currentSaveGame = loadFileName;
    SyncCacheWithDatabase();
}

void Database::OnSaveGame(ISaveGame* pSaveGame)
{
    const auto newSave = pSaveGame->GetFileName();
    LogInfo("Save Game : %s", newSave);
    // 将 m_saveCache 写入数据库
    try
    {
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
    }
    catch (const std::exception& e)
    {
        LogError("Save data error: %s", e.what());
    }
    m_currentSaveGame = newSave;
}

void Database::OnPostUpdate(float /*fDeltaTime*/)
{
    using namespace std::chrono;
    constexpr seconds SAVE_INTERVAL{1}; // 1秒间隔

    std::lock_guard lock(m_mutex);
    if (!m_globalDirty) return;
    if (const auto now = steady_clock::now(); now - m_lastSaveTime < SAVE_INTERVAL) return;

    int successCount = 0;

    try
    {
        ExecuteTransaction([this, &successCount](const SQLite::Database& db)
        {
            SQLite::Statement clearStmt(db, "DELETE FROM Store WHERE savefile = ''");
            clearStmt.exec();
            SQLite::Statement stmt(db,
                                   "INSERT INTO Store (key, savefile, type, value) "
                                   "VALUES (?, '', ?, ?) "
                                   "ON CONFLICT(key, savefile) DO UPDATE SET "
                                   "type=excluded.type, value=excluded.value, updated_at=CURRENT_TIMESTAMP");
            for (const auto& [k, v] : m_globalCache)
            {
                if (auto val = SerializeAnyValue(v))
                {
                    stmt.bind(1, k);
                    stmt.bind(2, v.type);
                    stmt.bind(3, *val);
                    stmt.exec();
                    stmt.reset();
                    successCount++;
                }
            }
        });
        m_globalDirty = false;
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
}

void Database::ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const
{
    SQLite::Transaction transaction(*m_db);
    task(*m_db);
    transaction.commit();
}

int Database::BatchOperation(SQLite::Database& db, const Cache& cache, const std::string& savefile)
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
    SQLite::Transaction transaction(db);

    int count = 0;

    for (const auto& [k, v] : cache)
    {
        if (auto val = SerializeAnyValue(v))
        {
            stmt.bind(1, k);
            stmt.bind(2, savefile);
            stmt.bind(3, v.type);
            stmt.bind(4, *val);
            stmt.exec();
            stmt.reset();
        }
        count++;
    }
    transaction.commit();
    return count;
}

std::optional<ScriptAnyValue> Database::ParseAnyValue(const int type, const std::string& value)
{
    ScriptAnyValue any;
    switch (type)
    {
    case ANY_TBOOLEAN:
        {
            any.type = ANY_TBOOLEAN;
            any.b = std::stoi(value) != 0;
        }
        break;
    case ANY_TNUMBER:
        {
            any.type = ANY_TNUMBER;
            any.number = std::stod(value);
        }
        break;
    case ANY_TSTRING:
        {
            any.type = ANY_TSTRING;
            // Fix for string encoding issues - ensure we properly handle UTF-8 strings
            any.str = _strdup(value.c_str());
        }
        break;
    default:
        return std::nullopt;
    }
    return any;
}

std::optional<std::string> Database::SerializeAnyValue(const ScriptAnyValue& any)
{
    switch (any.type)
    {
    case ANY_TBOOLEAN:
        return any.b ? "1" : "0";
    case ANY_TNUMBER:
        return std::to_string(any.number);
    case ANY_TSTRING:
        {
            if (!any.str) return "";

            // Return the string directly - SQLite handles UTF-8 text natively
            return std::string(any.str);
        }
    default:
        return std::nullopt;
    }
}

int Database::Dump(IFunctionHandler* pH)
{
    std::lock_guard lock(m_mutex);

    // Helper function to dump a specific cache
    auto dumpCache = [&](auto& cache, const std::string& cacheType)
    {
        gEnv->pConsole->PrintLine(("$3--- " + cacheType + " ---").c_str());
        for (const auto& [key, value] : cache)
        {
            switch (value.type)
            {
            case ANY_TBOOLEAN:
                {
                    std::ostringstream oss;
                    oss << "$3" << " Boolean Value [" << key << "] : " << (value.b ? "true" : "false");
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            case ANY_TNUMBER:
                {
                    std::ostringstream oss;
                    oss << "$3" << " Number Value [" << key << "] : " << value.number;
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            case ANY_TSTRING:
                {
                    std::ostringstream oss;
                    oss << "$3" << " String Value [" << key << "] : " << (value.str ? value.str : "");
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
