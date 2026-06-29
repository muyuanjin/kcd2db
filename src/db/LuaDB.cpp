//
// Created by muyuanjin on 2025/3/24.
//

#include "LuaDB.h"
#include <cryengine/IConsole.h>
#include <cryengine/IGame.h>
#include <cwchar>
#include <cstdint>
#include <ranges>
#include <sstream>
#include <unordered_set>
#include <string>
#include <windows.h>
#include "../hooks/CursorHook.h"
#include "../lua/db.h"
#include "../lua/LuaRunner.h"
#include "../util/StringUtils.h"

namespace
{
constexpr char kDatabasePath[] = "./kcd2db.db";
constexpr wchar_t kDatabasePathWide[] = L".\\kcd2db.db";

std::string GetFullPath(const wchar_t* path)
{
    const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (required == 0)
    {
        return "<unknown>";
    }

    std::wstring buffer(required, L'\0');
    const DWORD size = GetFullPathNameW(path, required, buffer.data(), nullptr);
    if (size == 0)
    {
        return "<unknown>";
    }

    buffer.resize(size);
    return kcd2db::WideToUtf8(buffer.c_str());
}

void LogDatabaseFileDiagnostics(const char* stage)
{
    const std::string fullPath = GetFullPath(kDatabasePathWide);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(kDatabasePathWide, GetFileExInfoStandard, &data))
    {
        LogWarn("SQLite database file check after %s: path=%s, GetFileAttributesExW failed with %lu.",
                stage,
                fullPath.c_str(),
                GetLastError());
        return;
    }

    const auto size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32)
        | static_cast<std::uint64_t>(data.nFileSizeLow);
    LogDebug("SQLite database file check after %s: path=%s, attributes=0x%08lX, size=%llu bytes.",
             stage,
             fullPath.c_str(),
             data.dwFileAttributes,
             static_cast<unsigned long long>(size));
}

std::unique_ptr<SQLite::Database> OpenDatabase()
{
    const std::string fullPath = GetFullPath(kDatabasePathWide);
    LogDebug("Opening SQLite database: relative path=%s, absolute path=%s, flags=OPEN_READWRITE|OPEN_CREATE.",
             kDatabasePath,
             fullPath.c_str());
    try
    {
        auto db = std::make_unique<SQLite::Database>(kDatabasePath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        LogDebug("SQLite database opened.");
        LogDatabaseFileDiagnostics("open");
        return db;
    }
    catch (const std::exception& e)
    {
        LogError("SQLite database open failed: relative path=%s, absolute path=%s, error=%s, GetLastError=%lu.",
                 kDatabasePath,
                 fullPath.c_str(),
                 e.what(),
                 GetLastError());
        LogDatabaseFileDiagnostics("failed open");
        throw;
    }
}

void LogDatabaseList(SQLite::Database& db)
{
    try
    {
        SQLite::Statement stmt(db, "PRAGMA database_list");
        while (stmt.executeStep())
        {
            LogDebug("SQLite database_list: seq=%d, name=%s, file=%s",
                     stmt.getColumn(0).getInt(),
                     stmt.getColumn(1).getString().c_str(),
                     stmt.getColumn(2).getString().c_str());
        }
    }
    catch (const std::exception& e)
    {
        LogWarn("Failed to query SQLite database_list: %s", e.what());
    }
}

class TempTableGuard
{
public:
    TempTableGuard(SQLite::Database& db, const std::string& tableName, const std::string& schema) :
        m_db(db),
        m_tableName(tableName)
    {
        const std::string createSql = "CREATE TEMP TABLE IF NOT EXISTS " + m_tableName + " " + schema;
        SQLite::Statement createStmt(m_db, createSql.c_str());
        createStmt.exec();
    }

    ~TempTableGuard()
    {
        try
        {
            const std::string dropSql = "DROP TABLE IF EXISTS " + m_tableName;
            SQLite::Statement dropStmt(m_db, dropSql.c_str());
            dropStmt.exec();
        }
        catch (...)
        {
        }
    }

    void Clear() const
    {
        const std::string clearSql = "DELETE FROM " + m_tableName;
        SQLite::Statement clearStmt(m_db, clearSql.c_str());
        clearStmt.exec();
    }

private:
    SQLite::Database& m_db;
    std::string m_tableName;
};

const char* ScriptAnyTypeName(const ScriptAnyType type)
{
    switch (type)
    {
    case ANY_ANY: return "any";
    case ANY_TNIL: return "nil";
    case ANY_TBOOLEAN: return "boolean";
    case ANY_THANDLE: return "handle";
    case ANY_TNUMBER: return "number";
    case ANY_TSTRING: return "string";
    case ANY_TTABLE: return "table";
    case ANY_TFUNCTION: return "function";
    case ANY_TUSERDATA: return "userdata";
    case ANY_TVECTOR: return "vector";
    default: return "unknown";
    }
}

bool IsSupportedRawLuaDBValue(const ScriptAnyType type)
{
    return type == ANY_TBOOLEAN || type == ANY_TNUMBER || type == ANY_TSTRING;
}

bool TraceLuaDBCallsEnabled()
{
    static const bool enabled = []()
    {
        const wchar_t* commandLine = GetCommandLineW();
        return commandLine && std::wcsstr(commandLine, L"-kcd2dbTraceLuaDBCalls") != nullptr;
    }();
    return enabled;
}
}

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
    m_db(OpenDatabase()),
    m_lastSaveTime(std::chrono::steady_clock::now())
{
    LogDatabaseList(*m_db);

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
    LogDebug("LuaDB schema initialization completed.");
    LogDatabaseFileDiagnostics("schema initialization");

    SyncCacheWithDatabase();

    CheckAndVacuum(*m_db);
    LogDebug("LuaDB vacuum check completed");
}

