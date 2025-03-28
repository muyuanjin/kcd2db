# [LuaDB](https://www.nexusmods.com/kingdomcomedeliverance2/mods/1523) - Kingdom Come Deliverance II Lua Persistence Module  

[中文](#中文)

## Introduction

This module provides **SQLite-based Lua data persistence** for Kingdom Come: Deliverance II mod developers, supporting
two storage modes:

- **Global** : Data persists across all game saves (independent of the game save).

- **Local** : Bound to the game save, data is associated with a specific game save file:
  - **Automatically saved** when the game is saved.
  - **Automatically loaded** when the game loads that save.

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
## Sample
```lua
YourMod = YourMod or (function()
-- Make LuaDB an enhancement for mods, allowing users to opt for LuaDB to achieve persistence.
local db = LuaDB and DB and select(2, pcall(DB.Create,"YourMod"))
return {
version = "__VERSION__",
localData = db and db.L or {},
globalData = db and db.G or {}
}
end)()

function YourMod:Init()
local settingA = YourMod.localData.settingA
local settingB = YourMod.globalData.settingB
-- do something with settings
end

function YourMod:Save()
YourMod.localData.settingA = "valueA"
YourMod.globalData.settingB = { a = 1, b = 2 }
-- save settings
end

function YourMod:DoSomething()
local settingA = YourMod.localData.settingA
local settingB = YourMod.globalData.settingB
-- do something with settings

-- update settings if something changed
YourMod.localData.settingA = settingA
YourMod.globalData.settingB = settingB
end

YourMod:Init()
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
- `DB("Your MOD")` - Equivalent to `DB.Create("Your MOD")`
- `DB.key` / `DB["key"]` - Access local key value (invalid if the method with the same name exists)
- `DB.L.key` /  `DB.L["key"]`  - Always access local key value, If you are not sure whether your key conflicts with an existing method name
- `DB.G.key` / `DB.G["key"]` - Always access global key value, If you are not sure whether your key conflicts with an existing method name

## Notes
1. Namespace names cannot contain colons, "namespace:" will be used as a key prefix in the database to isolate data from different mods.
2. When the key name is the same as an existing method, direct access will call the method instead of the key value, use. L or. G instead.
3. All values will be automatically JSON encoded/decoded (the game has built-in json.lua V0.1.1).
4. The JSON string size of a single object should not exceed 1 billion bytes (approximately 953MB), otherwise Sqlite will report an error "string or blob too big," making it impossible to store.
5. When using `Dump()`, $0~9 in strings will be parsed as color codes by the console. The in-game console cannot display non-ASCII characters, and DEBUG log data will be truncated to prevent lag.

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

## Want to know how I found the offset? Check out this [How to find the address of gEnv](https://github.com/muyuanjin/kcd2-mod-docs/blob/main/DISASSEMBLY.md)

---
<a name="中文"></a>
# [LuaDB](https://www.nexusmods.com/kingdomcomedeliverance2/mods/1523) - 天国拯救2 Lua 数据持久化模块

## 简介

本模块为《天国拯救2》Mod开发者提供基于Sqlite 的 Lua 数据存储解决方案，支持两种存储模式：

- **全局**：数据在所有游戏存档中持久存在（独立于游戏存档）。

- **本地**：绑定到游戏存档，数据与特定的游戏存档文件关联：
    - 游戏保存时**自动保存**。
    - 游戏加载该存档时**自动加载**。

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

## 示例
```lua
YourMod = YourMod or (function()
-- 使 LuaDB 成为MOD的增强功能，使用户可以选择 LuaDB 实现持久性。
local db = LuaDB and DB and select(2, pcall(DB.Create,"YourMod"))
return {
version = "__VERSION__",
localData = db and db.L or {},
globalData = db and db.G or {}
}
end)()

function YourMod:Init()
local settingA = YourMod.localData.settingA
local settingB = YourMod.globalData.settingB
-- do something with settings
end

function YourMod:Save()
YourMod.localData.settingA = "valueA"
YourMod.globalData.settingB = { a = 1, b = 2 }
-- save settings
end

function YourMod:DoSomething()
local settingA = YourMod.localData.settingA
local settingB = YourMod.globalData.settingB
-- do something with settings

-- update settings if something changed
YourMod.localData.settingA = settingA
YourMod.globalData.settingB = settingB
end

YourMod:Init()
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
- `DB("你的MOD")` - 等同于 `DB.Create("你的MOD")`
- `DB.key` / `DB["key"]` - 访问本地键值（若存在同名方法则无效）
- `DB.L.key` / `DB.L["key"]` - 始终访问本地键值，适用于不确定键名是否与现有方法名冲突时
- `DB.G.key` / `DB.G["key"]` - 始终访问全局键值，适用于不确定键名是否与现有方法名冲突时


## 注意事项
1. 命名空间名称不能包含冒号，“namespace:”将作为数据库中的键前缀，用于隔离不同模组的数据。
2. 当键名与现有方法同名时，直接访问将调用方法而不是键值，请使用.L或.G代替。
3. 所有值将自动进行JSON编码/解码（游戏内置了json.lua V0.1.1）。
4. 单个对象的JSON字符串大小不应超过10亿字节（约953MB），否则Sqlite会报错“字符串或二进制数据过大”，导致无法存储。
5. 使用`Dump()`时，字符串中的$0~9将被控制台解析为颜色代码。游戏内控制台无法显示非ASCII字符，DEBUG日志数据将被截断以防止卡顿。

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

## 是如何找到偏移量的？ [如何找到gEnv的地址](https://github.com/muyuanjin/kcd2-mod-docs/blob/main/DISASSEMBLY.md#%E5%A6%82%E4%BD%95%E6%89%BE%E5%88%B0genv%E7%9A%84%E5%9C%B0%E5%9D%80)