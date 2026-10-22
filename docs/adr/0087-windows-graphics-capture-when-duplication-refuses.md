# 0087 复制拒绝的屏改走 Windows Graphics Capture：内屏的幕布第一次跟上合成器

- **状态**：accepted（阶段 5 的延伸；依赖 ADR 0072、0073、0074、0086；扩展 ADR 0073 / 0074 的“只有可复制才能快”）
- **背景**：
  - 内屏 `B156HAN15.H` 在所属适配器上也拒绝 DXGI 桌面复制（`DXGI_ERROR_UNSUPPORTED`，ADR 0072）。玻璃在没有复制的屏上
    只剩 GDI：一次读 11~25 ms，smooth 档实测每 10 秒发布 60~96 帧（约 6~10 次/秒），后景明显卡顿；只有模糊与折射都为 0
    的 passthrough（ADR 0069）才跟得上桌面。
  - Windows.Graphics.Capture（WGC）从合成器取同一块屏的画面，不要求该输出可复制。同一台机器、同一块内屏实测可开会话并
    连续取帧；外接屏的复制路径不受影响。
- **决定**：
  - 新增第四个桌面源 `GlassCapture`（`src/app/glass/glass_capture.h`），实现同一条 `DesktopSource` 缝（ADR 0086）。
    每块屏的顺序固定为 **DXGI 复制 → WGC → GDI**；`FallbackSource` 改为有序列表，只有全部源都拒绝才把 `refused` 上抛。
    `canDuplicate()` 改名 `canFollow()`：它问的是“能不能跟帧”，不再是“能不能复制”。
  - **设备建在扫描输出的 GPU 上**：混合显卡机器上显示器可能由核显驱动，而 DXGI 把输出列在独显下；WGC 的设备若在另一块
    GPU，每帧经系统内存拷一次。内屏实测 81 次/秒（设备在 4060）对 130 次/秒（设备在核显）。用
    `QueryDisplayConfig` 的 target `adapterId` 找到真正扫描输出的适配器。
  - **关掉帧间隔下限**：Windows 11 24H2 的捕获会话默认约每三帧才送一帧，144 Hz 面板实测只有 48 次/秒；
    `IGraphicsCaptureSession5::MinUpdateInterval = 0` 后到 130 次/秒（合成器自身约 131 Hz）。该接口是 24H2 的，
    更早的系统保留默认值——48 次/秒仍远好于 GDI。
  - **四个帧池缓冲**：处理器每画一帧时，帧池要留出后续合成的位置。实测两缓冲 95 次/秒、三缓冲 107、四缓冲 130。
    代价是会话打开期间四张屏幕大小的 BGRA 缓冲。
  - 光标与系统捕获黄框都关掉（`IGraphicsCaptureSession2` / `3`）；两个接口任一不在（Windows 10）就拒绝 WGC，回 GDI，
    不在读者屏幕上画黄框。
  - 旋转输出与 HDR 帧拒绝：`item.Size()` 与桌面矩形不一致、或帧格式不是 BGRA/X8 时不接，仍交给复制与 GDI。
    WGC 没有脏矩形信息，因此每一帧都发布并跳过指纹（`shortcut:` 注释）；先有平滑，再按需要加区域判断。
  - WGC 只用于自动与流畅两档；标准档仍是 GDI 的 15 次/秒，省电档仍不 live。源顺序与档位判断在
    `GlassBackdrop::sessionFor()` 与 `updateLive()`。
- **后果**：
  - 真机（Release，内屏 1920×1080@144，流光，模糊 40、折射 20）：smooth 档 10 秒 tally 从 `publishes 60~96` 到
    **`publishes 950~1070`（95~107 次/秒）**，帧拷贝中位 1.0 ms（GDI 读中位 11~25 ms）；帧池四缓冲、设备在核显、
    下限关掉时到达 130 次/秒。黄框与光标均未出现；退出应用无崩溃（会话在读取线程上关闭）。
  - 外接屏仍走 DXGI 复制，行为与 ADR 0074 一致；Windows 10 上两个会话接口缺失，仍全部走 GDI。
  - 仍是合成器的产帧率上限：ADR 0074 在外接屏记录约 90 帧/秒，内屏的合成器约 131 Hz；玻璃不会超过它。剩下的每帧
    约 9 ms 花在纹素上传与场景图，是下一处该削的成本。
  - 新增 `glass_capture_test.cpp`（人跑，`LENS_GLASS_WGC_TEST=1`）：逐屏开会话、读一帧、打印尺寸与帧率；旋转外接屏
    也确认 `item.Size()` 与桌面矩形一致，identity 假设成立。`glass_source_test.cpp` 增加三源顺序的两例。
- **被排除的选项**：
  - **WGC 永远优先**：外接屏的 DXGI 复制已有实测（87~88 次/秒）与脏矩形门，WGC 没有后者；只在复制拒绝时才换。
  - **为无黄框走 `GraphicsCaptureAccess` 清单能力**：本应用是非打包的 Win32 安装版；`IsBorderRequired(false)` 在
    Win11 上直接生效，Windows 10 则没有该接口。为一个系统版本引入打包能力不划算，直接回 GDI。
  - **继续用 GDI 提频**：GDI 一次读 11~25 ms，提到 30 次/秒就要单核三成以上，而且仍不到 60；采集源换掉才是解。
  - **这一轮把纹理直接交给场景图**：`QRhiTexture::createFrom(NativeTexture)` 仍在私有头里（ADR 0074 的结论未变），
    且跨设备共享与场景图线程的生存期要先解决。