bool LuaDB::isRegistered() const
{
    std::lock_guard lock(m_mutex);
    return m_registered;
}

void LuaDB::RegisterLuaAPI()
{
    std::lock_guard lock(m_mutex);
    const DWORD threadId = GetCurrentThreadId();
    if (m_registered)
    {
        LogDebug("LuaDB is already registered on thread %lu.", threadId);
        return;
    }

    LogDebug("RegisterLuaAPI started on thread %lu.", threadId);
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
    SCRIPT_REG_TEMPLFUNC(CursorDeclare, "path");
    LogDebug("Registered LuaDB method CursorDeclare");
    SCRIPT_REG_TEMPLFUNC(CursorLock, "path");
    LogDebug("Registered LuaDB method CursorLock");
    SCRIPT_REG_TEMPLFUNC(CursorUnlock, "");
    LogDebug("Registered LuaDB method CursorUnlock");

    if (m_pSS->ExecuteBuffer(db_lua, strlen(db_lua), "db.lua"))
    {
        LogDebug("DB lua API loaded on thread %lu.", threadId);
    }
    else
    {
        LogError("DB lua API load failed on thread %lu.", threadId);
    }
    gEnv->pGame->GetIGameFramework()->RegisterListener(this, "LuaDB", FRAMEWORKLISTENERPRIORITY_DEFAULT);
    LogDebug("LuaDB registered as game framework listener on thread %lu.", threadId);
    LuaRunner::Instance().StartFromCommandLine();
    m_registered = true;
    LogInfo("LuaDB loading completed.");
}

void LuaDB::SyncCacheWithDatabase()
{
    std::lock_guard lock(m_mutex);
    SyncCacheWithDatabaseLocked();
}

void LuaDB::SyncCacheWithDatabaseLocked()
{
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
        LogInfo("Loaded %zu entries from %s", cache.size(), savefile.empty() ? "[Global]" : savefile.c_str());
    };

    m_globalCache.clear();
    LoadCache(m_globalCache, "");
    if (!m_saveCacheFileName.empty())
    {
        m_saveCache.clear();
        LoadCache(m_saveCache, m_saveCacheFileName);
    }
}

