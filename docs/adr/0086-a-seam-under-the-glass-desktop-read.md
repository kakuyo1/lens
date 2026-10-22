# 0086 桌面读取有一条缝：GDI 与复制是同一接口的两个适配器

- **状态**：accepted（架构评审候选 4 “A seam for the desktop” 的落地；行为不变）
- **背景**：
  - `takeSheet()` 自己决定哪条路回答：`Request.duplication` 非空就走桌面的复制帧，空就走 GDI，
    而 “测试平台不读真实屏幕” 又借一个 `Request.onDesktop` 的 bool 混在同一条判断里。于是 “复制 → GDI → 什么都没有”
    这个回退顺序只能靠拼出幕布、窗口和 QML 表面才观察得到，单元测试够不着。
  - 825 行的 `lens_gtest_glass_probe` 为了量这些数，自己又写了一遍适配器枚举、`DuplicateOutput`、
    `AcquireNextFrame` 与幕布拷贝框；它打印的数与应用付的数来自两份代码。探针的拷贝框还按桌面方向算，
    在旋转屏上与 `frameRectFor()` 的映射不一致。
  - `GlassDuplication` 本身已经很深，`Read{newFrame, usable, copyMs}` 就是这条缝该有的形状；
    问题不在它内部，而在调用方必须自己拼顺序。
- **决定**：
  - 新增 `src/app/glass/glass_source.h`/`.cpp`：`DesktopSource` 是唯一的接口——
    `read(native, scale, timeoutMs)` 与 `canDuplicate()`；`DesktopRead` 载着画、`newFrame`、`refused`、`usedGdi`、`copyMs`。
  - 三个适配器：`GlassDuplication` 直接实现 `DesktopSource`（`usable()` 改名 `canDuplicate()`）；
    `GdiSource` 是合成根按平台构造一次、读 `readDesktopRegion()` 的适配器，收缩比例由构造参数决定；
    `FallbackSource` 是唯一知道顺序的地方——首选可用就读首选，`refused` 才读回退，并把 `refused` 保留给幕布。
    测试里的伪源当作第三个适配器，不需要桌面。
  - `GlassBackdrop::Request` 去掉 `duplication` 与 `onDesktop`，只带一个 `DesktopSource*`。首张幕布与拖动重取
    仍走 `GdiSource`（首图要立刻有，复制会话的第一帧本来就要丢）；live 刷新走该屏的 `FallbackSource`。
    每屏一个 `Session{duplication, chain}`，`chain` 持有 `duplication` 与共享 `GdiSource` 的引用。
  - `refused` 沿读取结果上行：完成回调据此调用 `noteDuplicationRefused()`，GDI-only 的读（标准档、非 Windows）
    不会因为一次无关的读取把设置页那条说明清掉。
  - 探针改为链 `lens_app`，通过 `GlassDuplication` 与 `GdiSource` 量同一批数：帧到达率、一帧经应用整段拷回的
    开销、GDI 全分辨率与 1/4 的对照、抓屏排除。复制整屏、取到纹理能否 Map/共享、以及 “换一块适配器再复制”
    这三项是 2026-10-19 那轮探针回答过的问题，结论在 ADR-0072 与
    `docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md` 里；它们需要原始纹理，跨不过这条缝，
    因此从探针删除，而不是再留一份第二个读取路径。
- **后果**：
  - 回退顺序第一次可离线断言：新增 `glass_source_test.cpp` 5 例——GDI 档位与预算、首选新帧不碰回退、
    被拒时回退并保留 `refused`、没新帧不算回退、永久不可用的会话不再重试。
  - 探针从 825 行降到约 390 行；幕布拷贝框、`StretchBlt`、设备/适配器配对全部删除，旋转屏上的裁剪从此
    只有 `frameRectFor()` 一份。代价是探针现在链 `lens_app`，运行需要 Qt DLL 在 `PATH` 上。
  - `Request` 少一个字段（`duplication` + `onDesktop` → `source`）；调用方不再同时知道 “有没有复制” 和 “是不是桌面”。
  - 统计与文字色调仍留在 `takeSheet()`：适配器只报一段耗时与来源，不碰 `GlassStats`，伪源也不会污染应用的计数。
  - 行为不变：live 屏仍先复制、回退 GDI，GDI 屏仍共享 15 次/秒预算，首图与拖动仍走 GDI。
- **被排除的选项**：
  - 只把 `Request` 的 `GlassDuplication*` 换成接口指针、顺序仍写在 `takeSheet()`：回退仍然测不了，
    探针也仍然得自己拼。顺序是这次要收的东西，必须有自己的名字。
  - 让 `GlassDuplication` 自己拥有 GDI 回退：复制适配器会同时依赖 GDI，两个适配器就不再能独立替换，
    “首选/回退” 也无法用两个伪源测试。
  - 让适配器直接写 `GlassStats`：统计是应用级横切计数，写进去会把探针和单测的读数混进应用日志；
    耗时随 `DesktopRead` 返回，由调用方记账。
  - 把 `DesktopSource::read` 改成返回 `std::variant<Frame, NoFrame, Refused>`：三态已经由
    `image/newFrame/refused` 三个 bool 表达，且调用点各只问其中一条，variant 只会让调用方多一层解包。
