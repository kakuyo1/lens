# 0040 窗口排除抓屏时，玻璃幕布约每秒 15 次重抓：GDI 在工作线程上读，模糊在 1/4 分辨率上做

- **状态**：accepted (补充 ADR 0039：幕布在拖动之外也会刷新；ADR 0039 的拖动方案不变)
- **背景**：ADR 0039 的幕布抓一次就固定。窗口背后有视频时，玻璃里的画面不动，与 `UI.md` 4.4 “背后显示的是窗口背后的桌面” 不符。要跟上视频，就得周期重抓。
  本机是双显卡笔记本 (AMD 核显驱动内屏，RTX 4060 驱动外接屏)。DXGI Desktop Duplication 对内屏的 `DuplicateOutput` 返回 `DXGI_ERROR_UNSUPPORTED`：输出不属于创建设备的那块适配器。Windows.Graphics.Capture 去掉黄框要包清单里的 `graphicsCaptureWithoutBorder` 能力，本应用是 Inno Setup 安装的非打包程序，拿不到。只剩 GDI。
  实测 (离线探针，未在应用内测)：GDI 从桌面读 1075 × 1231 设备像素，中位 13.4 ms；直接缩到 1/4 再写出，中位 14.2 ms，读桌面才是主要开销。模糊在全分辨率上 41.6 ms，1/4 分辨率上 2.1 ms。
- **决定**：
  - 窗口排除抓屏 (ADR 0038)、玻璃主题开启、窗口可见时，`GlassBackdrop::setLive(true)` 启动 67 ms 定时器，约 15 fps。`captureBackdrop` 在排除分支调用它；`clear()` 停止它，玻璃关闭、窗口隐藏、切换主题都会走到 `clear()`。
  - 每次刷新在线程池 (单线程) 上用 GDI 读取幕布区域，`StretchBlt` 的 `HALFTONE` 直接缩到 1/4，再发布。同时只允许一次读取在途；tick 撞上在途读取时跳过，不排队。
  - 读取与发布分开。读取在工作线程上做，只用 GDI 与 `QImage`，不碰 Qt 对象；发布经 `QueuedConnection` 回到 GUI 线程。`generation_` 计数每次幕布重取或清除时递增，过期的读取结果被丢弃。
  - 坐标：`toNativeRect` 把逻辑矩形从屏幕自身的角点起算，乘该屏的 DPR，原点取自 `GetMonitorInfoW` (经 `QNativeInterface::QWindowsScreen`)。不用 `QHighDpi`，它在私有头里。
  - 文字色在每次刷新时重新判定，因为背后的视频会改变对比。
  - 非 Windows 与离屏平台不读桌面，幕布是黑的。离屏测试只断言状态与 serial，不读像素，也不启动刷新。
  - 1/4 分辨率的幕布在 QML 里仍按幕布的逻辑尺寸铺开，`shader` 按归一化坐标取样，不需要改着色器。
- **后果**：
  - 估算的 CPU 占用：读取约 21%(每秒 15 次，每次 14 ms)，模糊约 3%(每次 QML 请求 2 ms)。约 0.25 个核。这是离线探针的数字，未在应用中测。
  - 被排除的选项：DXGI Desktop Duplication (本机内屏不可用)；Windows.Graphics.Capture (黄框需要打包)；全分辨率模糊 (约 42 ms，15 fps 下约 0.9 核，负载高时会跌破帧率)；`QScreen::grabWindow` 放到工作线程 (它只能在 GUI 线程上用，且返回 `QPixmap`)。
  - 未做：空闲时暂停、按负载退帧、跨屏幕的幕布 (窗口跨屏时仍退为透明，同 ADR 0039)。系统拒绝排除 (Windows 10 2004 之前) 时不刷新，仍走 ADR 0038 的透明回退。
  - 隐私边界不变 (ADR 0034)：画面不落盘、不写日志、不上传。刷新让 “画面” 从每分钟一次变成每秒十几次，日志里也不能带像素。
  - 用户 2026-10-13 在真机 (release 构建) 上看过，接受当前效果。未测：内屏与外接屏的坐标映射；DPI 125% 下幕布与窗口的对齐；GDI 读到的颜色是否与屏幕显示一致 (颜色空间未核对)，玻璃是否发灰或发暗；每秒 15 次的 QML 图片加载，内存是否回落。见 `TODO.md`。
