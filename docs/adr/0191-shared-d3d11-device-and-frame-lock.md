# 0191 共享一个 D3D11 设备：帧锁串行化渲染，释放策略照旧

- **状态**：accepted（2026-11-17）
- **背景**：soak 每轮开合稳定爬升约 33 个句柄，2026-11-10 的 `!htrace` 归因（`docs/metrics/resource-footprint/handle-types-2026-11-10.md` §4.4）把三分之二钉在 `amdxx64!AmdD3D11CreateDeviceAndSwapChainExt`：threaded 渲染循环每窗建一个 D3D11 设备，AMD 驱动在设备销毁后不关那组 Event。同一次重建还付约 800 ms 与每窗约 75 个驱动线程。应用侧无泄漏可修，只能不让设备反复 birth/death。
- **决定**：进程建一个 BGRA 的 `ID3D11Device`（`SharedGraphics`，`src/app/main.cpp`），每窗场景图初始化前 `setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(...))` 采纳它；QRhi 包装仍每窗一个、隐藏照旧 `releaseResources()` 销毁（导入设备不被 `QRhiD3D11::destroy()` 释放，已核源码）。immediate context 只有一个且 D3D11 只保证逐调用线程安全，故每窗 `beforeFrameBegin` 锁、`afterFrameEnd` 解锁一把共享 mutex，把帧序列串行化在渲染线程之间，GUI 线程不被渲染阻塞（threaded 循环保证这两信号成对）。后建窗口靠 qApp 的 `ChildAdded` 过滤器补 tune（QML Window 是 `QQuickWindowQmlImpl : QQuickWindow`，Controls 弹窗运行时建窗，同走此钩）。device lost 时循环自己的重试只会再采纳已丢失的设备，故 `sceneGraphInvalidated` 检出 `GetDeviceRemovedReason() != S_OK` 后队列到 GUI 线程重建设备、逐窗重采纳；可见窗先 hide 再 show 让渲染线程停泊，TDR 时一次闪烁。
- **后果**：同构建同口径 25 轮对照，句柄斜率 +33/轮 → 0（预热后 4535 → 4531），线程 129 到 135 平稳，内存 337 到 371 MB 往返（释放未回归），round_ms 中位 1635 → 约 1040，命令拒绝 3 → 0；鼠标探针 25 s 最差 5.0 ms、0 停顿。light 主题共享组与 `LENS_KEEP_GRAPHICS` 对照组同显帧逐像素一致，liquid 加宠物目视正常。`LENS_KEEP_GRAPHICS=1` 语义不变：跳过全部 tune，回到每窗设备，作对照。代价与边界：多窗同显的渲染被帧锁串行（典型负载是宠物加一张面板，未测满七窗同显帧率）；TDR 恢复有一次闪烁且恢复路径未真机触发过；release 构建复测与周级 soak 外推留待发布前。玻璃自有的每屏捕获设备独立于此，不变（ADR 0087）。
- **被排除的做法**：
  - basic 渲染循环（`QSG_RENDER_LOOP=basic`）：QQuickWindow 文档称其共享设备，实测否决——Qt 6.9 的 `qsgrenderloop.cpp` 明写 basic 循环 “Uses one QRhi per window”，且其 `releaseResources()` 只清缓存不销毁设备；25 轮 soak 线程 86 → 670、内存平在 938 MB、句柄仍 +26/轮、round_ms 约 3 倍。文档那句是 Qt 5 语义。
  - `fromRhi` 共享 QRhi 对象：QRhi 非线程安全，等价强制单线程渲染，比 basic 多一层机械而无收益。
  - 每窗设备永不释放：470 MB 与 560 线程常驻，违反托盘常驻的内存预算，已被 keep-graphics 对照测量否定。