//
// Created by muyuanjin on 2025/3/24.
//

#pragma once
#include <functional>

#include "../kcd2/IScriptSystem.h"
#include "../kcd2/IGameFramework.h"
#include <unordered_map>
#include <mutex>
#include <SQLiteCpp/SQLiteCpp.h>

class Database final : public CScriptableBase , public IGameFrameworkListener {
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
    // IGameFrameworkListener
    void OnPostUpdate(float fDeltaTime) override {}
    void OnSaveGame(ISaveGame* pSaveGame) override;
    void OnLoadGame(ILoadGame* pLoadGame) override;
    void OnLevelEnd(const char* nextLevel)  override {}
    void OnActionEvent(const SActionEvent& event) override {}
    void OnPreRender() override{}
    void OnSavegameFileLoadedInMemory(const char* pLevelName) override{}
    void OnForceLoadingWithFlash()  override                          {}

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