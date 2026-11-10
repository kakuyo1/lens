# QML 场景与真机探针复测

## 1 对象与口径

新建 `build-ninja-qml-profile` 树：RelWithDebInfo，开 `LENS_ENABLE_QML_DEBUG=ON`，
这是 `qml-profile.ps1` 的工程前提，CMake 会给 `lens` 加 `QT_QML_DEBUG`。
应用内部按固定时间线走八步（托盘菜单、语言列表、统计、费用、单词、设置、气泡、退出），
不移动鼠标、不点托盘坐标，末步 `Qt.quit()`，所以 trace 可重复生成。
`scripts/profiling/parse-qml-trace.py` 把 `.qtd` 解析成事件汇总与热点表。

对照是 `qml-automated-scenario-baseline-2026-10-03.md`。那份的 trace 文件
（`test/records/qml-scenario-2026-10-03.qtd`）已经不在树里，数字取自报告正文。

另跑 `scripts/qa/mouse-stall-probe.ps1` 二十秒，验钩子装在独立线程这条约束。

## 2 结果

| 量 | 2026-10-03 | 2026-11-10 | 倍数 |
| --- | ---: | ---: | ---: |
| Trace wall duration | 8.264 s | 8.615 s | 1.04 |
| 事件数 | 1434 | 6991 | 4.9 |
| `Creating` | 276 条 / 763.720 ms | 1091 条 / 4207.375 ms | 4.0 / 5.5 |
| `Javascript` | 604 条 / 179.554 ms | 3032 条 / 1091.956 ms | 5.0 / 6.1 |
| `Binding` | 470 条 / 30.195 ms | 2392 条 / 631.016 ms | 5.1 / 20.9 |
| `Compiling` | 20 条 / 70.233 ms | 127 条 / 344.325 ms | 6.4 / 4.9 |
| `HandlingSignal` | 1 条 / 0.045 ms | 75 条 / 340.481 ms | 75 / 7566 |
| `MemoryAllocation` | 3 条 | 3 条 | 1.0 |
| `PixmapCache` | 55 条 | 266 条 | 4.8 |
| `SceneGraph` | 4 条 | 4 条 | 1.0 |

不同 profiler range 会重叠，捕获耗时只用于同类热点排序，不能相加成 wall-clock 时间。

前八个热点里有七个是启动期的对象创建：

| 位置 | 对象 | 次数 | 合计 ms |
| --- | --- | ---: | ---: |
| `Main.qml:15` | `QtQuick/Window` | 2 | 435.0 |
| `Main.qml:118` | `OnboardingWizard.qml` | 1 | 401.1 |
| `OnboardingWizard.qml:16` | `QtQuick/Window` | 2 | 400.5 |
| `OnboardingWizard.qml:223` | `ShadowCard.qml` | 1 | 397.9 |
| `OnboardingWizard.qml:306` | `QtQuick/Flickable` | 2 | 396.8 |
| `OnboardingWizard.qml:551` | `QtQuick/TextEdit` | 2 | 395.0 |
| `OnboardingWizard.qml:525` | `QtQuick/Column` | 2 | 394.9 |
| `OnboardingWizard.qml:543` | `QtQuick/Rectangle` | 2 | 394.8 |

其后是持续成本：`GlassHost.qml:112` 的 `onTriggered` 六次共 131.1 ms，
`ShadowCard.qml:71` 的 `onTriggered` 三次共 130.0 ms。
应用日志同时给出玻璃的十秒核算：693 次 tick、687 帧、桌面读中位 17.2 ms、
帧拷贝中位 1.3 ms，与 2026-10-19 那份采集上限报告里的节奏一致。

鼠标停顿探针：二十秒里 **0 次停顿**，最差单次 42.6 ms，阈值是 60 ms。
历史记录里钩子线程出过三次 311 ms，修复后是 0 次超过 60 ms、最差 27.7 ms；
本次最差略高但仍在阈值内，11-09 改动 `alt_key_hook.cpp` 之后没有回归。

## 3 读法

**端到端时长基本没变**：8.264 到 8.615 s，差 4%。但引擎工作量涨了约五倍，
事件数 1434 到 6991，`Creating` 的捕获耗时从 764 ms 到 4207 ms。
这不是同一份代码变慢五倍，而是场景里的东西变多了：10-03 时九个表面、玻璃只在设置窗，
现在是十九个表面、玻璃铺到各个表面，还多了三步的首次引导向导
（10-31 的 ADR 0148）与一批 phase 状态机。前八个热点有七个来自向导与根窗口的创建，
是一次性的启动成本，不是每次操作的开销。

`Binding` 的耗时涨了约二十一倍，是涨幅最大的一类，与绑定数量翻五倍、玻璃参数投影
（ADR 0173）和 phase 派生属性在同一时间落地同向。`HandlingSignal` 从 1 条到 75 条，
对应玻璃刷新与卡片动画的定时器回调，累计 340 ms，占 wall 的 4%。

**不能据此判断 60 Hz 卡顿**：trace 没有逐帧百分位，历史基线也明确写了这一条。
要看掉帧得另跑 `lens-measure.ps1 -Cpu` 加逐帧记录，或把动画段单独拆出来量。

也没有理由据此做优化。真要动，第一处是首次引导向导：它只在首次运行出现，
却在本次场景里占了七个热点；按需创建能把它从每次启动里拿掉，
但 10-05 的空闲内存报告已经说过，改成按需建要连带重做出现动画那一层，得单独立项。

## 4 顺带修掉的一个工具缺陷

`scripts/profiling/qml-profile.ps1` 原来只在 `-Scenario` 不等于 `full` 时设置
`LENS_QML_PROFILE_SCENARIO`。而 `main.cpp` 只在变量非空时跑时间线，`full` 在 C++ 侧
不是特殊值，任何其它名字都会落到全部八步。于是 `-Scenario full` 恰恰不跑场景、不调
`Qt.quit()`，profiler 会一直挂着，本次第一跑就挂了三分半。现在改成总是设置该变量。
历史报告里的复现命令写的是不带值的 `-Scenario`，对应更早的 switch 版本，
按现在的参数表要写 `-Scenario full`。

## 5 没覆盖到的

- 逐帧百分位与掉帧判定，理由见上。
- `lens_gtest_glass_probe` 的四例采集上限要可复制桌面会话，本轮没有跑；
  10-19/20 的数字仍是最新的采集上限基线。
- `ocr-bench.ps1` 的英中混排基线没有重跑，RapidOCR 可下载之后引擎变了，
  是 `docs/metrics/ocr-baseline-2026-10-28.md` 之后的一个待补项。
- 这份是 `QT_QML_DEBUG` 的测量构建，数字只与同口径的 trace 比，不能当作读者看到的耗时。

## 6 数字来源

`test/records/qml-scenario-2026-11-10.qtd` 与同名 `.json`（均 gitignored），
鼠标探针的控制台输出。历史数字照抄 10-03 那份报告的表格，没有重算。
