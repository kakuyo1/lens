# 内容过渡期间的原生窗口缩放

## 1、范围

三个表面的窗口高度直接绑在内容高度上，内容在 180–220 ms 里连续变化时，原生窗口原本跟着每帧改尺寸：

- **气泡的判定行**：指针进入卡片，`BubbleVerdictRow` 展开 55 px（`bubble` 令牌，220 ms）；离开时收起。
- **词汇面板的筛选**：在 all / known / new 之间切换时，行高用 `pop` 令牌（180 ms）折叠或展开，列表高度
  取 `min(306, contentHeight)`，窗口高 = 列高 + 34 + 阴影边距。
- **年度面板的调色板**：`paletteProgress` 从 0 到 1，加载器高度随 `implicitHeight` 一起展开（180 ms）。

面板还挂着 `holdCorner()`：高度每变一次，y 也跟着变一次。基线一次过渡有 8–13 次原生高度变化，面板的
y 变化次数与高度相同；帧间隔中位数 13–18 ms，p95 到 33 ms。

P0-2 让三个表面都在过渡起点一次跳到最大高度，内容在预尺寸的窗口里动。实测把六条过渡的原生高度变化
都降到 1 次，帧间隔 p95 降到 12 ms。但手测发现**词汇面板与年度面板在过渡中闪烁**，比改动前更糟：
面板以右下角为锚，窗口变高时 y 同时改变；为了让卡片的可见底边保持不动，卡片必须在固定尺寸的窗口里
每帧下移、缩短，这个 “窗口不动、卡片在窗口里动” 的表现在真实 Windows 透明窗口上读作闪烁。

最终只有气泡保留预尺寸；两个面板回退到逐帧跟随内容，回退后经手测确认恢复平滑、不再闪烁（2026-10-17）。
本文的气泡数据是当前实现的测量值，面板数据是被否掉的试验版本，保留下来作为回退依据。

## 2、方法

用 `test/qtest/surfaces/tst_p02_measure.qml`（临时用例，测量后已删除）在真实 Windows 平台上加载
`Main` 与真实的 `Controller`：

- `Connections` 监听目标 `Window` 的 `height` / `width` / `x` / `y` 变化信号，记录每次变化的时间与值。
- 4 ms 的 `Timer` 采样窗口高度，统计采样序列里出现过的不同高度。
- `FrameAnimation` 记录每帧的墙钟间隔，报告状态改变后 300 ms 内的中位数、p95、最大值和超过 20 ms
  的帧数（基线取最后一次几何变化后 40 ms，窗口长度接近）。
- 每条过渡采样 500 ms：`bubble.hovering = true / false`、`words.filter = 1 / 0`、
  `year.openPalette(2) / year.closePalette()`；每个状态稳定后再读窗口高度。

| 项 | 值 |
| --- | --- |
| 构建 | `build-ninja-release`（RelWithDebInfo），QML 经 qmlcachegen 预编译 |
| 平台 | Windows 11 10.0.26200，真实窗口平台（非 offscreen） |
| Qt | 6.9.0 MSVC 2022 x64 |
| 数据 | 测试夹具 `settings.json`，历史 8 个词（3 known、5 new） |
| 基线 | 同一用例、同一构建类型、同一台机器，在改动前的代码上测得 |

offscreen 平台的字体度量与真实平台相差 1–2 px，帧节拍也不代表实际显示，因此本文所有数字都取自真实
窗口平台。

## 3、结果

### 3.1 原生窗口几何变化

| 过渡 | 改动前 | 试验版 | 当前实现 |
| --- | ---: | ---: | ---: |
| 气泡展开判定行 | 13 | 1 | **1** |
| 气泡收起判定行 | 10 | 1 | **1** |
| 词汇 all → known | 9 | 1 | 9 |
| 词汇 known → all | 12 | 1 | 12 |
| 年度调色板展开 | 11 | 1 | 11 |
| 年度调色板收起 | 8 | 1 | 8 |

气泡的展开在第 0–11 ms 跳到目标高度，之后整个动画期间不再变，轨迹是 `[[3,"h",279],[3,"y",577]]`
（贴近屏幕下缘时被 `keepOnScreen` 上推一次）；收起在动画结束、行落位后变一次。远离屏幕边缘时 y 不变。

面板的试验版同样只有 1 次高度变化：展开类在起点跳到目标，收起类在动画结束后跳回。当前实现已回退，
高度与 y 恢复为每帧跟随内容。

### 3.2 帧间隔

| 过渡 | 中位数（前 → 试验） | p95（前 → 试验） | 最大（前 → 试验） | >20 ms 帧（前 → 试验） |
| --- | ---: | ---: | ---: | ---: |
| 气泡展开判定行 | 14 → 11 ms | 19 → 12 ms | 19 → 13 ms | 0/19 → 0/28 |
| 气泡收起判定行 | 18 → 11 ms | 32 → 12 ms | 32 → 13 ms | 4/13 → 0/28 |
| 词汇 all → known | 18 → 11 ms | 25 → 12 ms | 25 → 13 ms | 5/14 → 0/27 |
| 词汇 known → all | 13 → 11 ms | 19 → 12 ms | 19 → 14 ms | 0/17 → 0/28 |
| 年度调色板展开 | 16 → 11 ms | 25 → 12 ms | 25 → 13 ms | 1/15 → 0/27 |
| 年度调色板收起 | 16 → 11 ms | 33 → 12 ms | 33 → 14 ms | 4/13 → 0/27 |

