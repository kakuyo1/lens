# 表面窗口的内存占用：优化方案

> 数据来源：`docs/metrics/resource-footprint/glass-memory-2026-10-18.md`。这份文档只讲怎么改、按什么顺序改、
> 每步怎么验。

## 1、目标

| 指标 | 现在 | 目标 | 判定方式 |
| --- | ---: | ---: | --- |
| 空闲私有内存（托盘 + 宠物） | 250 MB | ≤ 200 MB | 启动后静置 10 分钟取末点 |
| 用完 5 个表面之后 | 967 MB | ≤ 350 MB | `exp_two.ps1` 扩展到 5 个表面 |
| 线程数 | 164 → 804 | ≤ 250，且不随使用增长 | 采样 CSV 的 `threads` 列 |
| 句柄数 | 4,501 → 7,768 | 关闭后回落 | 采样 CSV 的 `handles` 列 |
| 单表面常开 10 分钟 | +3 MB | 不劣化 | `exp_soak.ps1 -Mode soak` |

## 2、已经测出来的三件事

1. **每个窗口第一次上屏，永久多约 80 线程、1,630 句柄、60~100 MB，隐藏后一分不退**。小到几百像素的选区
   动作条与整块设置面板同价，所以成本按窗口算，不按内容大小算。
2. **玻璃只占 0~23 MB**。`theme=light` 对照下开设置窗是 +80.7 MB / +79 线程，`liquid` 是 +80.0~103.3 MB /
   +79 线程，线程与句柄几乎一致。
3. **每秒 15 次的重拍不涨内存**。常开 8 分钟只 +3.0 MB，线程反而降 4 个。它是 CPU 问题，不是内存问题。

## 3、地基：Qt 的默认值是 “不释放”

`QQuickWindow::setPersistentGraphics()` 的文档原文：

> Sets whether the graphics resources (graphics device or context, swapchain, buffers, textures) should be
> preserved, and cannot be released until the last window is deleted. **The default value is true.**

`releaseResources()` 的文档原文：

> Requests the scene graph to release cached graphics resources (pipelines, shader programs, image data). May
> also release the whole scene graph and window rendering resources. Behavior is controlled by
> `setPersistentGraphics()` and `setPersistentSceneGraph()`.

合起来读：**应用有释放的入口，但默认配置把它关着**。十二个窗口常驻、靠 `visible` 开关，于是每个窗口第一次
上屏拿到的整套驱动资源（一个 D3D11 设备、一条交换链、缓冲与纹理，以及驱动为它们起的线程池）一直保留到进程退出。

这条把方案从 “重构窗口模型” 拉回到 “改两个开关”，也解释了为什么第 2 节那个成本是 “每窗口一份、隐藏不退”，
而不是 “用一次漏一次”。

## 4、候选方案对比

| 方案 | 机制 | 改动面 | 预期收益 | 风险 | 结论 |
| --- | --- | --- | --- | --- | --- |
| **A 关持久化 + 隐藏时释放** | `setPersistentGraphics(false)`、`setPersistentSceneGraph(false)`，隐藏时 `releaseResources()` | C++ 约 25 行，一处调用 | 若释放的是整设备：967 → ~330 MB，804 → ~244 线程 | 低。重开要重建场景图，首帧可能闪一下 | **先做这个** |
| B 共享一个 QRhi | `QQuickGraphicsDevice::fromRhi()` 喂给十二个窗口 | C++ + 每个窗口一次调用 | 线程恒定 ~164，与窗口数解耦 | 中高。文档明确写 “只在不破坏图形 API 线程与像素格式规则的前提下共享”，且必须在上屏前调用 | 若 A 只释放一半再评估 |
| C 按需创建、隐藏即销毁 | `Main.qml` 十二个窗口改 `Loader` + `destroy()` | QML 约 300~500 行，牵动开合、定位、动画、`dismissOutside` | 彻底 | 高。且第 3 节的文档说资源保留到**最后一个**窗口销毁，收益存疑 | 不作为首选 |
| D 单窗口 + 内嵌面板 | 所有面板变成同一窗口里的 `Item` | 最大，ADR 0038/0041 要重写 | 最彻底 | 高。失去系统级置顶、抓屏排除、任务栏分离 | 兜底 |

