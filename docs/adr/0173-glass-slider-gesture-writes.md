# 0173 玻璃六参数走实时投影，滑块拖动只更新它、停手后落盘

- **状态**：accepted
- **背景**：流光/雾面玻璃主题里拖动 blur、saturation、refraction、rim 四个滑条明显卡顿。实测（Debug 构树、qtest 表面树、临时探针，测完已删）一次写入 591 ms：设置映射是全局唯一的绑定依赖，一次 `settingsChanged` 会让约 20 张玻璃卡的 GlassSurface 与 GlassHost（共 40 个对象，含隐藏的）重新读整张投影——每次写入 **437 次整表重建**；Release 同场景 58.5 ms/次。另外每个停止点都同步重写整份 `settings.json`（本机约 100 KB，带解释缓存），并把 sync 调度叫醒一次。写入路径本身（文档 + 存盘 + 信号，裸 Controller）只要 0.85 ms，卡顿全在订阅侧。
- **决定**：
  - **六个键有自己的实时投影**：新增 `src/app/glass/glass_parameters.h`（`GlassParameters`，一个键一个 Q_PROPERTY 与 notify），经 `Controller.glassParameters` 暴露。GlassSurface、GlassHost、选择条的 rim（CopyDrawer）与 General 页的六个滑条都读它，不再读设置映射。
  - **滑条写入的顺序**：文档立即更新（任何直接读都看见）→ `GlassParameters` 只发发生变化那一个键的通知 → 300 ms 静默后落盘并调度一次同步。拖动期间不发 `settingsChanged`、不写盘；停手后也不补发——映射是拉取式，读时自然最新，而六个键的 UI 消费者都走实时投影。
  - **投影按变更只建一次**：`AppController::settings()` 缓存上一次构建；信号一律经 `notifySettingsChanged()` 发出——先丢弃缓存、重读六个玻璃数，再 `emit settingsChanged()`。丢弃不能挂在信号的槽里：实测发射期间 QML 的处理器与绑定重算先于 C++ 槽运行，会把旧投影读走并保持到下一次变更；手势写入路径同样先丢弃缓存，测试与 C++ 的直接读取始终最新。
  - **落盘时机交给写入方**：`KnownStore::section(name, commit)` 新重载，默认重载仍是每次写都存；退出时析构函数冲刷尚未落盘的手势。
  - **测试**：`SettingsSectionWrite.ACommitOfItsOwnDecidesWhenTheDocumentIsSaved` 钉住 “文档立即、磁盘由 commit 决定”；Phase2E 的拖动跟手、拖动不变暗、着色器 uniform、零模糊直通、活纹理，以及设置面板 General 页的用例全绿。
- **后果**：
  - 同一基准复测：Debug 591 → 0.73 ms/次（General 页打开时 0.94 ms）；Release 58.5 → 0.06 ms/次（62 → 0.07 ms）。拖动期间零整表重建、零磁盘写。
  - 拖动中崩溃或被强杀，最多丢最后一个停止点（≤300 ms）；正常退出由析构冲刷。
  - **六个数不再随 `settingsChanged` 通知**：以后要读它们的地方走 `Controller.glassParameters`，否则绑定要到下一次无关的设置变更才刷新；直接读 `Controller.settings` 仍是即时的。
  - 同类的连续写入（General 页的 Typing speed 滑条）仍在旧路径上，本卡不做；要复用这套形状，给它同样的实时投影即可。