int LuaDB::GenericAccess(IFunctionHandler* pH, const AccessType action, const bool isGlobal)
{
    constexpr auto ArgError = [](auto* handler) { return handler->EndFunction(false); };
    constexpr auto ActionName = [](const AccessType action)
    {
        switch (action)
        {
        case AccessType::Set: return "Set";
        case AccessType::Get: return "Get";
        case AccessType::Del: return "Del";
        case AccessType::Exi: return "Exi";
        case AccessType::All: return "All";
        default: return "Unknown";
        }
    };

    try
    {
        const char* key = nullptr;
        ScriptAnyValue value;
        const char* funcName = pH->GetFuncName();
        const DWORD threadId = GetCurrentThreadId();

        if ((action != AccessType::All && !pH->GetParam(1, key)) || (action == AccessType::Set && !pH->
            GetParamAny(2, value)))
        {
            LogWarn("LuaDB.%s invalid arguments on thread %lu: action=%s, scope=%s, key=%s, valueType=%s",
                    funcName ? funcName : "<unknown>",
                    threadId,
                    ActionName(action),
                    isGlobal ? "global" : "save",
                    key ? key : "<missing>",
                    action == AccessType::Set ? ScriptAnyTypeName(value.type) : "n/a");
            return ArgError(pH);
        }

        if (action == AccessType::Set && !IsSupportedRawLuaDBValue(value.type))
        {
            LogWarn("LuaDB.%s unsupported value type on thread %lu: scope=%s, key=%s, valueType=%s",
                    funcName ? funcName : "<unknown>",
                    threadId,
                    isGlobal ? "global" : "save",
                    key ? key : "<missing>",
                    ScriptAnyTypeName(value.type));
            return pH->EndFunction(false);
        }

        if (TraceLuaDBCallsEnabled())
        {
            LogDebug("LuaDB.%s called on thread %lu: action=%s, scope=%s, key=%s, valueType=%s",
                     funcName ? funcName : "<unknown>",
                     threadId,
                     ActionName(action),
                     isGlobal ? "global" : "save",
                     key ? key : "<all>",
                     action == AccessType::Set ? ScriptAnyTypeName(value.type) : "n/a");
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

int LuaDB::CursorDeclare(IFunctionHandler* pH)
{
    const char* path = nullptr;
    if (!pH->GetParam(1, path) || !path || path[0] == '\0')
    {
        LogWarn("LuaDB.CursorDeclare invalid cursor path.");
        return pH->EndFunction(false);
    }

    return pH->EndFunction(CursorHook::DeclareCursorPath(path));
}

int LuaDB::CursorLock(IFunctionHandler* pH)
{
    const char* path = nullptr;
    if (!pH->GetParam(1, path) || !path || path[0] == '\0')
    {
        LogWarn("LuaDB.CursorLock invalid cursor path.");
        return pH->EndFunction(false);
    }

    return pH->EndFunction(CursorHook::LockCursorPath(path));
}

int LuaDB::CursorUnlock(IFunctionHandler* pH)
{
    return pH->EndFunction(CursorHook::UnlockCursorPath());
}

void LuaDB::OnLoadGame(ILoadGame* pLoadGame)
{
    try
    {
        const char* fileName = pLoadGame ? pLoadGame->GetFileName() : nullptr;
        if (!fileName)
        {
            LogError("Load game listener received a null save file name on thread %lu.", GetCurrentThreadId());
            return;
        }

        const std::string loadFileName = fileName;
        LogInfo("Load Game on thread %lu: %s", GetCurrentThreadId(), loadFileName.c_str());
        // 记录当前本地缓存对应的存档文件名。
        std::lock_guard lock(m_mutex);
        m_saveCacheFileName = loadFileName;
        SyncCacheWithDatabaseLocked();
    }
    catch (const std::exception& e)
    {
        LogError("Load data failed on thread %lu: %s", GetCurrentThreadId(), e.what());
    }
    catch (...)
    {
        LogError("Load data failed on thread %lu: Unknown error", GetCurrentThreadId());
    }
}

void LuaDB::OnSaveGame(ISaveGame* pSaveGame)
{
    const char* fileName = pSaveGame ? pSaveGame->GetFileName() : nullptr;
    if (!fileName)
    {
        LogError("Save game listener received a null save file name on thread %lu.", GetCurrentThreadId());
        return;
    }

    const std::string newSave = fileName;
    LogInfo("Save Game on thread %lu: %s", GetCurrentThreadId(), newSave.c_str());
    // 将当前缓存作为完整快照写入，避免同名存档复用时残留旧键。
    std::lock_guard lock(m_mutex);
    try
    {
        ExecuteTransaction([this, &newSave](SQLite::Database& db)
        {
            SQLite::Statement deleteStmt(db, "DELETE FROM Store WHERE savefile = ?");
            deleteStmt.bind(1, newSave);
            deleteStmt.exec();

            if (m_saveCache.empty())
            {
                return;
            }

            SQLite::Statement stmt(db,
                                   "INSERT INTO Store (key, savefile, type, value, updated_at) "
                                   "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)");
            for (const auto& [k, v] : m_saveCache)
            {
                stmt.bind(1, k);
                stmt.bind(2, newSave);
                stmt.bind(3, v.anyType());
                stmt.bind(4, serializeValue(v));
                stmt.exec();
                stmt.reset();
            }
        });
        LogInfo("Data saved: %zu entries", m_saveCache.size());
    }
    catch (const std::exception& e)
    {
        LogError("Save data failed: %s", e.what());
    }
    catch (...)
    {
        LogError("Save data failed: Unknown error");
    }
    m_saveCacheFileName = newSave;
}

void LuaDB::OnPostUpdate(float /*fDeltaTime*/)
{
    using namespace std::chrono;
    constexpr seconds SAVE_INTERVAL{1}; // 1秒间隔

    LuaRunner::Instance().ExecuteQueuedScripts(gEnv ? gEnv->pScriptSystem : nullptr);

    std::lock_guard lock(m_mutex);
    if (!m_globalDirty) return;
    if (const auto now = steady_clock::now(); now - m_lastSaveTime < SAVE_INTERVAL) return;

    LogDebug("OnPostUpdate flushing global data on thread %lu.", GetCurrentThreadId());
    try
    {
        int successCount = 0;
        ExecuteTransaction([this, &successCount](SQLite::Database& db)
        {
            // 创建临时表来保存当前所有的键，并在作用域结束后自动释放
            TempTableGuard tempKeys(db, "TempKeys", "(key TEXT PRIMARY KEY)");
            tempKeys.Clear();
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
            LogInfo("Global data saved: %zu entries", m_globalCache.size());
        }
        else
        {
            LogError("Global save failed: %d/%zu entries saved", successCount, m_globalCache.size());
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
    // Clear even after failure: this prevents an unsavable value or persistent SQLite
    // error from causing repeated high-frequency flush attempts. New SetG/DelG calls
    // will mark the global cache dirty again.
    m_globalDirty = false;
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
    oss << "[Save Data For : " << (m_saveCacheFileName.empty() ? "No save file bound to cache" : m_saveCacheFileName) << "]";
    dumpCache(m_saveCache, oss.str());
    return pH->EndFunction();
}
