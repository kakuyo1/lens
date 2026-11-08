# 0165 CaptureDuty 的五份职业分家，扫描会话成为深模块

- **状态**：accepted
- **背景**：评审（2026-11-08）卡 2：选区捕获、OCR 引擎生命周期、上传确认、悬浮取词、扫描会话五份职业挤在 `capture_duty.cpp`（1581 行）一个类里。扫描与 OCR 共用 `generation_`/`screenshotGeneration_` 与 pause 旗标的失效纪律缠在一起；`scanTick`/`onTrackFrame`/`scanJudged_` 没有任何单元能单独跑（CI 够不着），dev surface 也没有扫描钩子——ADR 0164 把采集策略收进 TrackCore 时，判定与失效这半边明确留在 duty 侧，留下的正是这块无人能测的接线。近两周几乎每个扫描 commit 都落在这里。先例：ADR 0139（ceremony/stroll 各成 module，控制器退为 adapter）、ADR 0140（ExplanationExchange 收拢事务）。
- **决定**：
  - **五个 module，duty 退为 adapter**（均 `src/app/duties/`，无 QObject、无钟，值进值出）：`ScanSession`（扫描会话：门、track 生命、逐表面判定、verdict 失效，自有 generation 与 pause）、`UploadTransaction`（上传事务：冻结裁片、consent、发送、重试决定）、`OcrLifecycle`（引擎生命：选中引擎、扫描用 Tesseract、migrate/probe/test/detect 状态）、`SelectionCapture`（选区：门、抓取、分类、bar/action/ready 三个 payload）、`HoverFlow`（悬浮节奏：rest、read、ask）。QTimer、QObject 信号、平台读取（`foregroundWindow`/monitor→QScreen→band）、executor 作业槽、`screenshotGeneration_`、BundleDownloader、settings 投影与 setSetting 分发留在 duty——它们是 adapter 本职或五职业的公共点。
  - **ScanTrack 缝**（0164 同款，两适配器成真）：`ScanSession` 定义 `ScanTrack{setBand}` + `TrackFactory`；生产适配器是 `capture_duty.cpp` 匿名空间的 `PageTrackScanTrack`（10 行，包 `PageTrack`），测试假适配器脚本化（`scan_session_test.cpp`）。FrameSink 的 generation 捕获与 QMetaObject 队列投递由 duty 注入的 marshal 逐字保留。
  - **失效契约一跳可见**：`session_.invalidate()` 是扫描失效唯一入口（++generation、清 context/track/双缓存，duty 无条件 emit scanCleared）；上传路径只在确有上传被放弃时失效，由 `UploadStep::cleared` 显式路由；`setBudgetPaused` 由 duty 分别打 `screenshotGeneration_` 与 `session_.invalidate()`——两条纪律不再共用成员。
  - **判定原样搬运**（候选 3 不在本卡）：`filterWords(surface, known, mastered, band, minimumWordLength, false)`、Mastered 不 carriage、判定缓存按 surface、verdict payload 的 `new`/`known` 字符串协议、`LENS_INFO` 行格式逐字不变；`verdictChanged` 重判无失效。
  - **dev surface 扫描钩子**：`scan arm <x,y,w,h>`（备好注入帧的摆放几何）、`scan frame <json>`（脚本化 TrackFrame 走 onFrame 全链路到铅笔层）、`scan clear`；`state` 增 `scan` 键（autoScan/tracking/generation/candidates）。离 Chrome 可驱动扫描（`scripts/qa/scenarios/scan-session.txt`）。
  - **测试**：`scan_session_test.cpp` 13 条——首个 duty 侧扫描循环测试（门序静默、等级漂移、非 Chrome 一次性拆除与 mark 持住豁免、context 变更失效+warm 先于 busy、busy 挡 track、band 跟随、帧判定/逐帧必发、stale 丢弃、lost 清线不 bump、verdict 重判、invalidate 无条件 cleared）。现有 `CaptureDuty.*` 13 条逐字不动、全绿，为行为保真门禁。
- **后果**：
  - `capture_duty.{h,cpp}` 346+1581 → 约 300+900 行，删除的是实现本体（无转发壳；`classifyText` 保留为路由别名，因 duty 内 4 处调用）。公共 QML 契约（Q_INVOKABLE/信号/rapidOcrBundle）与 `app_controller.cpp` 零改动。
  - 0164 的归属表更新：`scanJudged_`/`scanCandidates_`/`verdictChanged`/代际从 “duty” 改记为 “ScanSession（仍在 PageReader 缝外，TrackCore 判明盲）”。
  - dev 冒烟注意到一条既有行为：启动后首个 scanTick 消费一次等级漂移失效（scanLevel_ 初始 -1），注入帧若早于它会作废——剧本以 `wait 1s` 开头；注入词须在档位频段的 mastered 侧之外才画线（level 5 = 底 3000 ∪ TEM4/TEM8/B1B2，`ubiquitous` 是 TEM8 词）。
