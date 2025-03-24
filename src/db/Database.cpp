//
// Created by muyuanjin on 2025/3/24.
//

#include "Database.h"
#include "../kcd2/IConsole.h"
#include "../kcd2/IGame.h"
#include <optional>
#include <sstream>
#include <ctime>


// 初始化数据库
Database::Database(SSystemGlobalEnvironment* env)
{
    CScriptableBase::Init(env->pScriptSystem, env->pSystem);
    SetGlobalName("LuaDB");

    // 初始化SQLite
    m_dbPath = "./luadata.db";
    m_db = std::make_unique<SQLite::Database>(m_dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

    // 创建表结构 - 添加id作为自增主键，原先的主键变成唯一索引，添加时间戳字段
    ExecuteTransaction([&](SQLite::Database& db)
    {
        db.exec("CREATE TABLE IF NOT EXISTS Store ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT," // 自增主键，仅用于调试
            "key TEXT NOT NULL,"
            "savefile TEXT," // 存档ID，为空表示全局数据
            "type INTEGER,"
            "value TEXT,"
            "created_at INTEGER,"
            "updated_at INTEGER,"
            "UNIQUE (key, savefile))");

        // 添加索引以提高查询性能
        db.exec("CREATE INDEX IF NOT EXISTS idx_store_savefile ON Store(savefile)");
        db.exec("CREATE INDEX IF NOT EXISTS idx_store_type ON Store(type)");
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

    LogInfo("Save Game : %s (Previous: %s)", newSaveFileName.c_str(), oldSaveFileName.c_str());

    Flush(nullptr);

    // 如果是从旧存档保存到新存档，复制数据
    if (!oldSaveFileName.empty() && oldSaveFileName != newSaveFileName)
    {
        LogDebug("Copying data from previous save '%s' to new save '%s'",
            oldSaveFileName.c_str(), newSaveFileName.c_str());

        ExecuteTransaction([&](SQLite::Database& db)
        {
            // 获取当前时间戳用于创建和更新时间字段
            const int64_t currentTime = std::time(nullptr);
            
            // 使用预编译语句复制旧存档的数据到新存档
            SQLite::Statement countStmt(db, 
                "SELECT COUNT(*) FROM Store WHERE savefile = ? AND key NOT IN "
                "(SELECT key FROM Store WHERE savefile = ?)");
            countStmt.bind(1, oldSaveFileName);
            countStmt.bind(2, newSaveFileName);
            
            int recordsToCopy = 0;
            if (countStmt.executeStep()) {
                recordsToCopy = countStmt.getColumn(0).getInt();
            }
            
            if (recordsToCopy > 0) {
                // 准备批量复制的语句
                SQLite::Statement selectStmt(db, 
                    "SELECT key, type, value FROM Store WHERE savefile = ? AND key NOT IN "
                    "(SELECT key FROM Store WHERE savefile = ?) LIMIT 100 OFFSET ?");
                
                SQLite::Statement insertStmt(db,
                    "INSERT INTO Store (key, savefile, type, value, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)");
                
                // 分批复制数据
                int offset = 0;
                int totalCopied = 0;
                const int batchSize = 100;

                while (totalCopied < recordsToCopy) {
                    selectStmt.bind(1, oldSaveFileName);
                    selectStmt.bind(2, newSaveFileName);
                    selectStmt.bind(3, offset);
                    
                    int batchCopied = 0;
                    // 开始内部事务以优化批量插入
                    db.exec("BEGIN");
                    
                    while (selectStmt.executeStep()) {
                        const std::string key = selectStmt.getColumn(0).getText();
                        const int type = selectStmt.getColumn(1).getInt();
                        const std::string value = selectStmt.getColumn(2).getText();
                        
                        insertStmt.bind(1, key);
                        insertStmt.bind(2, newSaveFileName);
                        insertStmt.bind(3, type);
                        insertStmt.bind(4, value);
                        insertStmt.bind(5, currentTime); // 创建时间
                        insertStmt.bind(6, currentTime); // 更新时间
                        insertStmt.exec();
                        insertStmt.reset();
                        
                        batchCopied++;
                    }
                    
                    // 提交内部事务
                    db.exec("COMMIT");
                    
                    // 重置语句准备下一批
                    selectStmt.reset();
                    totalCopied += batchCopied;
                    offset += batchSize;
                    
                    // 如果本批次的数量小于批次大小，说明已经处理完所有数据
                    if (batchCopied < batchSize) {
                        break;
                    }
                }
                
                LogInfo("Copied %d records from previous save to new save.", totalCopied);
            } else {
                LogInfo("No new records to copy from previous save.");
            }
        });

        // 重新加载数据以反映变化
        m_currentSaveGame = newSaveFileName;
        LoadFromDB();
    }
    else
    {
        // 更新当前存档文件名
        m_currentSaveGame = newSaveFileName;
        
        // 标记数据有变动，需要保存
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            MarkDataChanged();
        }

        // 保存当前数据到数据库
        SaveToDB();
    }
}

void Database::OnLoadGame(ILoadGame* pLoadGame)
{
    Flush(nullptr);
    const std::string loadFileName = pLoadGame->GetFileName();
    LogInfo("Load Game : %s", loadFileName.c_str());

    // 更新当前存档文件名
    m_currentSaveGame = loadFileName;

    // 重新加载数据
    LoadFromDB();
}

// 数据变化检测
bool Database::HasDataChanged() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dataChanged;
}

// 标记数据变更
void Database::MarkDataChanged()
{
    // 移除锁，因为此方法总是在已经获取m_mutex锁的上下文中调用
    m_dataChanged = true;
}

void Database::LoadFromDB()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_saveCache.clear();
    m_globalCache.clear();
    int count = 0;
    const int BATCH_SIZE = 200; // 每批加载的记录数量

    // 获取全局数据的总数和存档数据的总数
    int globalDataCount = 0;
    int saveDataCount = 0;
    
    SQLite::Statement countGlobalStmt(*m_db, "SELECT COUNT(*) FROM Store WHERE savefile = ''");
    if (countGlobalStmt.executeStep()) {
        globalDataCount = countGlobalStmt.getColumn(0).getInt();
    }
    
    if (!m_currentSaveGame.empty()) {
        SQLite::Statement countSaveStmt(*m_db, "SELECT COUNT(*) FROM Store WHERE savefile = ?");
        countSaveStmt.bind(1, m_currentSaveGame);
        if (countSaveStmt.executeStep()) {
            saveDataCount = countSaveStmt.getColumn(0).getInt();
        }
    }
    
    // 预先为缓存分配空间以减少重新分配
    m_globalCache.reserve(globalDataCount);
    m_saveCache.reserve(saveDataCount);
    
    LogInfo("Loading data: Global entries: %d, Save entries: %d", globalDataCount, saveDataCount);
    
    // 批量加载数据的通用函数
    auto BatchLoadData = [&](bool isGlobal) {
        // 准备预编译语句
        SQLite::Statement stmt(*m_db, isGlobal
            ? "SELECT key, type, value FROM Store WHERE savefile = '' LIMIT ? OFFSET ?"
            : "SELECT key, type, value FROM Store WHERE savefile = ? LIMIT ? OFFSET ?");
            
        // 绑定预编译语句参数
        if (!isGlobal) {
            stmt.bind(1, m_currentSaveGame);
        }
        
        auto& targetCache = isGlobal ? m_globalCache : m_saveCache;
        int totalEntries = isGlobal ? globalDataCount : saveDataCount;
        int offset = 0;
        int loadedCount = 0;
        
        // 分批加载数据
        while (loadedCount < totalEntries) {
            // 绑定LIMIT和OFFSET参数
            if (isGlobal) {
                stmt.bind(1, BATCH_SIZE);
                stmt.bind(2, offset);
            } else {
                stmt.bind(2, BATCH_SIZE);
                stmt.bind(3, offset);
            }
            
            int batchCount = 0;
            while (stmt.executeStep()) {
                const std::string& key = stmt.getColumn(0).getText();
                const int type = stmt.getColumn(1).getInt();
                const std::string value = stmt.getColumn(2).getText();
                
                if (auto any = ParseAnyValue(type, value)) {
                    targetCache[key] = *any;
                    batchCount++;
                }
            }
            
            // 重置语句以便重新使用
            stmt.reset();
            loadedCount += batchCount;
            offset += BATCH_SIZE;
            
            // 如果这批次的数量小于BATCH_SIZE，说明已经处理完所有数据
            if (batchCount < BATCH_SIZE) {
                break;
            }
        }
        
        LogInfo("Loaded %d %s entries from database", loadedCount, isGlobal ? "global" : "save-specific");
        count += loadedCount;
    };
    
    // 先加载全局数据
    if (globalDataCount > 0) {
        BatchLoadData(true);
    }
    
    // 再加载存档特定数据
    if (!m_currentSaveGame.empty() && saveDataCount > 0) {
        BatchLoadData(false);
    }
    
    gEnv->pConsole->PrintLine(("Loaded " + std::to_string(count) + " entries from database.").c_str());
    m_dataChanged = false;
}

