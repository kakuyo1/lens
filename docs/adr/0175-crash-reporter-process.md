# 0175 崩溃的即时反馈来自随包的独立 reporter 进程

- **状态**：accepted
- **背景**：ADR 0174 的通知在下一次启动才出现。读者熟悉的即时弹窗都来自独立进程：垂死进程画不了 UI——堆与渲染循环就是坏掉的那套；在 handler 里直接 `MessageBoxW` 会把进程吊在模态消息循环里，极端崩溃下连对话框本身都画不出来；Crashpad 对这个产品过重（ADR 0174 已排除）。
- **决定**：
  - 随包 `lens_reporter.exe`（`src/reporter/`）：纯 Win32 手绘对话框，handler 写完转储、摘要与标记后 `CreateProcessW` 拉起，崩溃路径只多一次进程创建。
  - 文案五语编译进 exe，按 `settings.json` 的 `uiLanguage` 选表；reporter 没有 Qt，lupdate 够不到它，翻译直接改 `src/reporter/reporter.cpp`——这是 `.ts` 流程记录在案的例外。
  - 按钮：打开转储文件夹、报告问题。issue URL 的构造下沉为 `util/crash` 的纯函数 `crashIssueUrl`，门面与 reporter 共用一份模板，两处不能漂移。
  - reporter 只读 marker 与 `uiLanguage`，不上传任何东西；动作之外不做别的事。
- **后果**：
  - WER 开启的机器会同时看到系统对话框与 reporter，并存可接受；CRT 漏斗路径（terminate、abort 等 `TerminateProcess` 死）没有 WER，reporter 是唯一的即时反馈。
  - 原生对话框有意不进 UI-SPEC：它是 OS 风格，不是玻璃/QML 表面，崩溃当下也载入不了 QML。
  - 摘要格式与文案有了第二处读者：格式改动同步 `util/crash` 与 reporter；文案改动同步 `.ts` 与 reporter 的语言表。
