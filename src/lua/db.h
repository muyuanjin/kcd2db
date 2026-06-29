//
// Created by muyuanjin on 2025/3/26.
//

#ifndef DB_H
#define DB_H

#ifndef KCD2DB_VERSION
#define KCD2DB_VERSION "dev"
#endif

inline auto db_lua = R"lua(
if type(LuaDB) == "table" then
    LuaDB.__kcd2db_native = true
    LuaDB.__kcd2db_fake = false
    LuaDB.__kcd2db_persistent = true
    LuaDB.__kcd2db_backend = "sqlite"
end

local __kcd2db_existing_meta = _G.KCD2DB
if type(__kcd2db_existing_meta) ~= "table" then
    _G.KCD2DB = {}
end
local __kcd2db_existing_conflicts = _G.KCD2DB.conflicts
if type(__kcd2db_existing_conflicts) ~= "table" then
    _G.KCD2DB.conflicts = {}
    if __kcd2db_existing_conflicts ~= nil then
        _G.KCD2DB.conflicts.KCD2DB_conflicts = __kcd2db_existing_conflicts
    end
else
    _G.KCD2DB.conflicts = __kcd2db_existing_conflicts
end
if __kcd2db_existing_meta ~= nil and type(__kcd2db_existing_meta) ~= "table" then
    _G.KCD2DB.conflicts.KCD2DB = __kcd2db_existing_meta
end
_G.KCD2DB.native = true
_G.KCD2DB.fake = false
_G.KCD2DB.persistent = true
_G.KCD2DB.backend = "sqlite"
_G.KCD2DB.version = ")lua" KCD2DB_VERSION R"lua("

local __kcd2db_existing_db = _G.DB
local __kcd2db_existing_cursor = _G.KCD2DB.Cursor

