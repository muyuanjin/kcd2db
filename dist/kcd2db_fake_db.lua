local KCD2DB_FAKE_DB_VERSION = "1"

local raw_methods = {
    "Set", "Get", "Del", "Exi", "All",
    "SetG", "GetG", "DelG", "ExiG", "AllG",
    "Dump"
}

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

local function log(level, message)
    local line = "[kcd2db_fake_db][" .. level .. "] " .. message
    if type(System) == "table" and type(System.LogAlways) == "function" then
        System.LogAlways(line)
    elseif type(print) == "function" then
        print(line)
    end
end

local function ensure_meta()
    if type(_G.KCD2DB) ~= "table" then
        local previous = _G.KCD2DB
        _G.KCD2DB = { conflicts = {} }
        if previous ~= nil then
            _G.KCD2DB.conflicts.KCD2DB = previous
            log("WARN", "Replaced non-table global KCD2DB value.")
        end
    end

    if type(_G.KCD2DB.conflicts) ~= "table" then
        local previous_conflicts = _G.KCD2DB.conflicts
        _G.KCD2DB.conflicts = {}
        if previous_conflicts ~= nil then
            _G.KCD2DB.conflicts.KCD2DB_conflicts = previous_conflicts
            log("WARN", "Replaced non-table KCD2DB.conflicts value.")
        end
    end
    return _G.KCD2DB
end

local meta = ensure_meta()
local already_loaded = meta.fake_db_loaded == KCD2DB_FAKE_DB_VERSION

local function record_conflict(name, value)
    if type(meta.conflicts) ~= "table" then
        local previous_conflicts = meta.conflicts
        meta.conflicts = {}
        if previous_conflicts ~= nil then
            meta.conflicts.KCD2DB_conflicts = previous_conflicts
        end
    end
    meta.conflicts[name] = value
end

local function record_db_field_conflict(name, value)
    if type(meta.conflicts) ~= "table" then
        record_conflict("KCD2DB_conflicts", meta.conflicts)
    end
    if type(meta.conflicts.DB_fields) ~= "table" then
        local previous_fields = meta.conflicts.DB_fields
        meta.conflicts.DB_fields = {}
        if previous_fields ~= nil then
            meta.conflicts.DB_fields.DB_fields = previous_fields
        end
    end
    meta.conflicts.DB_fields[name] = value
end

local function has_complete_luadb()
    if type(_G.LuaDB) ~= "table" then
        return false
    end

    for _, name in ipairs(raw_methods) do
        if type(_G.LuaDB[name]) ~= "function" then
            return false
        end
    end

    return true
end

local function make_fake_luadb()
    local local_store = {}
    local global_store = {}

    local function valid_key(key)
        if type(key) == "string" then
            return true
        end
        log("WARN", "LuaDB key must be a string; got " .. type(key) .. ".")
        return false
    end

    local function valid_value(value)
        local value_type = type(value)
        if value_type == "boolean" or value_type == "number" or value_type == "string" then
            return true
        end
        log("WARN", "Raw LuaDB value must be boolean, number, or string; got " .. value_type .. ".")
        return false
    end

    local function set_value(store, key, value)
        if not valid_key(key) or not valid_value(value) then
            return false
        end
        store[key] = value
        return true
    end

    local function get_value(store, key)
        if not valid_key(key) then
            return nil
        end
        return store[key]
    end

    local function del_value(store, key)
        if not valid_key(key) then
            return false
        end
        local existed = store[key] ~= nil
        store[key] = nil
        return existed
    end

    local function exi_value(store, key)
        if not valid_key(key) then
            return false
        end
        return store[key] ~= nil
    end

    local function all_values(store)
        local result = {}
        for key, value in pairs(store) do
            result[key] = value
        end
        return result
    end

    local function dump_values()
        log("INFO", "--- [Global Data] ---")
        for key, value in pairs(global_store) do
            log("INFO", tostring(key) .. "  " .. type(value) .. "  " .. tostring(value))
        end
        log("INFO", "--- [Saved Data] ---")
        for key, value in pairs(local_store) do
            log("INFO", tostring(key) .. "  " .. type(value) .. "  " .. tostring(value))
        end
    end

    return {
        __kcd2db_native = false,
        __kcd2db_fake = true,
        __kcd2db_persistent = false,
        __kcd2db_backend = "memory",

        Set = function(key, value)
            return set_value(local_store, key, value)
        end,
        Get = function(key)
            return get_value(local_store, key)
        end,
        Del = function(key)
            return del_value(local_store, key)
        end,
        Exi = function(key)
            return exi_value(local_store, key)
        end,
        All = function()
            return all_values(local_store)
        end,

        SetG = function(key, value)
            return set_value(global_store, key, value)
        end,
        GetG = function(key)
            return get_value(global_store, key)
        end,
        DelG = function(key)
            return del_value(global_store, key)
        end,
        ExiG = function(key)
            return exi_value(global_store, key)
        end,
        AllG = function()
            return all_values(global_store)
        end,

        Dump = dump_values
    }