C 之所以从首选降级：如果十二个窗口共享同一个图形设备，销毁其中一个不会释放设备，只是释放它自己的场景图与
交换链。收益可能只有玻璃那 0~23 MB。这一点用阶段 0 的探针能直接看出来。

## 5、分阶段计划

### 阶段 0：先验释放语义（半天）

不改应用。探针已经写好：`.workbuddy-ai/tools/qwindow-probe/`。

```powershell
# 关掉持久化，隐藏时 releaseResources()
& .\.workbuddy-ai\tools\qwindow-probe\build.ps1

# 对照组：Qt 默认配置
& .\.workbuddy-ai\tools\qwindow-probe\build.ps1 -QtDefaults
```

探针建两个带 200 行列表的窗口（与面板的场景量级相当），依次打印 “起窗 1 / 起窗 2 / 隐藏并释放 / 销毁” 四个
时刻的线程、句柄、私有字节。

**通过标准**：`second hidden (+release)` 那一行，线程比 `second window shown` 少 60 以上，句柄少 1,200 以上。
对照组应当看不到回落。

**这一步的结论决定后面走哪条**：

- 回落 → 走阶段 1。
- 不回落，但销毁那一步回落 → 说明设备是按窗口的，只是 `releaseResources()` 不够，改走 C。
- 都不回落 → 设备在进程级共享，改走 B 或 D。

> 这台机器的沙箱不让 `cl.exe` 起 `reg.exe`，探针在这里编译不了，需要在普通终端跑一次。

### 阶段 1：应用内关掉持久化，隐藏时释放（1 天）

**改哪里**：`src/app/main.cpp:274` 的 `engine.loadFromModule("Lens", "Main");` 之后、事件循环之前。

```cpp
/// Hand a hidden surface's graphics resources back.
///
/// QQuickWindow defaults to setPersistentGraphics(true): the device, the swapchain, buffers and
/// textures are preserved until the last window is deleted, so a hidden window still holds a whole
/// driver resource set (measured: about 80 threads, 1,600 handles, 60~100 MB).
/// Turning both persistence hints off and calling releaseResources() on hide is what gives it back.
void releaseHiddenSurfaces(QObject* root)
{
    auto tune = [](QQuickWindow* w) {
        w->setPersistentGraphics(false);
        w->setPersistentSceneGraph(false);
        QObject::connect(w, &QWindow::visibleChanged, w, [w] {
            if (not w->isVisible())
                w->releaseResources();
        });
    };
    if (auto* self = qobject_cast<QQuickWindow*>(root))
        tune(self);
    for (auto* w : root->findChildren<QQuickWindow*>())
        tune(w);
}
```

调用点：

```cpp
engine.loadFromModule("Lens", "Main");
if (engine.rootObjects().isEmpty())
    return 1;
releaseHiddenSurfaces(engine.rootObjects().constFirst());
```

**两个要注意的地方**：

- `setPersistentGraphics` 只在场景图初始化之前有效。启动时已经可见的窗口（宠物、根窗口）可能赶不上，
  这没关系：它们是常驻的，本来也不该释放。其余十个窗口在第一次显示前就已经设好。
- `releaseResources()` 是排队到渲染线程的请求，不是同步调用。隐藏后要等一两帧才看到数字变化，
  采样间隔 10 秒足够。

**验证**：`exp_two.ps1`（开设置窗 → 关 → 开动作条）。看 “关掉设置窗” 那一行是否回到 250 MB / 164 线程附近。

**回滚**：把调用点注释掉即可；或加一个 `LENS_KEEP_GRAPHICS=1` 的开关跳过。

### 阶段 2：玻璃纹理随表面关闭释放（1 天）

> **实施结论：做了，实测零收益，已回退。**只放玻璃纹理、窗口资源保持默认时，关闭 15 秒后仍是 353.4 MB
> （打开时 353.9），VMMap 里那批 27~31 MB 的块一个没少 —— 它们是设备与交换链，属于阶段 1 的账。
> 细节见第 10 节。

