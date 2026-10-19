# 0072 能复制的输出从桌面复制取帧：逐屏回退 GDI，帧到达才发布

- **状态**：accepted（阶段 4；GDI 屏仍按 ADR 0040 的 15 次/秒预算，可复制的屏改由 ADR 0071 的活纹理交付）
- **背景**：
  - 阶段 0 的探针证明采集能力按输出而不是按机器：本机内屏在所属适配器上返回 `DXGI_ERROR_UNSUPPORTED`，外屏可复制，
    实测 137.5 帧/s 到达，幕布大小的区域复制并 Map 回 CPU 中位 0.75 ms，整屏 1.90 ms
    （`docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md` 3.1、3.2）。
  - 阶段 3 之后帧率仍由 67 ms 的 GDI 读取循环限制（同上 3.8）。GDI 的价随面积走，约 8 ms 固定底加每 Mpx 15 ms
    （同上 3.6），60 次/秒要三成到四成单核；再快的路只有把采集搬到 GPU。
  - 把复制到的纹理直接交给 Qt 场景图需要 `QRhiTexture::createFrom`，它在私有头里，与 ADR 0040 的 “不用私有头”
    相冲突。拷回 CPU 是公开 API，探针已量过，0.75 ms 对幕布够用。
- **决定**：
  - 新增 `GlassDuplication`（`src/app/glass_duplication.h`）：每块屏一台 D3D11 装置，建在拥有该输出的适配器上；
    `DuplicateOutput` 之后只读幕布那块区域，经默认纹理与 staging 纹理拷回 CPU 的 `QImage`，仍交给
    `GlassSheetItem` 的活纹理，交付路径不变。GDI 读保留为每块屏的回退，也是首张幕布与拖动重取的路径：首图在 GUI
    线程上马上要有，拖动重取已在工作线程上，两者都是一次读。
  - 失败逐屏、可恢复：`DXGI_ERROR_UNSUPPORTED` 记为永久（本机内屏就是这一条）；`ACCESS_LOST`、别的录屏占用、
    模式切换与找不到输出记 5 s 冷却后重试；输出格式不是 BGRA（HDR 桌面）也回退。回退的屏不改变 GDI 的共享预算。
  - 帧驱动：有可用复制的屏把 live 定时器提到 16 ms（约 62 次/秒轮询）。`AcquireNextFrame(0)` 在没有新帧时立即返回；
    有新帧时先用 `AccumulatedFrames`、`GetFrameMoveRects`、`GetFrameDirtyRects` 判断幕布是否被改到，没有就释放且不拷贝。
    静止的幕布因此一次也不拷贝。拷贝后的指纹门仍在，低于它的量化阈值的细微变化不会发布（ADR 0041）。
  - 会话的第一帧丢弃：旋转屏上拿到的首帧是黑的（实测），而卡片已有 GDI 的首张幕布；第二帧起才是会话看着桌面画出来的。
  - 旋转按取得纹理的真实尺寸与方向处理：帧尺寸取纹理描述（本机外屏 2560 × 1440），不取
    `DXGI_OUTDUPL_DESC.ModeDesc`（1440 × 2560，桌面方向）；`DXGI_OUTDUPL_DESC.Rotation` 是 “把帧转回桌面” 的方向，
    与本模块的 `ScreenRotation`（桌面转成帧的方向）相反，取反后映射（本机 270 → 90）。
  - 帧率上限先定在 60 次/秒。匹配 144 Hz 要免掉 CPU 拷贝与旋转、把纹理直接交给场景图，那是私有头那条路，留给以后。
- **后果**：
  - 真机（Release，2026-10-20 00:52~00:55；流光主题，设置面板移到外屏 `\\.\DISPLAY6`，1440 × 2560 @144 Hz 竖放，
    排除抓屏，独立设置文件）：
    - 静止幕布 10 s：`ticks 612`、`frames 1`、`noFrame 608`、`publishes 2`、`sameSkips 1`；只在窗口移动后的首帧拷贝一次。
    - 后景变色 10 s：`frames 503`（50.3 次/秒，后景自身的产帧率）、`noFrame 120`、`publishes 335`、`sameSkips 168`；
      `frame copy` 中位 4.5 ms、p95 6.7 ms、最差 8.5 ms。
    - 黑白交替 10 s：`frames 283`、`noFrame 341`、`publishes 204`、`sameSkips 79`；`frame copy` 中位 5.0 ms。
    - `ffplay` 播 1280 × 800 @60 帧的 `testsrc2`、窗口大半被设置面板遮住：10 s 内 `frames 256`（25.6 次/秒）、
      `noFrame 249`、`publishes 230`（其中 12 次是移动后的 GDI 首图）、`sameSkips 38`，`frame copy` 中位 4.6 ms。被遮挡的播放器可能被 DWM 节流，
      这条既不能证明也不能否证 “60 帧的真实后景能到 60”；它只说明 16 ms 轮询不是眼前的瓶颈。
    - 4.5~5.0 ms 是 “复制 + Map + 建 QImage + 四分之一转” 的整段，不是探针的 0.75 ms（那只是复制 + Map）。60 次/秒
      约 27%~30% 单核。这是下一处该削的成本，不是帧率上限的来源。
  - 内屏在这台机器上永远回退 GDI：同一个应用里一块屏保持 15 次/秒，另一块跟随后景。回退是常态，不是异常。
  - 首帧丢弃让复制屏的第一张图仍来自 GDI；静止桌面上复制会话不发布任何帧。窗口移动后的新幕布、拖动中的重取也走 GDI。
  - 内存未单独测：每块可复制屏多一台 D3D11 装置、两张幕布大小的纹理（默认 + staging）与元数据缓冲。WARP、RDP 与
    远程会话未测。
  - 真机用例：`GlassDuplication.CropFollowsTheRotatedScreen`（`test/googletest/integration/glass_duplication_test.cpp`）
    在每块屏上画一个四色象限窗口，从复制帧里读回：本机 DISPLAY6 四象限全对，与 GDI 同区域逐点比较 100% 一致；
    DISPLAY1 复制被拒，如实打印并留在 GDI。离线 `GlassDuplication.*` 五例钉住四种旋转的矩形映射。全量单测
    584 通过、1 跳过。
- **被排除的选项**：
  - 私有头 `QRhiTexture::createFrom`：违反 ADR 0040 的政策；本轮先用公开 API，代价是 4.5 ms 的整段。
  - 整屏缓存再裁：整屏拷回 1.90 ms × 60 约 11% 单核，每屏还多 14.7 MB；只拷幕布更省。
  - 每屏一条阻塞等待帧的线程：多线程与唤醒换来的只是首帧延迟，16 ms 轮询已足够，且只有一个工作线程。
  - 一个机器级的 “硬件加速” 开关：可用性逐屏，见探针 3.1。