试验版六条过渡的中位数都落在 11 ms（这台机器 90 Hz 的显示节拍），p95 都是 12 ms，没有超过 20 ms
的帧；改动前，内容逐帧重排加原生 resize 把帧推离显示节拍，收起类过渡 p95 到 32–33 ms。

**当前实现只保留气泡这一行**；两个面板回退后帧节奏与 “改动前” 一列相同。帧数从 13–19 帧变成 27–28 帧，
是显示节拍本身从 13–18 ms 收紧到 11 ms 的结果。

## 4、正确性

- 气泡的最终高度与改动前逐像素相同：收起 224，展开 279。判定行从卡片顶部向下展开，内容没有被裁剪。
- 面板试验版的最终高度也与改动前逐像素相同（词汇 433 / 278，年度 218 / 382），闪烁不是尺寸误差，
  而是过渡期间卡片在固定窗口内的位移；因此回退不以尺寸为理由。
- 回退后的面板与改动前的代码逐字一致（`WordsPopup.qml`、`YearWordsPopup.qml`、`ShadowCard.qml`）。
- `tst_bubble.qml` 8 项通过、`tst_panels.qml` 20 项通过，qml-lint 0 警告。
- 减少动效时 `HeightHold.begin()` 直接返回，不建立 hold，气泡窗口跟随内容的瞬时跳变。

## 5、结论

1. 气泡保留预尺寸：原生高度变化 13 → 1，帧 p95 从 19 / 32 ms 降到 12 ms，超过 20 ms 的帧从每次最多
   5 帧降到 0。气泡的卡片顶边固定、内容不在窗口里移动，是这一做法成立的条件。
2. 词汇面板与年度面板回退：试验版虽然把高度变化降到 1 次、帧 p95 降到 12 ms，但卡片在固定窗口里
   每帧下移造成可见闪烁，观感比旧的逐帧 resize 更糟。性能数字不能弥补错误的运动。
3. 规则：预尺寸只用于内容朝锚点反方向生长、卡片本身不必在窗口内移动的表面（docs/adr/0055）。
4. 气泡的代价：过渡的 180–220 ms 里窗口多出一块透明区域，参与命中测试；贴近屏幕下缘时展开会被上推一次。
5. 手测检查点：悬停气泡看判定行展开与收起，确认行从卡片顶部向下展开、卡片不整体跳动；切换词汇筛选
   all / known / new 与打开、收起年度调色板，确认面板恢复为改动前的平滑跟随，没有闪烁。

## 6、复现

临时用例的核心探针如下（放进 `test/qtest/surfaces/`，以 `lens_qtest_surfaces.exe -input <file>` 运行）：

```qml
QtObject {
    id: probe
    property var target: null
    property bool running: false
    property var geometry: []
    property var frames: []
    property var samples: []
    property real startedAt: 0

    function start(window) {
        target = window;
        geometry = [];
        frames = [];
        samples = [];
        startedAt = Date.now();
        frameClock.previous = 0;
        running = true;
    }
    function stop() { running = false; }
    function note(kind) {
        if (running)
            geometry.push({ t: Date.now() - startedAt, kind: kind,
                            v: kind === "h" ? target.height : kind === "y" ? target.y : target.x });
    }
}

Connections {
    target: probe.target
    enabled: probe.running
    function onHeightChanged() { probe.note("h") }
    function onYChanged() { probe.note("y") }
}

Timer {
    interval: 4
    repeat: true
    running: probe.running
    onTriggered: probe.samples.push(probe.target.height)
}

FrameAnimation {
    id: frameClock
    running: probe.running
    property real previous: 0
    onTriggered: {
        const now = Date.now() - probe.startedAt;
        probe.frames.push({ t: now, wallDt: now - previous });
        previous = now;
    }
}
```

三个测试函数分别执行 `bubble.hovering = true`、`words.filter = 1`、`year.openPalette(2)`，500 ms 后
`probe.stop()`，从 `geometry` 统计高度与 y 的变化次数，从 `frames` 取 `t <= 300` 的 `wallDt`，排序取
中位数、p95 和最大值。改动前的数字用同一用例在改动前的 `lens_app` 上测得；面板一栏的试验数字需要在
预尺寸版本上测得，当前代码复现不出来。

```powershell
$env:PATH = "B:/qtt/6.9.0/msvc2022_64/bin;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = "1"
./build-ninja-release/test/qtest/lens_qtest_surfaces.exe `
  -input test/qtest/surfaces/tst_p02_measure.qml
```

不要设置 `QT_QPA_PLATFORM=offscreen`：最终高度会因字体度量差 1–2 px，帧节拍也不代表真实显示。
