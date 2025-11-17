# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

这是一个为《天国拯救2》(Kingdom Come: Deliverance II)游戏开发的 Lua 数据持久化模块。通过 ASI 插件方式注入到游戏进程,提供基于 SQLite 的 Lua 数据存储能力,支持全局数据和存档关联数据两种模式。

## 构建命令

### 配置项目
```powershell
cmake -B build -G "Visual Studio 17 2022" -DSQLITECPP_RUN_CPPLINT=OFF
```

### 编译 Release 版本
```powershell
cmake --build build --config Release
```

### 编译 Debug 版本
```powershell
cmake --build build --config Debug
```

**注意**: 项目仅支持 Windows 平台和 MSVC 编译器 (CMakeLists.txt:12-13)。

## 项目架构

### 核心组件

#### 0. 引擎 SDK (`external/cryengine`)
- `external/cryengine/include/cryengine` 仅包含从 [ValtoGameEngines/CryEngine](https://github.com/ValtoGameEngines/CryEngine) 抽取的最小头文件, 用于声明 `gEnv`、`IGame`、`IConsole` 等类型。
- 该目录视为第三方依赖, 只提供编译期符号; 不可在功能开发或代码审查中直接修改, 也无需对其中的告警/Clang-Tidy 结果负责。
- 更新流程请参考 `external/cryengine/README.md`, 在同步上游版本时应标注来源 commit 并保持零业务改动。

#### 1. 游戏引擎接口层 (`external/cryengine/include/cryengine/`)
包含反向工程的游戏引擎接口定义:
- **`env.h`**: 定义 `SSystemGlobalEnvironment` 结构体,这是游戏引擎的全局环境变量,包含对各个子系统的指针 (pConsole, pScriptSystem, pGame 等)
- **`IScriptSystem.h`**: Lua 脚本系统接口,定义了 `CScriptableBase`、`IFunctionHandler` 等用于注册 C++ 函数到 Lua 的接口
- **`IGameFramework.h`**: 游戏框架接口,包含 `IGameFrameworkListener` 用于监听游戏事件 (存档保存/加载等)
- **`IGame.h`**, **`IConsole.h`**: 游戏和控制台接口

#### 2. 数据库层 (`src/db/`)
- **`LuaDB.h`/`LuaDB.cpp`**: 核心数据库管理类
  - 继承自 `CScriptableBase` 将 C++ 方法暴露给 Lua
  - 继承自 `IGameFrameworkListener` 监听游戏事件自动保存/加载数据
  - 使用 SQLite3 作为存储后端
  - 维护两个缓存: `m_saveCache` (存档关联) 和 `m_globalCache` (全局)
  - 定时写入机制减少磁盘 I/O

#### 3. Lua 包装层 (`src/lua/`)
- **`db.h`**: 内嵌的 Lua 代码,提供易用的 API 包装
  - `DB.Create(namespace)`: 创建命名空间隔离的数据库实例
  - 自动 JSON 序列化/反序列化
  - 支持表语法糖访问 (`DB.key`, `DB.L.key`, `DB.G.key`)
  - 支持点调用和冒号调用两种语法

#### 4. 注入与钩子 (`src/kcd2db.cpp`)
- **`find_env_addr()`**: 通过签名扫描定位 `gEnv` 全局变量地址
  - 步骤 1: 扫描字符串 "exec autoexec.cfg" 的地址
  - 步骤 2: 查找引用该字符串的 LEA 指令
  - 步骤 3: 根据版本特征定位 MOV 指令
  - 步骤 4: 从 MOV 指令计算 gEnv 地址
  - 支持游戏版本自适应 (v1.2/v1.3 旧版模式, v1.4+ 新版模式)
- **虚表钩子**: Hook `IGame::CompleteInit()` (虚表第5个函数) 在游戏初始化时注册 Lua API

### 数据流

1. **初始化流程** (`DllMain` → `main_thread` → `start`):
   - 定位 `gEnv` 全局环境 → 等待游戏启动 → Hook `CompleteInit` → 创建 `LuaDB` → 注册 Lua API

2. **数据持久化**:
   - Lua 调用 `DB.Set(key, value)` → JSON 编码 → 写入缓存 → 定时批量写入 SQLite
   - 游戏保存时触发 `OnSaveGame` → 立即同步缓存到数据库
   - 游戏加载时触发 `OnLoadGame` → 切换 `m_currentSaveGame` → 清空缓存 → 从数据库重新加载

3. **命名空间隔离**:
   - 不同 Mod 通过命名空间前缀隔离数据 (例如 `"MyMod:"` 作为键前缀)
   - 避免不同 Mod 之间的数据冲突

### 关键技术细节

#### 虚表钩子实现 (kcd2db.cpp:162-176)
使用原子操作 + `VirtualProtect` 修改虚表指针:
```cpp
VirtualProtect(&vTable[COMPLETE_INIT_INDEX], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
InterlockedCompareExchangePointer(&vTable[COMPLETE_INIT_INDEX],
    reinterpret_cast<PVOID>(&Hooked_CompleteInit),
    reinterpret_cast<PVOID>(OriginalCompleteInit));
```

#### 批量写入优化 (LuaDB.cpp:72-115)
- 使用事务批量提交 (每批 100 条)
- UPSERT 语句避免重复插入
- 减少锁竞争和磁盘写入次数

#### 线程安全
- `m_mutex` 保护缓存访问
- `std::atomic<LuaDB*>` 用于初始化同步
- 数据库操作通过 `ExecuteTransaction` 统一事务管理

## 调试方法

### 运行时调试
- 日志文件: 游戏根目录 `kcd2db.log`
- 数据库文件: 游戏根目录 `kcd2db.db`
- 使用 `-console` 启动游戏,调试输出会显示在独立控制台窗口
- Lua 中调用 `DB.Dump()` 或 `LuaDB.Dump()` 打印所有数据到控制台

### 常见问题排查
1. **游戏崩溃**: 游戏更新后偏移量可能变化,检查 `find_env_addr()` 的签名扫描逻辑
2. **API 未注册**: 检查 `Hooked_CompleteInit` 是否被调用,查看 kcd2db.log
3. **数据丢失**: 检查 `OnSaveGame`/`OnLoadGame` 事件是否触发,查看数据库文件

## 代码风格约定

### 命名规范
- 类名: PascalCase (例如 `LuaDB`, `ScriptValue`)
- 成员变量: `m_` 前缀 + camelCase (例如 `m_currentSaveGame`, `m_globalCache`)
- 函数名: PascalCase (例如 `RegisterLuaAPI`, `GenericAccess`)
- 全局变量: `g` 前缀 (例如 `gEnv`, `gLuaDB`)

### 字符编码
- 源文件使用 UTF-8 编码 (CMakeLists.txt:15-16)
- 字符串字面量支持中文注释

### 日志宏 (log/log.h)
- `LogDebug()`: 调试信息
- `LogInfo()`: 一般信息
- `LogWarn()`: 警告
- `LogError()`: 错误

## 依赖库

### 自动下载 (通过 FetchContent)
- **libmem** (v5.0.4): 用于内存扫描和进程操作
- **SQLite3** (v3.49.1): 数据库引擎
- **SQLiteCpp** (v3.3.1): SQLite 的 C++ 封装

### 游戏内置
- **json.lua** (v0.1.1): Lua JSON 库 (游戏自带)

## 重要限制

1. **单个值大小限制**: JSON 字符串不超过 1GB (约 953MB),受 SQLite BLOB 大小限制
2. **命名空间规则**: 不能包含冒号 (`:` 用作分隔符)
3. **键类型**: 必须是字符串 (或可转换为字符串的类型)
4. **值类型**: 可序列化为 JSON 的类型 (table, string, number, boolean)
5. **平台限制**: 仅支持 Windows x64 + MSVC

## 版本兼容性

项目通过检测指令上下文适配不同游戏版本 (kcd2db.cpp:82-105):
- **v1.4+**: 新版特征 `4C 8B 92 18 01 00 00` (mov r10, [rdx+118h])
- **v1.2/v1.3**: 旧版特征 `48 8B 0D ? ? ? ?` (mov rcx, cs:qword_...)

游戏更新时需要验证签名扫描的有效性。
