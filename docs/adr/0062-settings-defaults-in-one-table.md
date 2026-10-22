# 0062 设置默认值只有一张表，恢复默认有测试

- **状态**：superseded by ADR 0083（默认值并入设置表；恢复只走 setter 的原则不变）
- **背景**：恢复默认被拆在三处：`SettingsPopup.qml` 连续调 21 个 setter，`CaptureDuty::restoreDefaults()` 写采集项，`AppController::restoreCaptureDefaults()` 再补热键。默认值本身也分两处写：getter 里的 fallback 与 QML 里的实参各写一遍，`clipboardPolicy` 的默认值出现三次。没有任何用例断言恢复的结果，而恢复与新装早已不是同一组值——`etymology`、`example`、`reviewFrequency` 在面板里，恢复却不碰它们。
- **决定**：新增 `src/app/settings_defaults.h`，把面板拥有的默认值收在一处：getter 的 fallback 与 `AppController::restoreDefaults()` 都读它。恢复通过既有 setter 写入，所以 clamp 与校验仍是唯一的写入口；默认的服务商与模型从共享目录取，不再在 QML 与 `main()` 里各写一遍字面量。`KnownStore` 公开 `kDefaultLevel` 与 `kDefaultExplanationLang`，自身也改用它们。QML 的恢复只剩一次 `Controller.restoreDefaults()`。API key、自启、界面语言与每日预算不在这张表里：它们不是这个面板恢复的设置。
- **后果**：新增 `settings_defaults_test.cpp`，在两个全新的设置文档上构造两个 controller，改动所有可恢复项后调用恢复，逐键比较投影；第二段断言 API key、界面语言与每日预算在恢复后仍在。默认扫描白名单统一为 setter 产生的小写形式，新装与恢复因此显示同一个值。`docs/adr/0024` 的写入单元不变：表只提供值，写入仍走原来的 setter。代价是恢复现在会重置此前漏掉的词源、例句与复习频率，这是有意的行为修正。
