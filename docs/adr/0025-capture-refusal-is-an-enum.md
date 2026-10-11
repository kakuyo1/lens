# 0025 取词被拒的原因是枚举，不是字符串

- **状态**：accepted
- **背景**：`CaptureDuty::captureRefusalReason()` 返回过一个 `QString`：四个 gate 原因（`budget-paused` / `checking` / `busy` / `screenshot-off`）加区域那半（`outside-screen` / `too-large`），运行时不好时再把 `ocrStatus()` 的 token 原样带出来。`AppController` 用九次比较把它翻成一句话，最后一条臂兜住所有剩下的 —— 于是 `"timeout"` 与 `"start-failed"` 被说成 “OCR runtime is unavailable”，而新加一个状态只会默默加入它们。`ocrStatus()` 那边同样有一个 `default:`，新状态一律变成 `failed`。两个洞都不出声：改错了没有编译错误，也没有一条用例会红。
- **决定**：新增 `src/app/capture_refusal.h`（纯头文件，与 `notice.h` / `tray_state.h` 同形状）：`enum class CaptureRefusal` 13 个值 —— 就是今天 token 的集合；`refusalKey()` 给出 token；`refusalOf(capture::OcrStatus)` 给出运行时状态对应的原因，`Ready` 没有原因、返回 `std::nullopt`。`captureRefusalReason()` 与 `captureRegion()` 改为返回 `std::optional<CaptureRefusal>`（空就是能取），`reportCaptureRefusal()` 收枚举并用**一条无 default 的 switch** 给出句子，`screenshotFailed` 的信号改带枚举，**没有接收者的 `ocrFailed` 删掉**。token 与句子都是字节不变的，QML 与 `.ts` 一行未动。
- **后果**：`capture::OcrStatus::Ready` 是唯一一个 “不是一个原因” 的状态，所以 `refusalOf` 返回 optional 而不是往枚举里塞一个 `Ready` —— 取词的闸门因此不必再自己比一次 `!= Ready`。

  两处**折叠**写成代码而不是留白：`OutsideScreen` 与 `RegionTooLarge` 共用一句，四个运行时状态（`RuntimeMissing` / `StartFailed` / `TimedOut` / `Failed`）共用一句。折叠是 facade 的决定，词汇把它们分开留着。

  **“新增值会被编译器点名” 这句话不成立，如实记着。**两条 switch 都不写 default，每个值都摆在明处，但 MSVC 的 C4061 / C4062 默认关闭，而这个仓库没有开任何警告级别（`CMakeLists.txt` 只设 `/utf-8`）。实测过：打开 C4061 之后，一个 `switch (Qt::Key)`（`global_hotkey.cpp:46`）就报 458 条，噪声会淹掉信号，所以没开。于是这条保证落在两处。一是 `refusalOf` 的兜底**关闸而不是开闸**：未分类的状态落到 `Failed`（“运行时挂了”），与旧 `default:` 的行为一致 —— 若落到 `std::nullopt`，闸门会**打开**、设置页会说 `"ready"`，那比被替换掉的旧代码更糟。二是句子表那条 switch 穷尽，新加一个原因必须在 `reportCaptureRefusal` 里给出一句话。真要 “被点名”，需要 C4061 配 per-target 白名单，那是另外一件改动。

  **一处读者可见的变化，是修好的而不是保持的。**框选一块没有文字的图：旧代码发 `screenshotFailed(ocrStatus())`，而 `ocrStatus()` 读的是 `ocrState_` —— 取词闸门要求它必须是 `Ready` 才能开拍，所以那一路一直是 `"ready"`，落进链的兜底臂，读者读到的是 “OCR runtime is unavailable”；同一行的日志也照这样打。现在发的是这一份结果 `result.status` 对应的原因，读者读到 “No text was found in that region。” 或 “That region could not be read。”。这条路径**没有用例**：它要一个活的 OCR 引擎与事件循环，`CaptureDuty` 可注入的引擎只有 qtest 那条线够得着，而那边没有 OCR stub。被盖住的是映射（`EmptyText → EmptyText` 有用例），没被盖住的是**接线**。

  token 还是一条**跨 QML 的协议**：`SettingsCapturePage.qml` 认 `"checking"` 与 `"english-data-missing"`，`tst_ocr_capture.qml` 断言 `captureRegion()` 返回 `"screenshot-off"`，所以一个 token 改名就是一次 QML 改动。用例逐个钉住全部 13 个 token 正是为此 —— 十三份各不相同的期望值本身也就证明了它们两两不同，这也是为什么没有单列一条 “折叠的两半仍然是两个 token” 的用例：它在词汇这侧是重复的，而它真正说的事在句子表那侧，够不着。

  相邻的同类问题 `PendingSelection::kind`（枚举 → 字符串 → 枚举的完整往返，16 处比较在 `explanation_duty.cpp`、9 处在 `Bubble.qml`）本次**不动**，留作独立一条；`PendingSelection::preset` 不是同一种病 —— 它是 `llm_protocol.cpp` 的 map key，全程不透明传递、应用层零比较点。
