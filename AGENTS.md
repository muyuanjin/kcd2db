# Repository Guidelines

## 项目结构与模块组织
仓库产出 `kcd2db.asi` 注入模块，顶层 `CMakeLists.txt` 管理依赖。`src/kcd2db.cpp` 负责查找 `gEnv` 并重定向 `IGame::CompleteInit`，`src/db` 提供 LuaDB 与 SQLiteCpp 的桥接逻辑，`src/log` 统一日志函数，`src/lua` 保存对游戏 Lua 可见的 API 包装。`dist/` 保存可直接随 Mod 分发的 Lua fake DB 脚本，`docs/` 保存面向 Mod 作者的补充文档。`external/cryengine` 是最小 CryEngine 头文件镜像，仅提供编译期类型声明。当前仓库没有提交 `tests/` 或 `openspec/` 目录；若新增，请同步更新本文档和 README。构建产物写入 `build/` 或 IDE 生成目录，请避免提交生成文件。

## 关键实现边界
这是《天国拯救2》的 Windows x64 ASI 插件，CMake 会强制要求 Windows + MSVC。`src/kcd2db.cpp` 通过签名扫描定位 `gEnv`，修改 `IGame::CompleteInit` 虚表入口并注册 Lua API；任何改动都必须考虑游戏版本更新导致的指令上下文变化。触碰游戏 Lua 或 framework 状态的操作必须保持在游戏初始化线程上执行，Lua API 注册路径以 `CompleteInit` hook 为准；`main_thread` 只负责等待模块、定位 `gEnv`、安装 hook 和构造 `LuaDB`。`src/db/LuaDB.*` 维护 `m_saveCache` 与 `m_globalCache`：存档关联数据在 `OnSaveGame` 写入，全局数据由 `OnPostUpdate` 脏标记定时写入。`src/lua/db.h` 的 `DB` 包装层负责命名空间隔离与 JSON 编解码；底层 `LuaDB` 只直接处理 bool、float 和 string。

## CryEngine SDK 镜像
`external/cryengine/include/cryengine` 存放从 [ValtoGameEngines/CryEngine](https://github.com/ValtoGameEngines/CryEngine) 复制的最小头文件，仅用于提供引擎类型声明，帮助编译通过：
- 视为第三方依赖，禁止在业务功能或代码审查中修改、引用或报错；任何 Clang-Tidy/bug 检查工具都必须排除该目录。
- 如需更新，请参考 `external/cryengine/README.md`，统一同步上游偏移后再提交，禁止夹带业务逻辑。
- 需要对照引擎实现时，可参考该目录代码，但评审内容应聚焦 `src/`。

## 构建、测试与开发命令
在 WSL 中构建时，确保 `cmake` 解析到 Windows CMake/MSVC 工具链，且源码与构建目录位于 Windows 可访问路径。
- `cmake -B build -G "Visual Studio 17 2022" -DSQLITECPP_RUN_CPPCHECK=OFF -DSQLITECPP_RUN_CPPLINT=OFF`：配置 MSVC Release 方案。
- `cmake --build build --config Release`：产出发布版 `kcd2db.asi`。
- `cmake --build build --config Debug`：生成含调试符号的 DLL，便于附加到游戏进程。
- `ctest --test-dir build --output-on-failure`：仅在重新引入 CTest 目标且 `ctest` 可解析到 Windows CTest 后运行；当前 `CMakeLists.txt` 未定义测试目标。

## 编码风格与命名约定
使用 C++20、MSVC，`CMakeLists.txt` 为 MSVC 添加 `/utf-8`。保持 4 空格缩进、左花括号换行，头文件使用 `#pragma once`。类型与类名用 PascalCase，成员变量使用 `m_` 前缀，成员函数沿用现有 PascalCase/CamelCase 风格，文件级辅助函数优先使用 snake_case；日志 API 为 `LogDebug/LogInfo/LogWarn/LogError`。优先采用 RAII（`std::unique_ptr`, `std::optional`, `std::atomic`）和 `std::string_view` 以避免不必要复制；业务辅助逻辑应放在 `src/`，不要写入 `external/cryengine` 镜像。

## 测试指南
当前没有提交测试源或覆盖率配置。新增测试时请在 `CMakeLists.txt` 中显式启用 CTest 目标，并将测试源放入清晰的 `tests/` 子目录；涉及游戏 API 的行为应使用 stub 或可重复的日志/数据库快照。提交前至少运行可用的配置与构建命令，若新增了测试目标，再确保 `ctest` 可解析到 Windows CTest 并运行 `ctest --test-dir build --output-on-failure`。

## 依赖与运行时调试
依赖通过 CMake FetchContent 获取：libmem v5.0.4、SQLite3 v3.49.1、SQLiteCpp v3.3.1；游戏侧使用内置 `json.lua`，`src/lua/db.h` 会在需要时加载 `Scripts/Utils/JSON/json.lua`。运行时日志写入游戏进程工作目录下的 `kcd2db.log`，SQLite 数据库为 `kcd2db.db`；以 `-console` 启动游戏会额外打开调试控制台。Lua 侧可调用 `DB.Dump()` 或 `LuaDB.Dump()` 查看当前缓存内容。

## 提交与拉取请求指南
按照历史记录（如 `Refactor: 使用 std::bit_cast 替代 static_cast 进行虚表函数指针转换`、`Fix #1 Lua crash in multithreaded execution`）保持简短祈使句式，首字母大写，必要时引用 issue 编号或模块前缀。单个提交聚焦一个主题并包含相关验证。PR 描述需概述动机、方案与验证步骤；若修改影响游戏内接口，附加 Lua 片段示例能加速评审。

## 版本发布与 Release Notes
当用户要求 agent 创建或推送发布 tag（例如 `v0.1.13`）时，不要只依赖 GitHub 自动生成的 release notes。发布前必须主动收集上一个 GitHub Release/tag 到目标版本的提交、变更文件和相关 issue/PR 信息，并由 agent 编写面向用户的 release notes。内容应突出玩家或 Mod 使用者需要知道的游戏版本适配、崩溃修复、存档/数据风险、安装变化和调试信息；内部重构、workflow、文档、第三方头文件整理等与用户无关的变化应简要带过或省略。生成 notes 后先展示给用户确认，未经确认不要推送 tag 或触发发布。

确认发布后，按现有 tag workflow 推送 tag。等待 GitHub Actions 创建/更新 Release 与上传资产后，用用户确认过的正文更新 GitHub Release，例如 `gh release edit <tag> --notes-file <notes.md>`。如果发布同时准备同步到 Nexus Mods，应基于同一份用户确认过的变更摘要再压缩成 Nexus 页面适合的简短说明，并再次确认是否上传。

## 安全与配置提示
模块会修改游戏虚表与内存保护，请勿在未经验证的地址上尝试新钩子；偏移和签名扫描逻辑应集中在 `src/kcd2db.cpp` 或专用常量附近，便于热修复。当前设计不支持 `FreeLibrary` 热卸载，禁用模块需要移除或重命名 `.asi` 后重启游戏。提交前确认 `kcd2db.log` 与 `kcd2db.db` 仍使用相对游戏进程工作目录，同时避免将真实游戏资源或密钥推送到仓库。

## Agent 文档约定
本仓库只维护 `AGENTS.md` 作为 agent 指南，不再单独维护 `CLAUDE.md` 或其他平台专用重复文件。新增或调整 agent 规则时，合并到本文档；长篇 API、安装和用户示例应放入 README，避免让 agent 指南膨胀成架构手册。