std::optional<ScriptAnyValue> Database::ParseAnyValue(int type, const std::string& value)
{
    ScriptAnyValue any;
    any.type = static_cast<ScriptAnyType>(type);

    switch (type)
    {
    case ANY_TBOOLEAN:
        any.b = std::stoi(value) != 0;
        break;
    case ANY_TNUMBER:
        any.number = std::stod(value);
        break;
    case ANY_TSTRING:
        any.str = std::unique_ptr<char[]>(_strdup(value.c_str())).release();
        break;
    default:
        return std::nullopt;
    }
    return any;
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

    // 检查是否有有效的存档加载，如果没有，则无法使用Set方法
    if (m_currentSaveGame.empty())
    {
        LogWarn("Cannot use Set() when no save game is loaded, use SetG() instead");
        return pH->EndFunction(false);
    }

    const std::string key = keyParam ? keyParam : "";

    std::lock_guard lock(m_mutex);
    m_saveCache[key] = value;
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

    // 检查是否有有效的存档加载，如果没有，则无法使用Get方法
    if (m_currentSaveGame.empty())
    {
        LogWarn("Cannot use Get() when no save game is loaded, use GetG() instead");
        return pH->EndFunction();
    }

    const std::string key = keyParam ? keyParam : "";

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_saveCache.find(key);
    if (it == m_saveCache.end())
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

    // 检查是否有有效的存档加载，如果没有，则无法使用Delete方法
    if (m_currentSaveGame.empty())
    {
        LogWarn("Cannot use Delete() when no save game is loaded, use DeleteG() instead");
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";

    std::lock_guard<std::mutex> lock(m_mutex);
    bool result = m_saveCache.erase(key) > 0;
    if (result)
    {
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

    // 检查是否有有效的存档加载，如果没有，则无法使用Exists方法
    if (m_currentSaveGame.empty())
    {
        LogWarn("Cannot use Exists() when no save game is loaded, use ExistsG() instead");
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";

    std::lock_guard<std::mutex> lock(m_mutex);
    return pH->EndFunction(m_saveCache.find(key) != m_saveCache.end());
}

// 全局数据设置
int Database::SetG(IFunctionHandler* pH)
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

    std::lock_guard<std::mutex> lock(m_mutex);
    m_globalCache[key] = value;
    MarkDataChanged();

    return pH->EndFunction(true);
}

// 全局数据获取
int Database::GetG(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction();
    }

    std::string key = keyParam ? keyParam : "";

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_globalCache.find(key);
    if (it == m_globalCache.end())
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
int Database::DeleteG(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";

    std::lock_guard<std::mutex> lock(m_mutex);
    bool result = m_globalCache.erase(key) > 0;
    if (result)
    {
        MarkDataChanged();
    }
    return pH->EndFunction(result);
}

// 检查全局数据是否存在
int Database::ExistsG(IFunctionHandler* pH)
{
    const char* keyParam = nullptr;
    if (!pH->GetParam(1, keyParam))
    {
        return pH->EndFunction(false);
    }

    std::string key = keyParam ? keyParam : "";

    std::lock_guard<std::mutex> lock(m_mutex);
    return pH->EndFunction(m_globalCache.find(key) != m_globalCache.end());
}

int Database::Flush(IFunctionHandler* pH)
{
    try 
    {
        // 使用锁保护
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 只有在有数据变更时才执行保存操作
        if (m_dataChanged) 
        {
            // 添加详细日志以帮助调试
            LogDebug("Flush: Starting database flush operation");
            
            // 调用优化版的保存方法
            SaveToDB();
            
            LogDebug("Flush: Database flush completed successfully");
        } 
        else 
        {
            LogDebug("Flush: No changes to save");
        }
        
        return pH? pH->EndFunction(true): 1;
    }
    catch (const SQLite::Exception& e)
    {
        // 捕获并记录 SQLite 异常
        LogError("Flush: SQLite error during flush: %s (code: %d)", e.what(), e.getErrorCode());
        return pH? pH->EndFunction(false): 1;
    }
    catch (const std::exception& e)
    {
        // 捕获并记录其他标准异常
        LogError("Flush: Error during flush: %s", e.what());
        return  pH? pH->EndFunction(false): 1;
    }
    catch (...)
    {
        // 捕获所有未知异常
        LogError("Flush: Unknown error during flush");
        return pH? pH->EndFunction(false): 1;
    }
}

int Database::Dump(IFunctionHandler* pH)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Helper function to dump a specific cache
    auto dumpCache = [&](const auto& cache, const char* cacheType) {
        gEnv->pConsole->PrintLine(("--- " + std::string(cacheType) + " Data ---").c_str());
        for (const auto& [key, value] : cache)
        {
            switch (value.type)
            {
            case ANY_TBOOLEAN:
                {
                    std::ostringstream oss;
                    oss << cacheType << " Boolean Value [" << key << "] : " << (value.b ? "true" : "false");
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            case ANY_TNUMBER:
                {
                    std::ostringstream oss;
                    oss << cacheType << " Number Value [" << key << "] : " << value.number;
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            case ANY_TSTRING:
                {
                    std::ostringstream oss;
                    oss << cacheType << " String Value [" << key << "] : " << (value.str ? value.str : "");
                    gEnv->pConsole->PrintLine(oss.str().c_str());
                }
                break;
            default:
                continue;
            }
        }
    };
    
    // Dump global data
    dumpCache(m_globalCache, "Global");
    
    // Dump save-specific data
    dumpCache(m_saveCache, "Save");
    
    return pH->EndFunction();
}

// 持久化到数据库
void Database::SaveToDB()
{
    try
    {
        if (!m_dataChanged)
        {
            LogInfo("SaveToDB: No data changes detected, skipping database save");
            return;
        }

        int savedCount = 0;
        const int BATCH_SIZE = 100; // 每批保存的记录数

        LogInfo("SaveToDB: Starting database save operation");
        
        ExecuteTransaction([&](SQLite::Database& db)
    {
        // 获取当前时间戳
        int64_t currentTime = static_cast<int64_t>(std::time(nullptr));
        
        // 准备预编译语句
        SQLite::Statement checkExist(db, "SELECT id, created_at FROM Store WHERE key = ? AND savefile = ?");
        SQLite::Statement insert(db, "INSERT INTO Store (key, savefile, type, value, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)");
        SQLite::Statement update(db, "UPDATE Store SET type = ?, value = ?, updated_at = ? WHERE id = ?");

        // 批量保存数据的通用函数
        auto batchSaveCache = [&](const auto& cache, const std::string& saveId) {
            std::vector<std::pair<std::string, ScriptAnyValue>> batchItems;
            batchItems.reserve(BATCH_SIZE);
            
            // 将缓存项收集到批处理向量中
            for (const auto& item : cache) {
                batchItems.push_back(item);
                
                // 当批处理大小达到限制时处理这一批
                if (batchItems.size() >= BATCH_SIZE) {
                    // 开始内部事务以优化批量更新/插入
                    db.exec("BEGIN");
                    
                    for (const auto& [key, value] : batchItems) {
                        // 检查记录是否已存在
                        checkExist.bind(1, key);
                        checkExist.bind(2, saveId);
                        
                        bool recordExists = checkExist.executeStep();
                        int64_t recordId = 0;
                        int64_t createdAt = currentTime;
                        
                        if (recordExists) {
                            recordId = checkExist.getColumn(0).getInt64();
                            createdAt = checkExist.getColumn(1).getInt64();
                        }
                        
                        checkExist.reset();
                        
                        // 准备值
                        std::string stringValue;
                        switch (value.type) {
                        case ANY_TBOOLEAN:
                            stringValue = value.b ? "1" : "0";
                            break;
                        case ANY_TNUMBER:
                            stringValue = std::to_string(value.number);
                            break;
                        case ANY_TSTRING:
                            stringValue = value.str ? value.str : "";
                            break;
                        default: continue;
                        }
                        
                        if (recordExists) {
                            // 更新现有记录
                            update.bind(1, value.type);
                            update.bind(2, stringValue);
                            update.bind(3, currentTime); // 更新时间戳
                            update.bind(4, recordId);
                            update.exec();
                            update.reset();
                        } else {
                            // 插入新记录
                            insert.bind(1, key);
                            insert.bind(2, saveId);
                            insert.bind(3, value.type);
                            insert.bind(4, stringValue);
                            insert.bind(5, createdAt); // 创建时间戳
                            insert.bind(6, currentTime); // 更新时间戳
                            insert.exec();
                            insert.reset();
                        }
                        
                        savedCount++;
                    }
                    
                    // 提交内部事务
                    db.exec("COMMIT");
                    batchItems.clear();
                }
            }
            
            // 处理剩余项
            if (!batchItems.empty()) {
                // 开始内部事务以优化批量更新/插入
                db.exec("BEGIN");
                
                for (const auto& [key, value] : batchItems) {
                    // 检查记录是否已存在
                    checkExist.bind(1, key);
                    checkExist.bind(2, saveId);
                    
                    bool recordExists = checkExist.executeStep();
                    int64_t recordId = 0;
                    int64_t createdAt = currentTime;
                    
                    if (recordExists) {
                        recordId = checkExist.getColumn(0).getInt64();
                        createdAt = checkExist.getColumn(1).getInt64();
                    }
                    
                    checkExist.reset();
                    
                    // 准备值
                    std::string stringValue;
                    switch (value.type) {
                    case ANY_TBOOLEAN:
                        stringValue = value.b ? "1" : "0";
                        break;
                    case ANY_TNUMBER:
                        stringValue = std::to_string(value.number);
                        break;
                    case ANY_TSTRING:
                        stringValue = value.str ? value.str : "";
                        break;
                    default: continue;
                    }
                    
                    if (recordExists) {
                        // 更新现有记录
                        update.bind(1, value.type);
                        update.bind(2, stringValue);
                        update.bind(3, currentTime); // 更新时间戳
                        update.bind(4, recordId);
                        update.exec();
                        update.reset();
                    } else {
                        // 插入新记录
                        insert.bind(1, key);
                        insert.bind(2, saveId);
                        insert.bind(3, value.type);
                        insert.bind(4, stringValue);
                        insert.bind(5, createdAt); // 创建时间戳
                        insert.bind(6, currentTime); // 更新时间戳
                        insert.exec();
                        insert.reset();
                    }
                    
                    savedCount++;
                }
                
                // 提交内部事务
                db.exec("COMMIT");
            }
        };

        // 保存全局数据 (空存档ID)
        if (!m_globalCache.empty()) {
            LogInfo("Saving %d global entries...", m_globalCache.size());
            batchSaveCache(m_globalCache, "");
        }

        // 保存存档特定数据
        if (!m_currentSaveGame.empty() && !m_saveCache.empty()) {
            LogInfo("Saving %d save-specific entries...", m_saveCache.size());
            batchSaveCache(m_saveCache, m_currentSaveGame);
        }
    });

        LogInfo("SaveToDB: Saved %d entries to database", savedCount);

        // 重置数据变更标记
        m_dataChanged = false;
    }
    catch (const SQLite::Exception& e)
    {
        // 捕获并记录 SQLite 异常
        LogError("SaveToDB: SQLite error: %s (code: %d)", e.what(), e.getErrorCode());
        throw; // 重新抛出异常，让调用者可以处理
    }
    catch (const std::exception& e)
    {
        // 捕获并记录其他标准异常
        LogError("SaveToDB: Error: %s", e.what());
        throw; // 重新抛出异常，让调用者可以处理
    }
    catch (...)
    {
        // 捕获所有未知异常
        LogError("SaveToDB: Unknown error occurred");
        throw; // 重新抛出异常，让调用者可以处理
    }
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
    SCRIPT_REG_TEMPLFUNC(SetG, "key, value");
    SCRIPT_REG_TEMPLFUNC(GetG, "key");
    SCRIPT_REG_TEMPLFUNC(DeleteG, "key");
    SCRIPT_REG_TEMPLFUNC(ExistsG, "key");

    // 工具方法
    SCRIPT_REG_TEMPLFUNC(Flush, "");
    SCRIPT_REG_TEMPLFUNC(Dump, "");
}