阶段 1 释放的是窗口级资源（设备、交换链）。玻璃的三块 `ShaderEffectSource` 是 item 级资源，要单独放。

**改哪里**：`src/app/qml/components/GlassSurface.qml` 加一个函数，`GlassHost.qml` 的 `!visible` 分支调用。

```qml
// GlassSurface.qml
/// Hand the three offscreen textures back while the window is hidden. The ShaderEffectSource's
/// FBO is only released once Image.source is cleared.
function release() {
    first.source = "";
    second.source = "";
    displacement.source = "";
    firstTexture.sourceItem = null;
    secondTexture.sourceItem = null;
    displacementTexture.sourceItem = null;
}
```

```qml
// GlassHost.qml:48, the branch that already drops the sheet
if (!host.glass || !host.window.visible) {
    host.sheet.clear();
    surface.release();          // new: `surface` is the GlassSurface
    return;
}
```

**验证**：`exp_soak.ps1 -Mode cycles -Cycles 10`。每次关闭后应回到同一水位；VMMap 里那批 28~32 MB 的私有块
应随关闭消失。

**风险**：重开时要重建纹理，首帧会晚一帧。现有两槽轮换逻辑本来就是为 “新幕布就绪前不露底” 设计的，相容。

### 阶段 3：折射贴图多槽缓存、模糊复用暂存区（1 天）

> **实施结论：未做，依据见第 10 节。**阶段 1 之后堆不再无界增长（10 次开合的关闭水位稳定在 261~272 MB），
> 这一节的前提 “堆从 119 涨到 431” 已被证伪 —— 涨的是每个窗口自己的场景图。剩下的分配抖动由阶段 4 从源头
> 掐掉：桌面没变时根本不再请求模糊。真要做，收益在 CPU 与分配器压力，不在内存。

这两处是堆从 119 MB 涨到 431 MB、段数从 42 涨到 125 的直接来源。

**改哪里**：

- `src/app/glass_backdrop.cpp:432` 的 `cachedMap()`：单个 `mapKey_` 槽换成按同一字符串键的 `QHash<QString, QImage>`，
  上限 15 条（一个表面一条），超出按最近最少使用淘汰。
- `src/app/glass_math.cpp:113` 的 `blurredPlanes()`：三个 `std::vector<float>` 平面改为 provider 级复用缓冲，
  每次只 `resize` 不重新分配。注意 `requestImage` 可能在图片加载线程上被调用，复用缓冲要加锁。

**验证**：VMMap 的 Heap committed 与段数在 “开合 10 次” 前后不再增长。

**风险**：低。纯缓存层，语义不变。锁的粒度要覆盖整个 `blurredPicture` 调用。

### 阶段 4：没变化就不重拍（半天）

**改哪里**：`GlassSheet::publish()` 之前加一层比对：把新幕布降采样成指纹（例如 32×32 的灰度），与上一次相同就
直接返回，不递增 `serial`、不发 `pictureChanged`。

**验证**：静止桌面与播放视频各 5 分钟，数发布次数（临时日志，验完删掉）。

**收益**：这条不解决内存（实测常开是平的），解决 CPU 与分配 churn。ADR 0040 估的是约 0.25 个核。
`TODO.md` 里 “玻璃主题的实时性能优化” 那一条可以与这一步合并。

### 阶段 5：只有在阶段 1 收益不足时才做（2~3 天）

按阶段 0 的结论二选一：

- **B 共享一个 QRhi**：`QQuickWindow::setGraphicsDevice(QQuickGraphicsDevice::fromRhi(rhi))`，必须在每个窗口
  上屏前调用。需要先拿到一个 `QRhi`（第一个窗口的 `rhi()`），并且要验证线程模型与像素格式兼容。
- **C 按需创建**：`Main.qml` 的十二个窗口改 `Loader`。这会牵动 `hidePanels`、`placePanel`、`dismissOutside`
  三处按对象访问的代码，以及各表面的位置记忆。

