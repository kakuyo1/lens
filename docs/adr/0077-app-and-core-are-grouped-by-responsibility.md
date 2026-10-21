# 0077 按职责给 app 与 core 分组

- **状态**：accepted
- **背景**：`llm`、`capture`、`sync`、`update` 按能力切分，一个模块一个变化理由；`app` 与 `core` 按 “有没有 Qt” 切分，于是 `app` 收下所有带 Qt 的东西——QML、用例、Win32、玻璃与宠物，`core` 收下所有不带 Qt 的东西——规则、设置文档、加密与开发者命令语法。纯映射的 `sync_state` 与设置分区 `pet_store` 因为依赖 store 而落在 `app`，通用计时器 `profile` 落在 `core`，都是这条规则的副作用。
- **决定**：两个 target 不动，目录按职责分组。
  - `core/domain`：纯规则，包括筛选、复习曲线、同步合并与映射、宠物规则。
  - `core/document`：设置文档的所有者与分区，包括 `KnownStore`、`SettingsSection`、统计/复习/宠物分区。
  - `core/crypto`：`vault`。
  - `core/dev`：开发者命令语法与行分帧。
  - `app/duties`：用例对象与它们的纯辅助。
  - `app/platform`：Win32 与 shell 集成；`pet_window_native` 改名 `window_native` 并移入此组，因为玻璃也要排除窗口。
  - `app/glass`：桌面读取、复制与 sheet item。
  - `app/pet`：宠物的 Qt 壳。
  - `app/dev`：开发者表面的管道与执行。
  - `app` 根只留门面、组合根与跨组共享的设置与投影头文件。

  依赖方向：`core` 只依赖 `util`；`duties` 可以依赖 `platform` 与下层能力；`glass` 与 `pet` 可以依赖 `platform`，但两者互不依赖；`platform` 不依赖 `glass` 与 `pet`；只有 `app` 根与 `app/dev` 可以碰门面。
- **后果**：目录移动是一次性的 include 改动，行为不变；target 仍是 `lens_core` 与 `lens_app`，编译器看不见组边界，因此新增 `scripts/quality/layer-check.sh`，由 `.githooks/pre-commit` 与 CI 的 hygiene 任务执行。`qt_add_qml_module` 只注册 backing target 的类型，所以把 duties、platform、glass 拆成独立 target 需要 `QML_FOREIGN` 一类注册垫片；在需要编译器强制之前不做。`AppController` 仍是宽门面，那是 QML 契约本身，不是分组问题。
