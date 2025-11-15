# Repository Guidelines

## 项目结构与模块组织
仓库产出 `kcd2db.asi` 注入模块，顶层 `CMakeLists.txt` 管理依赖。`src/kcd2db.cpp` 负责查找 `gEnv` 并重定向 `IGame::CompleteInit`，`src/db` 提供 LuaDB 与 SQLiteCpp 的桥接逻辑，`src/log` 统一日志宏，`src/lua` 保存对游戏 Lua 可见的 API 包装。逆向得到的偏移、协议或说明位于 `openspec/`，而 `tests/` 下按功能划分（`hooks`、`luadb`、`log`、`stubs`）方便模拟游戏上下文。构建产物写入 `build/` 或 `build-coverage/`，请避免将生成文件提交版本库。

## 构建、测试与开发命令
统一在 WSL 中使用 `winexec` 调用 Windows 工具以保持一致环境：
- `winexec cmake -B build -G "Visual Studio 17 2022" -DSQLITECPP_RUN_CPPLINT=OFF`：配置 MSVC Release 方案。
- `winexec cmake --build build --config Release`：产出发布版 `kcd2db.asi`。
- `winexec cmake --build build --config Debug`：生成含调试符号的 DLL，便于附加到游戏进程。
- `winexec ctest --test-dir build --output-on-failure`：运行全部 CTest/Lua 集成用例。
- `winexec cmake -B build-coverage -DENABLE_COVERAGE=ON`（如需统计覆盖率）。

## 编码风格与命名约定
使用 C++20、MSVC，始终开启 `/utf-8`。保持 4 空格缩进、左花括号换行，头文件使用 `#pragma once`。类型与类名用 PascalCase，函数与方法用 CamelCase，局部变量和静态助手使用 snake_case；日志标签遵循 `LogInfo/LogWarn/LogError` API。优先采用 RAII（`std::unique_ptr`, `std::optional`, `std::atomic`）和 `std::string_view` 以避免不必要复制；任何重复模式应提炼为 `src/db` 或 `src/kcd2` 下的独立助手。

## 测试指南
单元或集成脚本放在 `tests/` 中与模块同名子目录，Lua 场景放入 `tests/luadb`，钩子/虚拟环境桩件进入 `tests/hooks` 与 `tests/stubs`。新增行为需同时提供成功与失败路径断言，并在提交前执行 `winexec ctest --output-on-failure`，目标是覆盖新逻辑的主要分支（建议 ≥80% 行覆盖）。若测试依赖游戏 API，请使用 stub 或日志快照 (`tests/log`) 以保持可重复。

## 提交与拉取请求指南
按照历史记录（如 `Fix #1 Lua crash in multithreaded execution`）保持简短祈使句式，首字母大写，必要时引用 issue 编号或模块前缀。单个提交聚焦一个主题并包含相关测试。PR 描述需概述动机、方案与验证步骤，涉及偏移或 `openspec/` 更新时附上调试日志或截图；若修改影响游戏内接口，附加 Lua 片段示例能加速评审。

## 安全与配置提示
模块会修改游戏虚表与内存保护，请勿在未经验证的地址上尝试新钩子；所有偏移请集中声明并引用 `openspec` 或专用常量，便于热修复。提交前确认 `kcd2db.log` 路径与 `kcd2db.db` 迁移策略未被硬编码到用户特定目录，同时避免将真实游戏资源或密钥推送到仓库。
