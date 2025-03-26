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


        -- 存档关联API

        function instance.Set(key, value)
            return LuaDB.Set(prefix_key(key), encode_value(value))
        end

        function instance.Get(key)
            return decode_value(LuaDB.Get(prefix_key(key)))
        end

        function instance.Del(key)
            return LuaDB.Del(prefix_key(key))
        end

        function instance.Exi(key)
            return LuaDB.Exi(prefix_key(key))
        end

        -- 全局API（跨存档）

        function instance.SetG(key, value)
            return LuaDB.SetG(prefix_key(key), encode_value(value))
        end

        function instance.GetG(key)
            return decode_value(LuaDB.GetG(prefix_key(key)))
        end

        function instance.DelG(key)
            return LuaDB.DelG(prefix_key(key))
        end

        function instance.ExiG(key)
            return LuaDB.ExiG(prefix_key(key))
        end

        -- 调试命令，在实现特定前缀查询前原样转发
        function instance.Dump()
            return LuaDB.Dump()
        end

        return instance
    end

    return M
end)()
)lua";

#endif //DB_H
