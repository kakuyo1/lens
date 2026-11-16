# 0184 崩溃通知收进自己的 duty，facade 只转发

- **状态**：accepted（2026-11-16）
- **背景**：架构评审 04 卡（2026-11-16）指出 `AppController` 拥有超出 QML 契约的行为：探针状态机、热键校验、门控对账与崩溃通知的启动序列都住在 facade 里，测它们要穿过整个控制器。复核后的结论是只抽崩溃通知这一块：其余几块各十余行，抽出来是薄模块，留在 facade 符合 ADR 0024 与 0173。崩溃通知不同：读标记（读句柄必须先关，Windows 才能删）、修剪转储、展示通知、再删标记的次序是 ADR 0174 的真实规则，此前没有任何离线用例覆盖这个序列。
- **决定**：
  - 新增 `CrashNoticeDuty`（`src/app/duties/crash_notice_duty.{h,cpp}`）：`reportStartupCrash(logDir)` 承接标记到通知的全部步骤，`openDumpFolder()` 与 `reportIssue()` 是两个胶囊的动作；摘要解析、转储修剪与 issue URL 仍由 `src/util/crash.h` 的纯函数提供。
  - facade 保留 QML 的三个拼写（两个 `Q_INVOKABLE` 与 `reportStartupCrash`）并转发；QML 与 `main.cpp` 的调用点零改动。
  - 通知字符串用 `QCoreApplication::translate("lens::app::AppController", …)` 显式沿用原上下文，翻译条目不迁移。
  - 探针、热键校验、门控对账显式不抽：各十余行，抽出去只是把行搬到另一个薄对象，测试面不会变宽。04 卡的其余部分到此关闭。
- **后果**：
  - 标记到通知的规则有了离线用例：有标记则两按钮通知、修剪、删标记；第二次启动无声；无标记无声且不建目录；坏标记照样通知并删除。
  - `AppController` 少两枚私有成员与约四十行行为，头文件不再包含 `util/crash.h`。
  - 两个动作本身（打开资源管理器与浏览器）仍不可离线断言，属人工验证；URL 的构造已在 `crash_test.cpp` 中钉住。
