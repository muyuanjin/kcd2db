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

| Type    | Storage Format         | Notes                   |
|---------|------------------------|-------------------------|
| Boolean | 0/1                    | 0 represents false      |
| Number  | Single-precision float |                         |
| String  | Latin1 encoding        | Max 255 bytes currently |

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
- Build with Cmake and MSVC 2022

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

| 类型  | 存储格式      | 说明        |
|-----|-----------|-----------|
| 布尔值 | 0/1       | 0表示false  |
| 数字  | 单精度浮点     |           |
| 字符串 | Latin1 编码 | 目前最长255字节 |

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
- 使用 Cmake 和 MSVC 2022 构建

## 调试
- 使用位于游戏根目录下名为 `kcd2db.db` 的 SQLite3 数据库
- 操作日志存储在游戏根目录下的 `kcd2db.log` 文件中
- 如果使用 `-console` 参数启动，调试输出也会显示在单独的控制台窗口中

## 注意事项

由于本MOD大量使用反汇编的游戏内部细节，可能会受到游戏更新的影响，如遇到更新后游戏崩溃问题，请尝试删除本MOD文件（或将后缀名
`.asi`改为其他后缀名）。