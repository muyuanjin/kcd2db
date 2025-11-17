// Database.h 简化版本
#pragma once
#include <functional>

#include <cryengine/IScriptSystem.h>
#include <cryengine/IGameFramework.h>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <variant>
#include <SQLiteCpp/SQLiteCpp.h>

class ScriptValue {
public:
    enum class Type { BOOL, NUMBER, STRING };
    // 构造函数
    ScriptValue() : m_value(false) {}  // 默认构造为 bool
    explicit ScriptValue(bool b) : m_value(b) {}
    explicit ScriptValue(float f) : m_value(f) {}
    explicit ScriptValue(const std::string& s) : m_value(s) {}
    explicit ScriptValue(const char* s) : m_value(std::string(s)) {}
    explicit ScriptValue(const ScriptAnyValue& v){
        switch (v.type)
        {
        case ANY_TBOOLEAN:
            m_value = v.b;
            break;
        case ANY_TNUMBER:
            m_value = v.number;
            break;
        case ANY_TSTRING:
            m_value = v.str;
            break;
        default:
            assert(false);
        }
    }

    ~ScriptValue() = default;
    ScriptValue(const ScriptValue&) = default;
    ScriptValue(ScriptValue&&) = default;
    ScriptValue& operator=(const ScriptValue&) = default;
    ScriptValue& operator=(ScriptValue&&) = default;
    // 类型查询
    Type type() const {
        return static_cast<Type>(m_value.index());
    }
    //兼容方法
    int anyType() const
    {
        switch (type())
        {
        case Type::BOOL: return ANY_TBOOLEAN;
        case Type::NUMBER: return ANY_TNUMBER;
        case Type::STRING: return ANY_TSTRING;
        default: return ANY_ANY;
        }
    }
    ScriptAnyValue toAnyValue() const
    {
        switch (type())
        {
        case Type::BOOL:
            return {as_bool()};
        case Type::NUMBER:
            return {as_number()};
        case Type::STRING:
            return {as_string().c_str()};
        }
        return {};
    }

    // 取值接口
    bool as_bool() const {
        assert(is_bool());
        return std::get<bool>(m_value);
    }
    float as_number() const {
        assert(is_number());
        return std::get<float>(m_value);
    }
    const std::string& as_string() const {
        assert(is_string());
        return std::get<std::string>(m_value);
    }
    // 类型校验
    bool is_bool() const   { return type() == Type::BOOL; }
    bool is_number() const { return type() == Type::NUMBER; }
    bool is_string() const { return type() == Type::STRING; }
private:
    std::variant<bool, float, std::string> m_value;
};


class LuaDB final : public CScriptableBase, public IGameFrameworkListener {
public:
    explicit LuaDB();
    ~LuaDB() override;
    void RegisterLuaAPI();
    bool isRegistered() const;

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
    typedef std::unordered_map<std::string, ScriptValue> Cache;

    struct CacheData {
        Cache cache;
        std::string savefile;
        bool& changedFlag;
    };

    int GenericAccess(IFunctionHandler* pH, AccessType action, bool isGlobal = false);

    void ExecuteTransaction(const std::function<void(SQLite::Database&)>& task) const;
    void SyncCacheWithDatabase();

    std::unique_ptr<SQLite::Database> m_db;
    std::string m_currentSaveGame;
    mutable std::mutex m_mutex;
    Cache m_saveCache;
    Cache m_globalCache;


    std::chrono::steady_clock::time_point m_lastSaveTime;

    bool m_globalDirty = false;
};
