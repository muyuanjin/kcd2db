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

    // Lua 方法 - 存档关联数据
    int Set(IFunctionHandler* pH);
    int Get(IFunctionHandler* pH);
    int Delete(IFunctionHandler* pH);
    int Exists(IFunctionHandler* pH);
    
    // Lua 方法 - 全局数据（跨存档）
    int SetGlobal(IFunctionHandler* pH);
    int GetGlobal(IFunctionHandler* pH);
    int DeleteGlobal(IFunctionHandler* pH);
    int ExistsGlobal(IFunctionHandler* pH);
    
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

    // 数据变化检测
    bool HasDataChanged() const;
    
private:
    void RegisterMethods();
    void LoadFromDB();
    void SaveToDB();
    void ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const;
    
    // 数据变更标记
    void MarkDataChanged();
    
    std::mutex m_mutex;
    std::unordered_map<std::string, ScriptAnyValue> m_cache; // 内存缓存
    std::unique_ptr<SQLite::Database> m_db;                  // 数据库连接
    std::string m_dbPath;                                    // 数据库路径
    std::string m_currentSaveGame;                           // 当前存档文件路径
    bool m_dataChanged;                                      // 数据是否被修改
};
