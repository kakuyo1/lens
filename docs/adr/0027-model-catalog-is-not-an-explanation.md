# 0027 模型目录不是解释的职责

- **状态**：accepted
- **背景**：`ExplanationDuty` 的 interface 里一直混着两件事：解释的调度与呈现，以及服务商与模型列表。后者是 6 个公开方法（`setProvider` / `setModel` / `refreshModels` / `models` / `settings` / `currentProvider`）加三个私有方法与一条 `modelsFetched` 连接，`settingsChanged` 与 `modelsChanged` 两条信号也只有这一半在发；而两半只共享 `storage_` 与 `llm_`，不共享任何状态。于是每改一边都要跨过另一边的方法名，设置页读的键和解释队列的键住在同一个头文件里。ADR-0017 的两条规则还只端到端可断言：想钉住 “换服务商时模型跟不跟”，得先造一个真 `CostDuty`、手搓一个 `QNetworkReply` 对，再跑事件循环。
- **决定**：新增 `src/app/model_catalog_duty.{h,cpp}`（`ModelCatalogDuty : QObject`，只持 `StorageDuty&` 与 `llm::LlmClient&`），ADR-0017 / 0018 / 0020 的规则整体搬进去，`AppController` 的十处转发与那两条 connect 改指它。**另加 `src/app/model_catalog.h`**（header-only，与 `notice.h` / `tray_state.h` / `capture_refusal.h` / `year_grid.h` 同形，不进 CMake）：ADR-0017 里不需要文档也不需要网络的两条判断成为纯函数 —— `modelsKnownFor()`（服务商报过的列表压过目录种子，种子只给从没连上过的机器）与 `modelFollowingProvider()`（换服务商时模型跟不跟、跟到第几条，条件是四条），用例进 `app_seams_test.cpp` 的 `ModelCatalog` 套件。
- **后果**：`ExplanationDuty` 的公开面从 21 个方法缩到 15 个，且不再发 `settingsChanged` / `modelsChanged`；两条信号与它们背后的三条 ADR 现在住在同一个 module 里。

  **一处非显然的连带改动：两条价格句子换了翻译上下文。**`settings()` 里由 `tr()` 造出的 `No listed price; recorded cost is zero.` 与 `Input %1 / Output %2 %3 per %4 tokens` 原先落在 `lens::app::ExplanationDuty` 上下文下。函数搬走之后，它们改为与同函数里另外四条字符串一样、把上下文显式写成 `SettingsPopup` —— 那本来才是画这张 map 的表面，四条邻居早就在那里。四份 `.ts` 里的译文原样搬入，`lens::app::ExplanationDuty` 这个上下文随之消失（它只剩这两条）。这不是手感：改完之后拿 `lupdate` 自己的输出来逐条对过，四份文件里这两条的上下文、源串与译文与 lupdate 算出来的一字不差，`lrelease` 报 0 unfinished。

  **`languages` 留在 `ModelCatalogDuty::settings()` 里**，尽管它是给 `SettingsLearningPage`（解释语言下拉）与 `TrayMenu`（界面语言列表）读的、与模型无关。它是 `catalog.json` 的数据，拆出去要动两个 QML 文件与一条 qtest 断言，换不来任何东西。

  **发现但没动**：`es_ES` / `ja_JP` 两份 `.ts` 比 `en_US` / `zh_CN` 落后 46 条源字符串（`lupdate` 对它们报 “46 new”）。那是这次之前就有的债，两条语言各欠一份翻译，另开一条处理 —— 顺手把它们变成 46 条 `unfinished` 只会把债记进一个搬移的 diff 里。

  用例：`app_seams_test.cpp` 新增 6 条离线用例；`phase2_explanation_test.cpp` 里三条服务商用例改用 `ModelCatalogDuty` 驱动，断言一行未动。