end

local function json_available()
    if type(json) == "table" and type(json.encode) == "function" and type(json.decode) == "function" then
        return true
    end

    if type(Script) == "table" and type(Script.LoadScript) == "function" then
        pcall(Script.LoadScript, "Scripts/Utils/JSON/json.lua")
    end

    return type(json) == "table" and type(json.encode) == "function" and type(json.decode) == "function"
end

local function encode_value(value)
    if json_available() then
        local success, result = pcall(json.encode, value)
        if success then
            return result, true
        end
        log("WARN", "json.encode failed for " .. type(value) .. " value: " .. tostring(result))
        return nil, false
    end

    log("WARN", "json.lua is unavailable; DB wrapper cannot encode " .. type(value) .. " values.")
    return nil, false
end

local function decode_value(value)
    if json_available() and type(value) == "string" then
        local success, result = pcall(json.decode, value)
        if success then
            return result
        end
    end
    return value
end

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

local function create_metatable(opts)
    return {
        __index = function(_, key)
            return opts.getFunc(key)
        end,
        __newindex = function(_, key, value)
            if value == nil then
                opts.delFunc(key)
            else
                opts.setFunc(key, value)
            end
        end,
        __tostring = function()
            return opts.toStringFunc()
        end
    }
end

local function make_db(existing_db)
    local M = {
        L = {},
        G = {}
    }

    if type(existing_db) == "table" then
        for key, value in pairs(existing_db) do
            if db_api_fields[key] then
                record_db_field_conflict(key, value)
                log("WARN", "Overwrote existing DB." .. tostring(key) .. " field.")
            else
                M[key] = value
            end
        end
    end

    local function setImpl(key, value)
        local encoded, ok = encode_value(value)
        if not ok then
            return false
        end
        return LuaDB.Set(key, encoded)
    end
    M.Set = wrap(setImpl, 2, M)

    local function getImpl(key)
        return decode_value(LuaDB.Get(key))
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
        for key, value in pairs(result) do
            result[key] = decode_value(value)
        end
        return result
    end
    M.All = wrap(allImpl, 0, M)

    local function setGImpl(key, value)
        local encoded, ok = encode_value(value)
        if not ok then
            return false
        end
        return LuaDB.SetG(key, encoded)
    end
    M.SetG = wrap(setGImpl, 2, M)

    local function getGImpl(key)
        return decode_value(LuaDB.GetG(key))
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
        for key, value in pairs(result) do
            result[key] = decode_value(value)
        end
        return result
    end
    M.AllG = wrap(allGImpl, 0, M)

    local function dumpImpl()
        return LuaDB.Dump()
    end
    M.Dump = wrap(dumpImpl, 0, M)

    setmetatable(M.L, create_metatable({
        getFunc = function(key)
            return M.Get(key)
        end,
        setFunc = function(key, value)
            M.Set(key, value)
        end,
        delFunc = function(key)
            M.Del(key)
        end,
        toStringFunc = function()
            return tostring(M) .. ":L"
        end
    }))

    setmetatable(M.G, create_metatable({
        getFunc = function(key)
            return M.GetG(key)
        end,
        setFunc = function(key, value)
            M.SetG(key, value)
        end,
        delFunc = function(key)
            M.DelG(key)
        end,
        toStringFunc = function()
            return tostring(M) .. ":G"
        end
    }))

    local function Create(namespace)
        assert(type(namespace) == "string" and #namespace > 0, "Namespace must be a non-empty string")
        if namespace:find(":") then
            error("Namespace cannot contain colon")
        end

        if not namespace:match(":$") then
            namespace = namespace .. ":"
        end

        local instance = {
            L = {},
            G = {}
        }

        local function prefix_key(key)
            if type(key) ~= "string" then
                local encoded, ok = encode_value(key)
                if ok and type(encoded) == "string" then
                    key = encoded
                else
                    key = tostring(key)
                end
            end
            return namespace .. key
        end

        local function _setImpl(key, value)
            local encoded, ok = encode_value(value)
            if not ok then
                return false
            end
            return LuaDB.Set(prefix_key(key), encoded)
        end
        instance.Set = wrap(_setImpl, 2, instance)

        local function _getImpl(key)
            return decode_value(LuaDB.Get(prefix_key(key)))
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
            local output = {}
            for key, value in pairs(result) do
                if type(key) == "string" and key:sub(1, #namespace) == namespace then
                    output[key:sub(#namespace + 1)] = decode_value(value)
                end
            end
            return output
        end
        instance.All = wrap(_allImpl, 0, instance)

        local function _setGImpl(key, value)
            local encoded, ok = encode_value(value)
            if not ok then
                return false
            end
            return LuaDB.SetG(prefix_key(key), encoded)
        end
        instance.SetG = wrap(_setGImpl, 2, instance)

        local function _getGImpl(key)
            return decode_value(LuaDB.GetG(prefix_key(key)))
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
            local output = {}
            for key, value in pairs(result) do
                if type(key) == "string" and key:sub(1, #namespace) == namespace then
                    output[key:sub(#namespace + 1)] = decode_value(value)
                end
            end
            return output
        end
        instance.AllG = wrap(_allGImpl, 0, instance)

        local function _dumpImpl()
            log("INFO", "--- [Global Data For: " .. namespace:sub(1, -2) .. "] ---")
            local globalResult = instance.AllG()
            for key, value in pairs(globalResult) do
                local encoded = encode_value(value)
                log("INFO", tostring(key) .. "  " .. type(value) .. "  " .. tostring(encoded))
            end
            log("INFO", "--- [Saved Data For: " .. namespace:sub(1, -2) .. "] ---")
            local localResult = instance.All()
            for key, value in pairs(localResult) do
                local encoded = encode_value(value)
                log("INFO", tostring(key) .. "  " .. type(value) .. "  " .. tostring(encoded))
            end
        end
        instance.Dump = wrap(_dumpImpl, 0, instance)

        setmetatable(instance.L, create_metatable({
            getFunc = function(key)
                return instance.Get(key)
            end,
            setFunc = function(key, value)
                instance.Set(key, value)
            end,
            delFunc = function(key)
                instance.Del(key)
            end,
            toStringFunc = function()
                return tostring(instance) .. ":L"
            end
        }))

        setmetatable(instance.G, create_metatable({
            getFunc = function(key)
                return instance.GetG(key)
            end,
            setFunc = function(key, value)
                instance.SetG(key, value)
            end,
            delFunc = function(key)
                instance.DelG(key)
            end,
            toStringFunc = function()
                return tostring(instance) .. ":G"
            end
        }))

        setmetatable(instance, {
            __index = function(t, key)
                local method = rawget(t, key)
                return method or instance.Get(key)
            end,
            __newindex = function(t, key, value)
                if rawget(t, key) ~= nil then
                    error("Cannot override method: " .. key)
                end
                if value == nil then
                    instance.Del(key)
                else
                    instance.Set(key, value)
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
                M.Set(key, value)
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
end

local function db_create_works()
    if type(_G.DB) ~= "table" or type(_G.DB.Create) ~= "function" then
        return false
    end

    local function instance_shape_works(candidate)
        if type(candidate) ~= "table" then
            return false
        end
        for key in pairs(db_api_fields) do
            if key == "L" or key == "G" then
                if type(candidate[key]) ~= "table" then
                    return false
                end
            elseif key ~= "Create" and type(candidate[key]) ~= "function" then
                return false
            end
        end
        return true
    end

    for key in pairs(db_api_fields) do
        if key == "L" or key == "G" then
            if type(_G.DB[key]) ~= "table" then
                return false
            end
        elseif type(_G.DB[key]) ~= "function" then
            return false
        end
    end

    local suffix = tostring({}):gsub("[^%w_]", "_")
    local probe_namespace = "__kcd2db_fake_db_probe_" .. suffix
    local call_namespace = "__kcd2db_fake_db_probe_call_" .. suffix

    local ok, instance = pcall(_G.DB.Create, probe_namespace)
    if not ok or type(instance) ~= "table" then
        return false
    end

    local call_ok, call_instance = pcall(_G.DB, call_namespace)
    if not call_ok or type(call_instance) ~= "table" then
        return false
    end

    if not instance_shape_works(instance) or not instance_shape_works(call_instance) then
        return false
    end

    ok = pcall(function()
        local local_key = "__probe_local_" .. suffix
        local local_colon_key = "__probe_local_colon_" .. suffix
        local instance_l_key = "__probe_instance_l_" .. suffix
        local instance_g_key = "__probe_instance_g_" .. suffix
        local call_key = "__probe_call_" .. suffix
        local call_colon_key = "__probe_call_colon_" .. suffix
        local global_key = "__probe_global_" .. suffix
        local global_colon_key = "__probe_global_colon_" .. suffix
        local db_l_key = "__kcd2db_fake_db_probe_db_l_" .. suffix
        local db_g_key = "__kcd2db_fake_db_probe_db_g_" .. suffix
        local nil_key = "__probe_nil_" .. suffix
        local table_key = "__probe_table_" .. suffix

        assert(instance.Set(local_key, true) ~= false, "local Set failed")
        assert(instance.Exi(local_key) == true, "local Exi failed")
        assert(instance.Get(local_key) == true, "local Get failed")

        assert(instance:Set(local_colon_key, 12) ~= false, "local colon Set failed")
        assert(instance:Get(local_colon_key) == 12, "local colon Get failed")

        local all_local = instance.All()
        assert(type(all_local) == "table", "All did not return a table")
        assert(all_local[local_key] == true, "All missing local key")
        assert(all_local[local_colon_key] == 12, "All missing colon local key")

        instance.L[instance_l_key] = "instance-local-shortcut"
        assert(instance.L[instance_l_key] == "instance-local-shortcut", "instance L shortcut failed")
        instance.G[instance_g_key] = "instance-global-shortcut"
        assert(instance.G[instance_g_key] == "instance-global-shortcut", "instance G shortcut failed")

        _G.DB.L[db_l_key] = "db-local-shortcut"
        assert(_G.DB.L[db_l_key] == "db-local-shortcut", "DB.L shortcut failed")
        _G.DB.G[db_g_key] = "db-global-shortcut"
        assert(_G.DB.G[db_g_key] == "db-global-shortcut", "DB.G shortcut failed")

        assert(instance.SetG(global_key, "global") ~= false, "global SetG failed")
        assert(instance.ExiG(global_key) == true, "global ExiG failed")
        assert(instance.GetG(global_key) == "global", "global GetG failed")

        assert(instance:SetG(global_colon_key, false) ~= false, "global colon SetG failed")
        assert(instance:GetG(global_colon_key) == false, "global colon GetG failed")

        local all_global = instance.AllG()
        assert(type(all_global) == "table", "AllG did not return a table")
        assert(all_global[global_key] == "global", "AllG missing global key")
        assert(all_global[global_colon_key] == false, "AllG missing colon global key")

        assert(call_instance.Set(call_key, "call-local") ~= false, "DB call instance Set failed")
        assert(call_instance.Get(call_key) == "call-local", "DB call instance Get failed")
        assert(call_instance:Set(call_colon_key, "call-colon") ~= false, "DB call instance colon Set failed")
        assert(call_instance:Get(call_colon_key) == "call-colon", "DB call instance colon Get failed")
        call_instance.L[call_key] = "call-instance-l"
        assert(call_instance.L[call_key] == "call-instance-l", "DB call instance L shortcut failed")
        call_instance.G[call_colon_key] = "call-instance-g"
        assert(call_instance.G[call_colon_key] == "call-instance-g", "DB call instance G shortcut failed")

        if json_available() then
            assert(instance.Set(table_key, { nested = { value = 7 } }) ~= false, "table Set failed")
            local table_value = instance.Get(table_key)
            assert(type(table_value) == "table", "table Get did not decode table")
            assert(type(table_value.nested) == "table", "table Get did not decode nested table")
            assert(table_value.nested.value == 7, "table Get returned wrong nested value")

            assert(instance.Set(nil_key, nil) ~= false, "nil Set failed")
            assert(instance.Exi(nil_key) == true, "nil Exi failed")
            assert(instance.Get(nil_key) == nil, "nil Get failed")
        end

        assert(instance.Del(local_key) == true, "local Del failed")
        assert(instance.Del(local_colon_key) == true, "local colon Del failed")
        assert(instance.DelG(global_key) == true, "global DelG failed")
        assert(instance.DelG(global_colon_key) == true, "global colon DelG failed")
        instance.L[instance_l_key] = nil
        instance.G[instance_g_key] = nil
        _G.DB.L[db_l_key] = nil
        _G.DB.G[db_g_key] = nil
        call_instance.Del(call_key)
        call_instance.Del(call_colon_key)
        call_instance.DelG(call_colon_key)
        instance.Del(nil_key)
        instance.Del(table_key)
    end)

    pcall(function()
        instance.Del("__probe_local_" .. suffix)
        instance.Del("__probe_local_colon_" .. suffix)
        instance.Del("__probe_nil_" .. suffix)
        instance.Del("__probe_table_" .. suffix)
        instance.DelG("__probe_global_" .. suffix)
        instance.DelG("__probe_global_colon_" .. suffix)
        instance.Del("__probe_instance_l_" .. suffix)
        instance.DelG("__probe_instance_g_" .. suffix)
        call_instance.Del("__probe_call_" .. suffix)
        call_instance.Del("__probe_call_colon_" .. suffix)
        call_instance.DelG("__probe_call_colon_" .. suffix)
        _G.DB.Del("__kcd2db_fake_db_probe_db_l_" .. suffix)
        _G.DB.DelG("__kcd2db_fake_db_probe_db_g_" .. suffix)
    end)

    return ok
end

local raw_complete = has_complete_luadb()
local raw_is_fake = raw_complete and _G.LuaDB.__kcd2db_fake == true
local installed_fake = false

if not raw_complete then
    if _G.LuaDB ~= nil then
        record_conflict("LuaDB", _G.LuaDB)
        log("WARN", "Replaced incomplete global LuaDB value with session-only fake backend.")
    else
        log("WARN", "LuaDB native backend is unavailable; using session-only fake backend.")
    end
    _G.LuaDB = make_fake_luadb()
    raw_complete = true
    raw_is_fake = true
    installed_fake = true
end

local needs_db_wrapper = not db_create_works()
if needs_db_wrapper then
    local existing_db = nil
    if type(_G.DB) == "table" then
        existing_db = _G.DB
    elseif _G.DB ~= nil then
        record_conflict("DB", _G.DB)
        log("WARN", "Replaced non-table global DB value.")
    end

    _G.DB = make_db(existing_db)
    log("INFO", "Installed DB wrapper.")
end

local using_native = raw_complete and not raw_is_fake
meta.native = using_native
meta.fake = not using_native
meta.persistent = using_native and (_G.LuaDB.__kcd2db_persistent ~= false) or false
meta.backend = using_native and (_G.LuaDB.__kcd2db_backend or "native-supported") or "memory"
meta.version = KCD2DB_FAKE_DB_VERSION
meta.fake_db_loaded = KCD2DB_FAKE_DB_VERSION

if not already_loaded or installed_fake or needs_db_wrapper then
    if using_native then
        log("INFO", "Using LuaDB backend: " .. meta.backend .. ".")
    else
        log("WARN", "Using session-only fake LuaDB backend; data is memory-only and has no save-switch isolation.")
    end
end
