//
// Created by muyuanjin on 2025/3/26.
//

#ifndef DB_H
#define DB_H

inline auto db_lua = R"lua(
_G.DB = _G.DB or (function()
    -- 检查 json 是否可用
    local _json_available = false

    local json_available = function()
        if _json_available == false then
            _json_available = type(json) == "table" and type(json.encode) == "function" and type(json.decode) == "function"
        end
        return _json_available
    end

    local function encode_value(value)
        if json_available() then
            return json.encode(value)
        end
        return value
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

    -- 主模块表
    local M = {
        L = {},
        G = {}
    }

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

    local function setImpl(key, value)
        return LuaDB.Set(key, encode_value(value))
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
        local result = LuaDB.All()
        for k, v in pairs(result) do
            result[k] = decode_value(v)
        end
        return result
    end
    M.All = wrap(allImpl, 0, M)

    local function setGImpl(key, value)
        return LuaDB.SetG(key, encode_value(value))
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
        local result = LuaDB.AllG()
        for k, v in pairs(result) do
            result[k] = decode_value(v)
        end
        return result
    end
    M.AllG = wrap(allGImpl, 0, M)

    local function dumpImpl()
        return LuaDB.Dump()
    end
    M.Dump = wrap(dumpImpl, 0, M)

    local function createMetatable(opts)
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

    setmetatable(M.L, createMetatable({
        getFunc = function(k)
            return M.Get(k)
        end,
        setFunc = function(k, v)
            M.Set(k, v)
        end,
        delFunc = function(k)
            M.Del(k)
        end,
        toStringFunc = function()
            return tostring(M) .. ":L"
        end
    }))

    setmetatable(M.G, createMetatable({
        getFunc = function(k)
            return M.GetG(k)
        end,
        setFunc = function(k, v)
            M.SetG(k, v)
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
                key = encode_value(key)
                if type(key) ~= "string" then
                    key = tostring(key)
                end
            end
            return namespace .. key
        end

        -- Define methods with parameter counts
        local function _setImpl(key, value)
            return LuaDB.Set(prefix_key(key), encode_value(value))
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
            local result = LuaDB.All()
            local output = {}
            for k, v in pairs(result) do
                -- 只返回命名空间前缀的键
                if type(k) == "string" and k:sub(1, #namespace) == namespace then
                    local raw_key = k:sub(#namespace + 1)
                    output[raw_key] = decode_value(v)
                end
            end
            return output
        end
        instance.All = wrap(_allImpl, 0, instance)

        local function _setGImpl(key, value)
            return LuaDB.SetG(prefix_key(key), encode_value(value))
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
            local result = LuaDB.AllG()
            local output = {}
            for k, v in pairs(result) do
                -- 只返回命名空间前缀的键
                if type(k) == "string" and k:sub(1, #namespace) == namespace then
                    local raw_key = k:sub(#namespace + 1)
                    output[raw_key] = decode_value(v)
                end
            end
            return output
        end
        instance.AllG = wrap(_allGImpl, 0, instance)

        local function _dumpImpl()
            System.LogAlways("$6--- [Global Data For: " .. namespace:sub(1, -2) .. "] ---\n")
            local globalResult = instance.AllG()
            for k, v in pairs(globalResult) do
                System.LogAlways("  $5" .. k .. "  $8" .. type(v) .. "  $3" .. tostring(encode_value(v)) .. "\n")
            end
            -- 修复此处：从插入表格改为插入字符串
            System.LogAlways("$6--- [Saved Data For: " .. namespace:sub(1, -2) .. "] ---\n")
            local localResult = instance.All()
            for k, v in pairs(localResult) do
                System.LogAlways("  $5" .. k .. "  $8" .. type(v) .. "  $3" .. tostring(encode_value(v)) .. "\n")
            end
        end
        instance.Dump = wrap(_dumpImpl, 0, instance)

        -- ------------- 批量设置子表的元表----------------
        -- 本地操作（L 子表）
        setmetatable(instance.L, createMetatable({
            getFunc = function(k)
                return instance.Get(k)
            end,
            setFunc = function(k, v)
                instance.Set(k, v)
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
            getFunc = function(k)
                return instance.GetG(k)
            end,
            setFunc = function(k, v)
                instance.SetG(k, v)
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
end)()
)lua";

#endif //DB_H
