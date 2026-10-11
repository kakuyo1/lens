# 0029 其余 duty 的一行转发迁到 duty 上

- **状态**：accepted
- **背景**：ADR 0028 只把 `UpdateDuty` 暴露为只读属性，门面上仍有一批只把调用转给 duty 的一行转发，以及镜像 duty 状态的 `bubble`、`notice`、`models` 属性和对应信号。这些转发让门面的接口仍然和它的实现一样宽。
- **决定**：`ExplanationDuty`、`CaptureDuty`、`ModelCatalogDuty` 以 `explanationDuty`、`captureDuty`、`modelCatalogDuty` 暴露为只读属性，标注沿用 ADR 0028。以下 11 个纯转发直接迁到 duty 上，QML 调用点同步改为 `Controller.<属性>.<函数>`：
  - `ExplanationDuty`：`runSelectionAction`、`mark`、`dismissBubble`、`dismissNotice`、`dismissReview`
  - `CaptureDuty`：`setAutoScan`、`setOcrCapture`、`setMinimumWordLength`、`setScanWhitelist`
  - `ModelCatalogDuty`：`setProvider`、`refreshModels`

  `bubble` 与 `notice` 属性及 `bubbleChanged`、`noticeChanged` 信号随 `ExplanationDuty` 走；`models` 属性及 `modelsChanged` 信号随 `ModelCatalogDuty` 走。门面上的这些属性和信号一并删除。

  以下仍留在门面：`reviewWord`，它把 `(int x, int y)` 换算成 `QPoint`；`setTesseractExecutable`、`setTesseractDataDirectory`，它们在 QML 边界把 `QUrl` 转成本地路径；所有设置写入；`explainLemma`，它要做预算判断；`captureRegion`；`removeWord`；`setModel`，它有条件地发射信号。`modeLabel` 也留在门面，因为它的 `tr()` 上下文是 `AppController`，迁走会改变翻译键。`busyLabel` 体内只有一行转发，但本次没有迁移，留待后续单独处理。

- **后果**：
  - 行为变化：`removeWord` 不再发出 `bubbleChanged`。`invalidateWord` 在被删的词正是气泡标题时，由 duty 自己清空气泡并发出信号；其他情况下，原来那次发射只会让 QML 重读同一个气泡。由 `LlmTest.Phase2RemovingAWordClearsItsOwnBubbleAndLeavesAnotherAlone` 锁定。
  - `CaptureDuty` 上的 `setTesseractExecutable`、`setTesseractDataDirectory` 接受 `QString`，QML 不直接调用它们；门面上接受 `QUrl` 的同名函数仍是 QML 的入口。两者并存，是因为路径转换属于 QML 边界，不属于 duty。
  - 门面的 `busyLabel` 仍是一行转发，这是未完成的部分。
  - 与 ADR 0028 的规则一致：只转发、不发门面信号的 invokable 迁走；会发门面信号、或要做转换的留在门面。
