// Database.h 简化版本
#pragma once
#include <functional>

#include "../kcd2/IScriptSystem.h"
#include "../kcd2/IGameFramework.h"
#include <unordered_map>
#include <mutex>
#include <optional>
#include <SQLiteCpp/SQLiteCpp.h>


class Database final : public CScriptableBase, public IGameFrameworkListener {
public:
    explicit Database(SSystemGlobalEnvironment* env);
    ~Database() override;

    // Lua API
    int Set(IFunctionHandler* pH)  { return GenericAccess(pH, AccessType::Set); }
    int Get(IFunctionHandler* pH)  { return GenericAccess(pH, AccessType::Get); }
    int Del(IFunctionHandler* pH)  { return GenericAccess(pH, AccessType::Del); }
    int Exi(IFunctionHandler* pH)  { return GenericAccess(pH, AccessType::Exi); }
    int All(IFunctionHandler* pH)  { return GenericAccess(pH, AccessType::All); }
    int SetG(IFunctionHandler* pH) { return GenericAccess(pH, AccessType::Set, true); }
    int GetG(IFunctionHandler* pH) { return GenericAccess(pH, AccessType::Get, true); }
    int DelG(IFunctionHandler* pH) { return GenericAccess(pH, AccessType::Del, true); }
    int ExiG(IFunctionHandler* pH) { return GenericAccess(pH, AccessType::Exi, true); }
    int AllG(IFunctionHandler* pH) { return GenericAccess(pH, AccessType::All, true); }

    int Dump(IFunctionHandler* pH);

    const char* getName() const { return m_sGlobalName; }

    // IGameFrameworkListener
    void OnSaveGame(ISaveGame* pSaveGame) override;
    void OnLoadGame(ILoadGame* pLoadGame) override;
    void OnPostUpdate(float fDeltaTime) override;

    void OnLevelEnd(const char* nextLevel)  override {}
    void OnActionEvent(const SActionEvent& event) override {}
    void OnPreRender() override{}
    void OnSavegameFileLoadedInMemory(const char* pLevelName) override{}
    void OnForceLoadingWithFlash()  override                          {}

private:
    enum class AccessType { Set, Get, Del, Exi, All };
    typedef std::unordered_map<std::string, ScriptAnyValue> Cache;

    struct CacheData {
        Cache cache;
        std::string savefile;
        bool& changedFlag;
    };

    static int BatchOperation(SQLite::Database& db, const Cache& cache, const std::string& savefile);

    int GenericAccess(IFunctionHandler* pH, AccessType action, bool isGlobal = false);
    void SyncCacheWithDatabase();

    static std::optional<ScriptAnyValue> ParseAnyValue(int type, const std::string& value);
    static std::optional<std::string> SerializeAnyValue(const ScriptAnyValue& any);
    void ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const;

    std::unique_ptr<SQLite::Database> m_db;
    std::string m_currentSaveGame;
    mutable std::mutex m_mutex;
    Cache m_saveCache;
    Cache m_globalCache;


    std::chrono::steady_clock::time_point m_lastSaveTime;

    bool m_globalDirty = false;
};
