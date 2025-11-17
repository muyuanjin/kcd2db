# CryEngine SDK 快照

`external/cryengine` 提供了最小化的 CryEngine 头文件镜像, 仅用于在本项目中声明游戏引擎类型。

## 来源

- 上游仓库: <https://github.com/ValtoGameEngines/CryEngine>
- 采集方式: 根据 `kcd2db` 需要, 将相关接口头文件复制并做最小改动以便独立编译。

## 使用约定

1. 这些文件视为**第三方 SDK**, 只作为编译期依赖, 不得在常规功能开发中修改。
2. 如果上游版本变化需要同步, 请统一在此目录更新, 并在提交说明中标明上游 commit/标签。
3. 任何调试、Clang-Tidy 或审查任务应排除该目录; 它只是一份引擎快照的引用, 非项目自有代码。

## 更新流程

1. 从上游仓库拉取最新代码或定位所需版本。
2. 仅复制编译所需的头文件到 `external/cryengine/include/cryengine/`。
3. 如需打补丁 (例如修复独立编译所需的声明), 勿直接混入业务改动, 在提交信息中解释原因。
