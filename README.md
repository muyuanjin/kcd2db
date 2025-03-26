# LuaDB - Kingdom Come Deliverance II Lua Persistence Module

[中文](#简介)

## Introduction

This module provides **SQLite-based Lua data persistence** for Kingdom Come: Deliverance II mod developers, supporting
two storage modes:

- **Global Storage**: Persists data across game saves (APIs end with "G") with automatic save/load
- **Save-associated Storage**: Data bound to game save slots with automatic save/load synchronization

## Installation Requirements

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
2. Place the `.asi` file of this mod and ASI loader in the game root directory (where `KingdomCome.exe` resides)

## API Documentation

### The new version updates the ease-of-use API wrapper for lua
Use `DB.Create` to create a database object, then use the methods of that object to store and retrieve data.  

`DB` will automatically convert key, value into JSON strings for storage, and automatically parse JSON strings back into lua objects when reading.  

You can now store any Lua object encodable by [json.lua](https://github.com/rxi/json.lua)! Including tables, strings, numbers, booleans, etc.  

Strings in objects are no longer limited by character sets.

```lua
local myDB = DB.Create("MyAwesomeMod")  -- Use your Mod name or a unique enough string that won't conflict as the namespace
myDB.Set("player_health", 85.6)
myDB:Set("has_dragon_sword", true) -- myDB allows both .call and :call syntax
-- Test storage/retrieval of complex large objects
local test = {
    a = 1,
    b = "nihao",
    c = {
        d = 2,
        e = "shijie",
        f = {
            g = 3,
            h = "非ASCII字符"
        }
    },
    d = string.rep("a", 1000)
}
myDB.Set("test", test)

-- Global APIs (Cross-save)
myDB.SetG("settings", {volume = 0.8, fullscreen = true})
local settings = myDB.GetG("settings") -- return table {volume = 0.8, fullscreen = true}
```
# DB API Documentation

## Global Methods
- `DB.Get(key)` - Get local key value
- `DB.Set(key, value)` - Set local key value
- `DB.Del(key)` - Delete local key value
- `DB.Exi(key)` - Check if local key exists
- `DB.All()` - Get all local key values
- `DB.GetG(key)` - Get global key value
- `DB.SetG(key, value)` - Set global key value
- `DB.DelG(key)` - Delete global key value
- `DB.ExiG(key)` - Check if global key exists
- `DB.AllG()` - Get all global key values
- `DB.Dump()` - Print all data (Note: $1~9 in strings will be treated as color characters)
- `DB.Create("Your MOD")` - Create a namespace instance  

All methods support both . Call and : Call syntax, which can be selected according to personal preferences.

## Quick Access
- `DB.key` / `DB["key"]` - Access local key value (invalid if the method with the same name exists)
- `DB.L.key` - Always access local key value
- `DB.G.key` - Always access global key value
- `DB.Create("Your MOD").key` - Access namespace local key value
- `DB.Create("Your MOD").L.key` - Always access namespace local key value
- `DB.Create("Your MOD").G.key` - Always access namespace global key value

## Notes
1. When the key name is the same as an existing method, direct access will call the method instead of the key value
2. When using `Dump()`, $1~9 in strings will be parsed as color codes by the console
3. All values will be automatically JSON encoded/decoded (if JSON is available)
4. Namespaces will automatically add a ":" suffix

### Save-associated APIs

```lua
-- Store data (automatically bound to current save)
LuaDB.Set(key, value)

-- Retrieve data
local value = LuaDB.Get(key)

-- Delete data
LuaDB.Del(key)

-- Check key existence
local exists = LuaDB.Exi(key)
```

### Global APIs (Cross-save)

```lua
-- Store global data
LuaDB.SetG(key, value)

-- Retrieve global data
local value = LuaDB.GetG(key)

-- Delete global data
LuaDB.DelG(key)

-- Check global key existence
local exists = LuaDB.ExiG(key)
```

### Debug Commands

```lua
-- View all stored data in console
LuaDB.Dump()
```

## Supported Data Types

- Key : must be a string
- Value : can be a boolean, number, or string

| Type    | Storage Format         | Notes                                       |
|---------|------------------------|---------------------------------------------|
| Boolean | 0/1                    | 0 represents false                          |
| Number  | Single-precision float |                                             |
| String  | Latin1 encoding        | After testing, it can save a string of 10MB |

## Usage Examples

```lua
-- Save-associated data example
LuaDB.Set("player_health", 85.6)
LuaDB.Set("has_dragon_sword", true)

-- Global data example
LuaDB.SetG("global_kill_count", 42)
LuaDB.SetG("ending_unlocked", "bad_ending")

-- View data in console
LuaDB.Dump()
```

## Build

- `cmake -B build -G "Visual Studio 17 2022" -DSQLITECPP_RUN_CPPLINT=OFF`
- `cmake --build build --config Release`

## Debugging

- Uses SQLite3 database named `kcd2db.db` in game root
- Operation logs stored in `kcd2db.log` in game root
- If launched with `-console`, debug output will also appear in a separate console window

## Important Notes

This mod heavily utilizes reverse-engineered game internals and may be affected by game updates. If experiencing crashes
after game updates, try removing the mod file (or rename `.asi` extension to disable).

---

# LuaDB - 天国拯救2 Lua 数据持久化模块

## 简介

本模块为《天国拯救2》Mod开发者提供基于Sqlite 的 Lua 数据存储解决方案，支持两种存储模式：

- **全局存储**：跨存档永久保存数据（API以G结尾）自动保存/加载
- **存档关联存储**：数据随游戏存档保存/加载

## 安装要求

1. 安装 [终极 ASI 加载器](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
2. 将本Mod的`.asi`文件与ASI加载器共同放置于游戏根目录（`KingdomCome.exe`所在目录）

## API文档

### 新版本更新了 lua 的易用性API包装
使用 `DB.Create` 创建一个数据库对象，然后使用该对象的方法进行数据存储和读取。  

`DB`将自动把key,value转换为json字符串进行存储，读取时自动解析json字符串还原为lua对象。  

你现在可以存储任何可以通过[json.lua](https://github.com/rxi/json.lua)编码的Lua对象了！，包括table，string，number，boolean等等，对象中的字符串也不再有字符集限制  

```lua
local myDB = DB.Create("MyAwesomeMod")  -- 使用你的 Mod 名或者一个足够独特不会冲突的字符串作为命名空间
myDB.Set("player_health", 85.6)
myDB:Set("has_dragon_sword", true)  -- myDB 允许.调用 和 :调用 语法
-- 测试复杂大对象的存储/读取
local test = {
    a = 1,
    b = "nihao",
    c = {
        d = 2,
        e = "shijie",
        f = {
            g = 3,
            h = "测试字符"
        }
    },
    d = string.rep("a", 1000)
}
myDB.Set("test", test)

-- 全局API（跨存档）
myDB.SetG("settings", {volume = 0.8, fullscreen = true})
local settings = myDB.GetG("settings") -- return table {volume = 0.8, fullscreen = true}
```
# DB API 文档

## 全局方法
- `DB.Get(key)` - 获取本地键值
- `DB.Set(key, value)` - 设置本地键值
- `DB.Del(key)` - 删除本地键值
- `DB.Exi(key)` - 检查本地键是否存在
- `DB.All()` - 获取所有本地键值
- `DB.GetG(key)` - 获取全局键值
- `DB.SetG(key, value)` - 设置全局键值
- `DB.DelG(key)` - 删除全局键值
- `DB.ExiG(key)` - 检查全局键是否存在
- `DB.AllG()` - 获取所有全局键值
- `DB.Dump()` - 打印所有数据(注意: 字符串中的$1~9会被当作颜色字符)
- `DB.Create("Your MOD")` - 创建命名空间实例  

所有的方法都同时支持 . 调用和 : 调用语法，可以根据个人喜好选择使用

## 快捷访问
- `DB.key` / `DB["key"]` - 访问本地键值(与已有方法同名时无效)
- `DB.L.key` - 总是访问本地键值
- `DB.G.key` - 总是访问全局键值
- `DB.Create("Your MOD").key` - 访问命名空间本地键值
- `DB.Create("Your MOD").L.key` - 总是访问命名空间本地键值
- `DB.Create("Your MOD").G.key` - 总是访问命名空间全局键值

## 注意事项
1. 键名与已有方法同名时，直接访问会优先调用方法而非键值
2. 使用`Dump()`时，字符串中的$1~9会被控制台解析为颜色代码
3. 所有值会自动进行JSON编码/解码(如果json可用)
4. 命名空间会自动添加":"后缀

### 存档关联API

```lua
-- 存储数据（自动关联当前存档）
LuaDB.Set(key, value)

-- 获取数据
local value = LuaDB.Get(key)

-- 删除数据
LuaDB.Del(key)

-- 检查键是否存在
local exists = LuaDB.Exi(key)
```

### 全局API（跨存档）

```lua
-- 存储全局数据
LuaDB.SetG(key, value)

-- 获取全局数据  
local value = LuaDB.GetG(key)

-- 删除全局数据
LuaDB.DelG(key)

-- 检查全局键是否存在
local exists = LuaDB.ExiG(key)
```

### 调试命令

```lua
-- 控制台查看所有存储数据
LuaDB.Dump()
```

## 数据类型支持

- Key : 必须是字符串
- Value : 可以是布尔值、数字或字符串

| 类型  | 存储格式      | 说明                  |
|-----|-----------|---------------------|
| 布尔值 | 0/1       | 0表示false            |
| 数字  | 单精度浮点     |                     |
| 字符串 | Latin1 编码 | 经过测试，它可以保存一个10MB字符串 |

## 使用示例

```lua
-- 存档关联数据示例
LuaDB.Set("player_health", 85.6)
LuaDB.Set("has_dragon_sword", true)

-- 全局数据示例
LuaDB.SetG("global_kill_count", 42)
LuaDB.SetG("ending_unlocked", "bad_ending")

-- 控制台查看数据
LuaDB.Dump()
```

## 构建

- `cmake -B build -G "Visual Studio 17 2022" -DSQLITECPP_RUN_CPPLINT=OFF`
- `cmake --build build --config Release`

## 调试

- 使用位于游戏根目录下名为 `kcd2db.db` 的 SQLite3 数据库
- 操作日志存储在游戏根目录下的 `kcd2db.log` 文件中
- 如果使用 `-console` 参数启动，调试输出也会显示在单独的控制台窗口中

## 注意事项

由于本MOD大量使用反汇编的游戏内部细节，可能会受到游戏更新的影响，如遇到更新后游戏崩溃问题，请尝试删除本MOD文件（或将后缀名
`.asi`改为其他后缀名）。