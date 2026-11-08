# 0167 扫描 payload 的发射门控：值得重建才发射

- **状态**：accepted
- **背景**：自动扫描实测（2026-11-08）页面静止时轨道仍每秒送 2–23 帧，每帧全量 `scanPlaced`——几百个词各建一个 `QVariantMap`、铅笔层 Repeater 整体重置、全屏层窗口随空帧 show/hide 风暴（日志 `pencil window shown` 高频刷屏）。主线程与渲染线程被重建打满，整机鼠标卡顿、风扇拉满，且与当日多起 `0xC0000005` 崩溃时间线重合（崩溃定位见 `logs/dumps` 的 WER LocalDumps 取证）。`ScanSession` 的每帧语义（ADR 0165/0166：扫描仍持有的每一帧都带 placed，空或不空）是有测试钉住的契约，不该为省电而松动。
- **决定**：
  - **门控在发射边界**：新增 `src/app/duties/scan_emission_gate.h`（纯头文件，无 QObject，时间由调用方递入）：与上一次已发射 payload 逐字段相同的 placement 直接丢弃——层上画的已经是它；变化的 placement 受 66 ms（约 15 Hz）节拍约束，节拍窗口内的进 `held_` 槽，最新观察覆盖旧的。
  - **尾随刷新**：被节流压住的最新 payload 由 `CaptureDuty` 的单发 `scanPlaceFlush_` 定时器在节拍打开时补发（`takeHeld`），帧流停止也不丢最后一次状态；刷新本身计一次发射，节拍自它重启。
  - **clear 复位**：`scanCleared` 的三个发射点（`routeScanStep` 的 step.cleared、`invalidateScan`、`setScanPaused`）统一走 `clearScanLines()`——门控遗忘已发射记忆与 held payload，页面回来后的第一次变化立即发射，不吃残余节拍。
  - **payload 投影与发射解耦**：`emitPlaced` 收拢原本内联的 `QVariantMap` 组装；门控判定发生在组装之前，被丢弃的帧连 map 都不建。
  - **测试**：新增 `scan_emission_gate_test.cpp` 九条进 `lens_gtest_unit`（首发即发、逐字段去重、节拍内 hold 与尾随、节拍外即发、最新覆盖旧的、刷新计入节拍、clear 复位、几何/判定变化不算相同、空帧收线只走一次节拍）。既有 `ScanSession.*`（14 条）与 `CaptureDuty.*`（13 条）不动，为行为保真门禁。
- **后果**：
  - 页面静止时 QML 重建归零，滚动时铅笔层至多 15 Hz 重绘；虚线在滚动中的刷新滞后至多 66 ms，虚线本就是近似摆放，不可感。
  - `ScanSession` 每帧带 placed 的契约原样保留，门控是 `routeScanStep` 一侧的发射决策；dev 面板 `scan frame` 走同一路径（场景单帧间隔 1 s，不受节拍影响）。
  - 轨道侧 16 ms 的 UIA 读取节奏不在本卡范围：那是 ADR 0163/0164 的采集设计，若仍需压热，另立卡调 `kTrackPeriod` 与读取批量的权衡。