## 6、验证矩阵

| 阶段 | 跑什么 | 看什么 | 通过标准 |
| --- | --- | --- | --- |
| 0 | `qwindow-probe/build.ps1` | `second hidden (+release)` 那行 | 线程 −60 以上，句柄 −1,200 以上 |
| 1 | `exp_two.ps1` | “关掉设置窗” 那行 | 回到 250 MB / 164 线程附近 |
| 1 | `exp_soak.ps1 -Mode both` | 10 次开合 | 每次关闭后同一水位，累计涨幅 ≤ 20 MB |
| 2 | `exp_soak.ps1 -Mode cycles` | VMMap 的 28~32 MB 块 | 关闭后消失 |
| 3 | 同上的 VMMap 快照 | Heap committed 与段数 | 开合前后不增长 |
| 4 | 静止 5 分钟 vs 视频 5 分钟 | 发布次数 | 静止时接近 0 |
| 全部 | `threadmap.py` | 线程归属 | 驱动线程 ≤ 200 |

`TEST.md` 的分级照旧：C++ 逻辑改动跑一条相关 gtest；QML 可见表面用 `ui-run.ps1` 抓一张图；跨目标的合并才跑全量。

## 7、风险与对策

| 风险 | 表现 | 对策 |
| --- | --- | --- |
| 重开表面时闪一下 | 场景图重建，首帧空白 | 出现动画本来就有淡入；若明显，改为延迟 500 ms 释放，而不是立刻 |
| 释放时机与抓屏排除冲突 | `GlassHost.capture()` 在隐藏时已经 `sheet.clear()`，两者顺序要对 | 先 `clear()` 再 `releaseResources()` |
| `releaseResources()` 频率过高 | 频繁开合导致反复重建，CPU 抖动 | 隐藏后延迟 500 ms 再释放，取消则跳过 |
| 阶段 1 收益只有一部分 | 线程降了但内存没降 | 按阶段 0 的结论转 B 或 C，不要叠加改 |
| 多屏与 DPI | 释放后重建的交换链尺寸不对 | 用现有 `keepOnScreen` / `checkScreenLayout` 的路径复测两种缩放 |

## 8、不做的事

- **不改每秒 15 次的刷新率**。它是 CPU 问题，且阶段 4 会用 “没变化不重拍” 自然降下来，改频率会改变观感。
- **不动 ADR 0038 的抓屏排除**。那是玻璃能读到 “卡片背后” 的前提。
- **不为内存引入新依赖**。方案全部在现有 Qt 与标准库范围内。
- **不在 `src/` 里留测量探针**。`TEST.md` §5 与 pre-commit 的第三条都要求提交前清掉。

## 9、顺序与依赖

```
阶段 0（探针）
   ├─ 回落      → 阶段 1 → 阶段 2 → 阶段 3 → 阶段 4
   ├─ 只销毁回落 → 阶段 5C（按需创建）
   └─ 都不回落   → 阶段 5B（共享 QRhi）或 5D（单窗口）
```

阶段 2、3、4 与阶段 1 不冲突，可以并行；阶段 1 与阶段 5 互斥，不要同时改。

每个阶段单独一个提交，提交前按 `TEST.md` 的分级跑该跑的，并在提交信息里写清跑了什么。

## 10、实施结果（2026-10-18）

### 阶段 0：探针给出了明确答案

探针在本机编不过是因为沙箱拦 `cmd.exe`，不是代码问题：直接用 `cl.exe` 编译即可运行。

| 时刻 | 关持久化 + 隐藏释放 | Qt 默认 |
| --- | ---: | ---: |
| 起第一个窗口 | 87 线程 / 1,622 句柄 / 132.9 MB | 88 / 1,691 / 130.7 |
| 起第二个窗口 | 162 / 2,841 / 201.8 | 163 / 2,910 / 198.2 |
| 隐藏第二个并释放 | **89 / 1,638 / 181.3** | 164 / 2,913 / 198.2 |
| 销毁第二个 | 89 / 1,634 / 179.0 | 90 / 1,703 / 180.3 |