_G.DB = (function()
    local function log_warning(message)
        if type(System) == "table" and type(System.LogAlways) == "function" then
            pcall(System.LogAlways, "[kcd2db] [WARN] " .. tostring(message) .. "\n")
        end
    end

    local db_api_fields = {
        L = true,
        G = true,
        Set = true,
        Get = true,
        Del = true,
        Exi = true,
        All = true,
        SetG = true,
        GetG = true,
        DelG = true,
        ExiG = true,
        AllG = true,
        Dump = true,
        Create = true
    }

    local function record_db_field_conflict(key, value)
        if type(_G.KCD2DB.conflicts) ~= "table" then
            local previous_conflicts = _G.KCD2DB.conflicts
            _G.KCD2DB.conflicts = {}
            if previous_conflicts ~= nil then
                _G.KCD2DB.conflicts.KCD2DB_conflicts = previous_conflicts
            end
        end
        if type(_G.KCD2DB.conflicts.DB_fields) ~= "table" then
            local previous_fields = _G.KCD2DB.conflicts.DB_fields
            _G.KCD2DB.conflicts.DB_fields = {}
            if previous_fields ~= nil then
                _G.KCD2DB.conflicts.DB_fields.DB_fields = previous_fields
            end
        end
        _G.KCD2DB.conflicts.DB_fields[key] = value
    end

    -- 检查 json 是否可用
    local _json_available
    local function json_available()
        if _json_available == nil then
            _json_available = type(json) == "table" and type(json.encode) == "function" and type(json.decode) == "function"
            if not _json_available then
                if type(Script) == "table" and type(Script.LoadScript) == "function" then
                    local loaded, err = pcall(Script.LoadScript, "Scripts/Utils/JSON/json.lua")
                    if not loaded then
                        log_warning("Failed to load json.lua: " .. tostring(err))
                    end
                else
                    log_warning("Script.LoadScript is unavailable; DB table values will not be JSON encoded.")
                end
                _json_available = type(json) == "table" and type(json.encode) == "function" and type(json.decode) == "function"
            end
        end
        return _json_available
    end

    local function encode_value(value, context)
        if json_available() then
            local success, result = pcall(json.encode, value)
            if success then
                return result, true
            end
            log_warning("json.encode failed"
                    .. (context and (" in " .. tostring(context)) or "")
                    .. ": value_type=" .. type(value)
                    .. ", error=" .. tostring(result))
            return nil, false
        end
        log_warning("json.lua is unavailable; DB wrapper cannot encode "
                .. type(value)
                .. (context and (" in " .. tostring(context)) or ""))
        return nil, false
    end

    local function decode_value(value, context)
        if json_available() and type(value) == "string" then
            local success, result = pcall(json.decode, value)
            if success then
                return result
            end
        end
        return value
    end

    -- 主模块表
    local M = {
        L = {},
        G = {}
    }

    if type(__kcd2db_existing_db) == "table" then
        for key, value in pairs(__kcd2db_existing_db) do
            if db_api_fields[key] then
                record_db_field_conflict(key, value)
                log_warning("Overwrote existing DB." .. tostring(key) .. " field.")
            else
                M[key] = value
            end
        end
    elseif __kcd2db_existing_db ~= nil then
        _G.KCD2DB.conflicts.DB = __kcd2db_existing_db
        log_warning("Replaced non-table global DB value.")
    end

    -- 通用包装函数，处理点调用和冒号调用
    local function wrap(func, param_count, ins)
        return function(...)
            local args = { ... }
            local is_method_call = #args >= 1 and args[1] == ins
            local call_args = {}

            if is_method_call then
                for i = 1, param_count do
                    call_args[i] = args[i + 1]
                end
            else
                for i = 1, param_count do
                    call_args[i] = args[i]
                end
            end

            return func(unpack(call_args, 1, param_count))
        end
    end

    local function install_cursor_api()
        local cursor = __kcd2db_existing_cursor
        if type(cursor) ~= "table" then
            cursor = {}
            if __kcd2db_existing_cursor ~= nil then
                _G.KCD2DB.conflicts.Cursor = __kcd2db_existing_cursor
                log_warning("Replaced non-table KCD2DB.Cursor value.")
            end
        end

        if type(_G.KCD2DB.conflicts.Cursor_fields) ~= "table" then
            _G.KCD2DB.conflicts.Cursor_fields = {}
        end

        local function set_cursor_field(key, value)
            if cursor[key] ~= nil then
                _G.KCD2DB.conflicts.Cursor_fields[key] = cursor[key]
                log_warning("Overwrote existing KCD2DB.Cursor." .. tostring(key) .. " field.")
            end
            cursor[key] = value
        end

        local function has_native_cursor_api(name)
            return type(LuaDB) == "table" and type(LuaDB[name]) == "function"
        end

        local function declareImpl(path)
            if has_native_cursor_api("CursorDeclare") then
                return LuaDB.CursorDeclare(path)
            end
            return false
        end

        local function lockImpl(path)
            if has_native_cursor_api("CursorLock") then
                return LuaDB.CursorLock(path)
            end
            return false
        end

        local function unlockImpl()
            if has_native_cursor_api("CursorUnlock") then
                return LuaDB.CursorUnlock()
            end
            return false
        end

        set_cursor_field("Declare", wrap(declareImpl, 1, cursor))
        set_cursor_field("Lock", wrap(lockImpl, 1, cursor))
        set_cursor_field("Unlock", wrap(unlockImpl, 0, cursor))
        _G.KCD2DB.Cursor = cursor
    end
    install_cursor_api()

    local function setImpl(key, value)
        local encoded, ok = encode_value(value, "DB.Set key=" .. tostring(key))
        if not ok then
            return false
        end
        return LuaDB.Set(key, encoded)
    end
    M.Set = wrap(setImpl, 2, M)

    local function getImpl(key)
        return decode_value(LuaDB.Get(key), "DB.Get key=" .. tostring(key))
    end
    M.Get = wrap(getImpl, 1, M)

    local function delImpl(key)
        return LuaDB.Del(key)
    end
    M.Del = wrap(delImpl, 1, M)

    local function exiImpl(key)
        return LuaDB.Exi(key)
    end
    M.Exi = wrap(exiImpl, 1, M)

    local function allImpl()
        local result = LuaDB.All() or {}
        if type(result) ~= "table" then
            log_warning("LuaDB.All returned " .. type(result) .. "; returning an empty table.")
            return {}
        end
        for k, v in pairs(result) do
            result[k] = decode_value(v, "DB.All key=" .. tostring(k))
        end
        return result
    end
    M.All = wrap(allImpl, 0, M)

    local function setGImpl(key, value)
        local encoded, ok = encode_value(value, "DB.SetG key=" .. tostring(key))
        if not ok then
            return false
        end
        return LuaDB.SetG(key, encoded)
    end
    M.SetG = wrap(setGImpl, 2, M)

    local function getGImpl(key)
        return decode_value(LuaDB.GetG(key), "DB.GetG key=" .. tostring(key))
    end
    M.GetG = wrap(getGImpl, 1, M)

    local function delGImpl(key)
        return LuaDB.DelG(key)
    end
    M.DelG = wrap(delGImpl, 1, M)

    local function exiGImpl(key)
        return LuaDB.ExiG(key)
    end
    M.ExiG = wrap(exiGImpl, 1, M)

    local function allGImpl()
        local result = LuaDB.AllG() or {}
        if type(result) ~= "table" then
            log_warning("LuaDB.AllG returned " .. type(result) .. "; returning an empty table.")
            return {}
        end
        for k, v in pairs(result) do
            result[k] = decode_value(v, "DB.AllG key=" .. tostring(k))
        end
        return result
    end
    M.AllG = wrap(allGImpl, 0, M)

    local function dumpImpl()
        return LuaDB.Dump()
    end
    M.Dump = wrap(dumpImpl, 0, M)

)lua" R"lua(
    local function createMetatable(opts)
        local function warn_failed_assignment(key)
            log_warning("Assignment failed for " .. tostring(opts.assignmentName)
                    .. " key=" .. tostring(key)
                    .. "; value was not stored.")
        end

        return {
            __index = function(_, key)
                return opts.getFunc(key)
            end,
            __newindex = function(_, key, value)
                if value == nil then
                    opts.delFunc(key)
                else
                    local ok = opts.setFunc(key, value)
                    if ok == false then
                        warn_failed_assignment(key)
                    end
                end
            end,
            __tostring = function()
                return opts.toStringFunc()
            end
        }
    end

    setmetatable(M.L, createMetatable({
        assignmentName = "DB.L",
        getFunc = function(k)
            return M.Get(k)
        end,
        setFunc = function(k, v)
            return M.Set(k, v)
        end,
        delFunc = function(k)
            M.Del(k)
        end,
        toStringFunc = function()
            return tostring(M) .. ":L"
        end
    }))

    setmetatable(M.G, createMetatable({
        assignmentName = "DB.G",
        getFunc = function(k)
            return M.GetG(k)
        end,
        setFunc = function(k, v)
            return M.SetG(k, v)
        end,
        delFunc = function(k)
            M.DelG(k)
        end,
        toStringFunc = function()
            return tostring(M) .. ":G"
        end
    }))

    -- 创建带命名空间的 DB 实例
    local function Create(namespace)
        assert(type(namespace) == "string" and #namespace > 0,
                "Namespace must be a non-empty string")
        -- 如果namespace包含冒号则失败
        if namespace:find(":") then
            error("Namespace cannot contain colon")
        end

        -- 确保命名空间以冒号结尾，作为分隔符
        if not namespace:match(":$") then
            namespace = namespace .. ":"
        end

        local instance = {
            L = {},
            G = {}
        }

        -- 内部方法：添加命名空间前缀
        local function prefix_key(key)
            if type(key) ~= "string" then
                local encoded, ok = encode_value(key, "DB.Create prefix key")
                if ok and type(encoded) == "string" then
                    key = encoded
                else
                    key = tostring(key)
                end
            end
            return namespace .. key
        end

        -- Define methods with parameter counts
        local function _setImpl(key, value)
            local prefixed_key = prefix_key(key)
            local encoded, ok = encode_value(value, "DB instance Set key=" .. tostring(prefixed_key))
            if not ok then
                return false
            end
            return LuaDB.Set(prefixed_key, encoded)
        end
        instance.Set = wrap(_setImpl, 2, instance)

        local function _getImpl(key)
            local prefixed_key = prefix_key(key)
            return decode_value(LuaDB.Get(prefixed_key), "DB instance Get key=" .. tostring(prefixed_key))
        end
        instance.Get = wrap(_getImpl, 1, instance)

        local function _delImpl(key)
            return LuaDB.Del(prefix_key(key))
        end
        instance.Del = wrap(_delImpl, 1, instance)

        local function _exiImpl(key)
            return LuaDB.Exi(prefix_key(key))
        end
        instance.Exi = wrap(_exiImpl, 1, instance)

        local function _allImpl()
            local result = LuaDB.All() or {}
            if type(result) ~= "table" then
                log_warning("LuaDB.All returned " .. type(result) .. "; returning an empty table.")
                return {}
            end
            local output = {}
            for k, v in pairs(result) do
                -- 只返回命名空间前缀的键
                if type(k) == "string" and k:sub(1, #namespace) == namespace then
                    local raw_key = k:sub(#namespace + 1)
                    output[raw_key] = decode_value(v, "DB instance All key=" .. tostring(k))
                end
            end
            return output
        end
        instance.All = wrap(_allImpl, 0, instance)

        local function _setGImpl(key, value)
            local prefixed_key = prefix_key(key)
            local encoded, ok = encode_value(value, "DB instance SetG key=" .. tostring(prefixed_key))
            if not ok then
                return false
            end
            return LuaDB.SetG(prefixed_key, encoded)
        end
        instance.SetG = wrap(_setGImpl, 2, instance)

        local function _getGImpl(key)
            local prefixed_key = prefix_key(key)
            return decode_value(LuaDB.GetG(prefixed_key), "DB instance GetG key=" .. tostring(prefixed_key))
        end
        instance.GetG = wrap(_getGImpl, 1, instance)

        local function _delGImpl(key)
            return LuaDB.DelG(prefix_key(key))
        end
        instance.DelG = wrap(_delGImpl, 1, instance)

        local function _exiGImpl(key)
            return LuaDB.ExiG(prefix_key(key))
        end
        instance.ExiG = wrap(_exiGImpl, 1, instance)

        local function _allGImpl()
            local result = LuaDB.AllG() or {}
            if type(result) ~= "table" then
                log_warning("LuaDB.AllG returned " .. type(result) .. "; returning an empty table.")
                return {}
            end
            local output = {}
            for k, v in pairs(result) do
                -- 只返回命名空间前缀的键
                if type(k) == "string" and k:sub(1, #namespace) == namespace then
                    local raw_key = k:sub(#namespace + 1)
                    output[raw_key] = decode_value(v, "DB instance AllG key=" .. tostring(k))
                end
            end
            return output
        end
        instance.AllG = wrap(_allGImpl, 0, instance)

        local function _dumpImpl()
            System.LogAlways("$6--- [Global Data For: " .. namespace:sub(1, -2) .. "] ---\n")
            local globalResult = instance.AllG()
            for k, v in pairs(globalResult) do
                System.LogAlways("  $5" .. k .. "  $8" .. type(v) .. "  $3" .. tostring((encode_value(v, "DB instance Dump global key=" .. tostring(k)))) .. "\n")
            end
            -- 修复此处：从插入表格改为插入字符串
            System.LogAlways("$6--- [Saved Data For: " .. namespace:sub(1, -2) .. "] ---\n")
            local localResult = instance.All()
            for k, v in pairs(localResult) do
                System.LogAlways("  $5" .. k .. "  $8" .. type(v) .. "  $3" .. tostring((encode_value(v, "DB instance Dump local key=" .. tostring(k)))) .. "\n")
            end
        end
        instance.Dump = wrap(_dumpImpl, 0, instance)

        -- ------------- 批量设置子表的元表----------------
        -- 本地操作（L 子表）
        setmetatable(instance.L, createMetatable({
            assignmentName = "DB(" .. namespace:sub(1, -2) .. ").L",
            getFunc = function(k)
                return instance.Get(k)
            end,
            setFunc = function(k, v)
                return instance.Set(k, v)
            end,
            delFunc = function(k)
                instance.Del(k)
            end,
            toStringFunc = function()
                return tostring(instance) .. ":L"
            end
        }))
        -- 全局操作（G 子表）
        setmetatable(instance.G, createMetatable({
            assignmentName = "DB(" .. namespace:sub(1, -2) .. ").G",
            getFunc = function(k)
                return instance.GetG(k)
            end,
            setFunc = function(k, v)
                return instance.SetG(k, v)
            end,
            delFunc = function(k)
                instance.DelG(k)
            end,
            toStringFunc = function()
                return tostring(instance) .. ":G"
            end
        }))
        -- 主表的元表（额外需要处理方法覆盖保护）
        setmetatable(instance, {
            __index = function(t, key)
                local method = rawget(t, key)
                return method or instance.Get(key)
            end,
            __newindex = function(t, key, value)
                -- 原始保护逻辑无法被通用函数简化（需要保留）
                if rawget(t, key) ~= nil then
                    error("Cannot override method: " .. key)
                end
                -- 默认仍视为本地操作
                if value == nil then
                    instance.Del(key)
                else
                    local ok = instance.Set(key, value)
                    if ok == false then
                        log_warning("Assignment failed for DB(" .. namespace:sub(1, -2) .. ") key="
                                .. tostring(key)
                                .. "; value was not stored.")
                    end
                end
            end,
            __tostring = function()
                return "DB Instance: " .. namespace:sub(1, -2)
            end
        })

        return instance
    end
    M.Create = wrap(Create, 1, M)

    setmetatable(M, {
        __index = function(t, key)
            local method = rawget(t, key)
            return method or M.Get(key)
        end,
        __newindex = function(t, key, value)
            if rawget(t, key) ~= nil then
                error("Cannot override method: " .. key)
            end
            if value == nil then
                M.Del(key)
            else
                local ok = M.Set(key, value)
                if ok == false then
                    log_warning("Assignment failed for DB key=" .. tostring(key) .. "; value was not stored.")
                end
            end
        end,
        __call = function(_, namespace)
            return M.Create(namespace)
        end,
        __tostring = function()
            return "DB"
        end
    })

    return M
end)()
)lua";

#endif //DB_H
