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
    local M = {}

    -- 创建带命名空间的 DB 实例
    function M.Create(namespace)
        assert(type(namespace) == "string" and #namespace > 0,
               "Namespace must be a non-empty string")

        -- 确保命名空间以冒号结尾，作为分隔符
        if not namespace:match(":$") then
            namespace = namespace .. ":"
        end

        local instance = {}

        -- 内部方法：添加命名空间前缀
        local function prefix_key(key)
            if type(key) ~= "string" then
                key = encode_value(key)
            end
            return namespace .. key
        end

        -- 通用包装函数，处理点调用和冒号调用
        local function wrap(func, param_count)
            return function(...)
                local args = {...}
                local is_method_call = #args >= 1 and args[1] == instance
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

        -- Define methods with parameter counts
        local function setImpl(key, value)
            return LuaDB.Set(prefix_key(key), encode_value(value))
        end
        instance.Set = wrap(setImpl, 2)

        local function getImpl(key)
            return decode_value(LuaDB.Get(prefix_key(key)))
        end
        instance.Get = wrap(getImpl, 1)

        local function delImpl(key)
            return LuaDB.Del(prefix_key(key))
        end
        instance.Del = wrap(delImpl, 1)

        local function exiImpl(key)
            return LuaDB.Exi(prefix_key(key))
        end
        instance.Exi = wrap(exiImpl, 1)

        local function setGImpl(key, value)
            return LuaDB.SetG(prefix_key(key), encode_value(value))
        end
        instance.SetG = wrap(setGImpl, 2)

        local function getGImpl(key)
            return decode_value(LuaDB.GetG(prefix_key(key)))
        end
        instance.GetG = wrap(getGImpl, 1)

        local function delGImpl(key)
            return LuaDB.DelG(prefix_key(key))
        end
        instance.DelG = wrap(delGImpl, 1)

        local function exiGImpl(key)
            return LuaDB.ExiG(prefix_key(key))
        end
        instance.ExiG = wrap(exiGImpl, 1)

        local function dumpImpl()
            return LuaDB.Dump()
        end
        instance.Dump = wrap(dumpImpl, 0)

        return instance
    end

    return M
end)()
)lua";

#endif //DB_H