线程 −73、句柄 −1,203，达到 “−60 / −1,200” 的通过标准；对照组隐藏后一分不退。走方案 A。

### 阶段 1：做了，收益是主要的那一份

`src/app/main.cpp`：`releaseHiddenSurfaces()` 在 `loadFromModule` 之后、事件循环之前给每个顶层窗口关掉两个持久化
提示，并在隐藏 500 ms 后 `releaseResources()`（500 ms 内又显示则跳过，避免开合动画期间反复重建场景图）。
`LENS_KEEP_GRAPHICS=1` 跳过这一步，用于同二进制对照。

liquid 主题，开设置面板 → 关闭 → 开选区动作条（`exp_two.ps1`）：

| 时刻 | 改前 | 改后 |
| --- | ---: | ---: |
| 空闲 | 251.3 MB / 164 线程 / 4,501 句柄 | 251.3 / 165 / 4,504 |
| 打开设置面板 | 333.0 / 243 / 6,131 | 354.9 / 246 / 6,142 |
| 关闭 15 秒后 | 333.1 / 243 / 6,131 | **260.0 / 165 / 4,520** |
| 再开动作条 | 393.6 / 323 / 7,768 | 327.4 / 245 / 6,156 |

线程与句柄完全归还，内存回落到空闲 +9 MB。第二个表面不再叠一份全价：改前 323 线程，改后 245（始终只有一个窗口
的资源集在）。

10 次开合：关闭水位 261.4 → 271.7 MB（+10.3，阈值 20 内），线程 165 → 159，句柄 4,520 → 4,558。
**注意释放是滞后的**：关闭 3 秒时采样还有 303.7 MB，15 秒后才降到 260.7，所以验证脚本要采两次。

### 阶段 2：做了，实测零收益，已回退

只放玻璃纹理、窗口资源保持默认（`-KeepGraphics`）时：

| 时刻 | 值 |
| --- | ---: |
| 空闲 | 252.0 MB / 165 线程 |
| 打开设置面板 | 353.9 / 246 |
| 关闭 15 秒后 | **353.4 / 245** |

一分不降。VMMap 也印证：≥20 MB 的私有块从 4 个减到 2 个全部来自阶段 1，玻璃那层没动它们。原因大概是场景图在
`persistentGraphics` 下自己握着纹理，item 层清空 `Image.source` 并不会让驱动还页。留着只有风险（重开时首帧空）
没有收益，`GlassSurface.qml` 已还原。

### 阶段 3：未做

阶段 1 之后堆不再无界增长：10 次开合的关闭水位稳定在 261~272 MB，`Heap` 停在 152 MB（空闲 119 MB）。
原先 “堆从 119 涨到 431” 是每个窗口的场景图，不是折射贴图。剩下的分配抖动由阶段 4 从源头掐掉。

### 阶段 4：做了，CPU 减半

`GlassSheet::publish()` 先用 `pictureFingerprint()`（32×32 灰度、每通道留 5 位）比对，桌面没变就不递增 serial、
不发 `pictureChanged`。设置面板常开、桌面静止 50 秒：

| | CPU | 内存 |
| --- | ---: | ---: |
| 改前 | 8.7 s / 50 s = **17%** 单核 | 353.7 → 354.8 MB |
| 改后 | 5.0 s / 50 s = **10%** 单核 | 346.5 → 348.1 MB |

剩下的是每秒 15 次的 GDI 读取本身，指纹只能省掉模糊与 QML 重新加载。指纹会不会把玻璃冻住：滚动背后的页面时
CPU 从 8.7% 升到 12%，内存 +10 MB（新幕布进了图片缓存），说明变化仍会发布。`GlassMath` 的 4 个新用例覆盖
稳定性、真实变化、单像素噪声与空图。

### 还没做的

- 每 10 次开合 +10 MB / +38 句柄的慢速爬升，稳定但不归零。候选是 QML 图片缓存的 serial 条目与字体缓存。
- 阶段 5（共享 QRhi 或按需创建）不需要了：阶段 1 已经把线程从 804 压到 245，与窗口数解耦。
