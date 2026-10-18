# 0059 CostDuty 对 QML 只读可见，门面的死入口删除

- **状态**：accepted
- **背景**：ADR 0028 把 `UpdateDuty` 暴露为只读属性后，`CostDuty` 与 `busyLabel` 留待决定（ADR 0029）。门面上还留着三个没有调用者的入口：`onSelectionReleased` 不在 `Q_INVOKABLE` 里，QML 到不了，活的路径是 `CaptureDuty` 自己连的钩子；`ignoreReview` 无人调用；`bubbleHoverChanged` 只被 `Bubble.qml` 调一次，而悬停计时与行高都在 QML 里完成，门面那一行只写了一条日志，没有读者。`main()` 另有一处 `checkForUpdatesAtStartup()`，只是转给 `UpdateDuty`，而同一段的同步已经直接调用 `syncDuty()->startAtLaunch()`。
- **决定**：删掉上述三个入口，`Bubble.qml` 的调用点同一次改动删掉；`main()` 改为 `controller.updateDuty()->checkAtStartup()`，门面的转发删除。`CostDuty` 按 ADR 0028 的写法标注 `QML_ANONYMOUS` 与 `QML_UNCREATABLE`，以 `Controller.costDuty` 只读属性暴露，`stats` 与 `cost` 改为 `Q_PROPERTY`，通知沿用 `stateChanged`；门面的 `stats()`、`cost()` 及同名属性删除。QML 与 qtest 的读取点在同一提交改为 `Controller.costDuty.*`，`tray.cpp` 改读 `costDuty()->stats()`。`busyLabel` 仍按 ADR 0029 留在门面。
- **后果**：门面不再持有 cost 与 stats 的第二条访问路径，QML 从拥有状态的 duty 读值。`statsChanged` 与 `settingsChanged` 仍由门面发出，`words`、`yearDays` 的绑定与同步调度不变。`bubbleHoverChanged` 删掉后，悬停不再进 C++ 日志；没有任何组件读那条日志，所以不补别处的记录。`docs/history/PHASE1.md` 仍提到这三个名字，那是历史记录，不改。删除与迁移没有兼容层，这是 ADR 0028 的要求。
