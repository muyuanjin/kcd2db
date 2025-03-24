//
// Created by muyuanjin on 2025/3/24.
//

#pragma once
#include <functional>

#include "../kcd2/IScriptSystem.h"
#include <unordered_map>
#include <mutex>
#include <SQLiteCpp/SQLiteCpp.h>

class Database final : public CScriptableBase {
public:
    explicit Database(SSystemGlobalEnvironment* env);
    ~Database() override;

    // Lua 方法
    int Set(IFunctionHandler* pH);
    int Get(IFunctionHandler* pH);
    int Delete(IFunctionHandler* pH);
    int Exists(IFunctionHandler* pH);
    int Flush(IFunctionHandler* pH);
    int Dump(IFunctionHandler* pH);
private:
    void RegisterMethods();
    void LoadFromDB();
    void SaveToDB();
    void ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const;
    std::mutex m_mutex;
    std::unordered_map<std::string, ScriptAnyValue> m_cache;
    std::unique_ptr<SQLite::Database> m_db;
    std::string m_dbPath;
};