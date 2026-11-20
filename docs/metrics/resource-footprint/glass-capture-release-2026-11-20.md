# 玻璃抓屏的空闲释放：隐藏后回落到哪，剩下的钱是谁的

## 对象与口径

修前一轮、修后六轮、两次线程归因、一次三轮开合、两次 dev 树对照。全部在同一台机器、同一天完成，没有管理员权限，没有改动读者设置。

1. **修前**：`build-ninja-release/src/app/lens.exe`（21:00:22 链接，HEAD `2f4c3977` 加工作区里未提交的 stage 3 改动；玻璃代码与 HEAD 相同），21:01:21 启动，PID 30008，21:04:39 退出。
2. **修后**：同一棵树重新增量链接（21:06 一轮只含空闲释放；21:32 一轮加 `IDXGIDevice3::Trim()`；21:56 一轮加 `onFrameArrived` 的互斥，22:06 一轮把布尔换成池代次），六次完整协议：PID 35724（21:07:06，隐藏后取样带中途 VMMap）、15540（21:25:18，干净）、13540（21:33:06，干净）、29352（21:37:25，干净）、27040（21:56:44，干净）、14416（22:07:11，干净，代次版）。
3. **线程归因**：PID 17712（21:23 前后），空闲、显示中、释放后各取一次 `threadmap.py`。
4. **三轮开合**：PID 4396（21:12:08），开合三轮、末轮再等 240 秒。
5. **dev 树对照**：`build-ninja-dev/src/app/lens.exe`（Debug，11-18 23:39 构建，不含本轮改动）两次，PID 31356（`theme=light`，`logs/dev-run/attr-settings.json`）与 9024（`theme=liquid`、`quality=standard` 的临时副本 `logs/dev-run/ctrl-liquid-standard.json`）。只用来框定 “没有采集时 settings 值多少”，绝对数不外推。

release 各轮用的是 `%APPDATA%\Lens\settings.json`（`theme=liquid`、`quality=smooth`、宠物 mochi、`autoScan=true`），`state` 在每轮启动后报同样的事实。该文件本轮没有手工读写；应用在每轮里自行写了 7 次 `settings saved`（autoScan 的既有行为），`mastered` 从 198 涨到 200，没有 `verdict marked` 与铅笔层事件。

协议（逐轮相同）：启动后静置约 60 秒记空闲，经开发管道 `surface show settings`、`wait visible:settings`、`wait painted:settings`，按住 10 秒，`surface hide settings`、`wait hidden:settings`，之后每秒采一次 `Get-Process` 到隐藏 +120 秒；干净轮在 +120 秒后取一份 VMMap，再 `surface show settings` 做一次复显，按住约 5 秒后隐藏、`quit`。可见表面时间每轮 15 秒左右。没有合成鼠标、没有键盘注入、没有截图；每轮只停止自己启动的 PID。

命令：

```
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/settle.ps1 -Tag before
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/settle.ps1 -Tag after
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/settle.ps1 -Tag after-clean -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/settle.ps1 -Tag after2-clean -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/settle.ps1 -Tag after3-clean -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/threads.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/cycles.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/control.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File logs/glass-release/control.ps1 -Tag ctrl-liquid-standard -Settings D:/lens-dev/logs/dev-run/ctrl-liquid-standard.json -Tail 60
```

并发负载：每轮启动前 `Get-Process -Name lens` 为空，没有其它构建、测试或 VMMap 在跑；桌面本身有宠物动画与扫描轨（`autoScan=true`），这是后面读数波动的前提。

## 结果

### 一、修前：一张玻璃表面把地板抬到空闲 +195 MB

空闲取 60 秒时最后一帧（295.1 MB）。隐藏后 5 秒退到 488.8 MB 就停住，之后 120 秒不再往空闲回落；线程与句柄也停在显示后的量级，会话还开着。

| 时刻 | 私有 MB | 工作集 MB | 线程 | 句柄 | 相对空闲 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 空闲 60 s | 295.1 | 349.0 | 155 | 4,883 | — |
| 显示中峰值 | 557.1 | 421.2 | 219 | 6,224 | +262.0 |
| 隐藏 +5 s | 488.8 | 412.7 | 218 | 6,207 | +193.7 |
| 隐藏 +15 s | 489.1 | 413.0 | 218 | 6,207 | +194.0 |
| 隐藏 +30 s | 490.3 | 413.1 | 218 | 6,206 | +195.2 |
| 隐藏 +60 s | 490.0 | 412.9 | 209 | 6,150 | +194.9 |
| 隐藏 +120 s | 490.9 | 407.8 | 209 | 6,146 | +195.8 |

VMMap：隐藏 +60 秒私有合计 485.6 MB（堆 183.2、Private Data 244.7、Image 44.8、栈 12.8）；+120 秒 486.5 MB（堆 184.2、Private Data 244.7）。日志里没有释放行，也没有第二次开采集；采集会话从 21:02:22.103 打开后一直开着，线程 209 与句柄 6,146 在 +120 秒仍与显示后同一量级。

### 二、修后：会话在隐藏后 10.0 秒退场

六轮的协议相同，区别只在 “隐藏期间是否加中途 VMMap” 与链接历史。释放行每轮都写在隐藏后 10.0 秒（21:08:27.76、21:26:38.97、21:34:26.97、21:38:45.34、21:58:05.42、22:08:32.33）。

| 轮次 | 空闲 60 s | 显示峰值 | +5 s | +15 s | +30 s | +60 s | +120 s | +120 线程/句柄 | VMMap +120：堆 / Private Data |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 修前 | 295.1 | 557.1 | 488.8 | 489.1 | 490.3 | 490.0 | 490.9 | 209 / 6,146 | 184.2 / 244.7 |
| 修后 D（中途 VMMap） | 293.3 | 514.4 | 462.7 | 429.3 | 422.3 | 422.3 | 425.2 | 184 / 5,002 | 161.0 / 205.3 |
| 修后 A（干净） | 292.8 | 382.3 | 369.5 | 342.4 | 334.6 | 335.4 | 335.5 | 182 / 4,996 | 159.3 / 115.3 |
| 修后 B（干净，含 Trim） | 291.4 | 543.7 | 382.4 | 351.9 | 347.0 | 346.9 | 346.8 | 182 / 5,000 | 158.9 / 126.9 |
| 修后 C（干净，含 Trim） | 290.4 | 535.8 | 469.9 | 440.0 | 434.8 | 434.5 | 435.2 | 183 / 5,001 | 158.9 / 215.0 |
| 修后 E（干净，布尔互斥） | 294.6 | 396.9 | 392.9 | 361.1 | 357.6 | 357.4 | 359.0 | 183 / 5,002 | 161.7 / 136.3 |
| 修后 F（干净，代次互斥） | 291.8 | 378.8 | 369.1 | 336.9 | 334.0 | 333.8 | 333.8 | 182 / 4,998 | 159.3 / 113.5 |

空闲 +50 MB 的线：修前 345.1、D 343.3、A 342.8、B 341.4、C 340.4、E 344.6、F 341.8。**A 与 F 落到线内（+42.6、+42.0 MB）；B 差 5.5 MB，C 差 94.1 MB，D 差 79.0 MB，E 差 62.8 MB。**修前的 +195.8 MB 在六轮修后里一次都没出现，但 “+50 以内” 没有稳定成立。

线程与句柄的回退是齐的：六轮修后都在隐藏 +40 秒内从 218 线程 / 6,207 句柄落到 182–185 / 4,996–5,009，与空闲的 153–155 / 4,871–4,888 只差那 30 个左右驱动线程与 110 个左右句柄。堆在六轮里是平的：158.9–161.7 MB（修前 183–184 MB），Private Data 是 113.5–215.0 MB（修前 244.7 MB；空载参考约 84 MB，取自归因轮同一份读者设置下的 60 秒快照）。E 与 F 两轮补上了 `onFrameArrived` 与 `release()` 的互斥：迟到的帧回调不能把帧留在 `latest` 里，也就不会拖住设备；两轮的地板是 +64.4 与 +42.0 MB，读数形状与 A 到 D 相同。

峰值没有抬高：修前 557.1 MB，修后 378.8–543.7 MB；高端的 543.7 与 535.8 仍在修前的波动范围内，不代表新增成本。

### 三、线程归因：退的是采集，留的是共享设备的池

同一进程的三次 `threadmap.py`（PID 17712）：

| 时刻 | 总线程 | `nvwgf2umx.dll` | `amdxx64.dll` | `Qt6Core.dll` | `GraphicsCapture.dll` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 空闲 60 s | 152 | 79 | 48 | 7 | 0 |
| 显示中（painted 后 5 s） | 219 | 111 | 72 | 17 | 1 |
| 隐藏 +30 s（释放后） | 185 | 111 | 48 | 8 | 0 |

采集在 AMD 适配器上（日志 `device on AMD Radeon(TM) Graphics`）：AMD 驱动线程 72 → 48，`GraphicsCapture.dll` 的线程消失，Qt 线程 17 → 8，句柄约 −1,200。留下的 +32 个线程全部起于 `nvwgf2umx`；采集设备建在 AMD 上，AMD 线程已随释放退回，所以它们不属于采集会话，最可能是共享设备（建在 NVIDIA 上）渲染玻璃时的驱动池（ADR 0191）。该轮释放后 30 秒的私有字节是 349.7 MB；这一轮没有采空闲值，所以不放进上面的表。

### 四、三轮开合：地板是每轮重新抬起的，不叠加

PID 4396，三轮各显示 10 秒、隐藏后分别等 40、40、240 秒：

| 轮次 | 显示峰值 | 释放后 +30～40 s | 线程 / 句柄 |
| --- | ---: | ---: | --- |
| 第 1 轮 | 508.6 | 388.8 | 186 / 5,029 |
| 第 2 轮 | 512.2 | 375.5 | 186 / 5,030 |
| 第 3 轮 | 517.7 | 356.7 | 184 / 5,015 |

第三轮的尾巴：356.7 MB 之后 240 秒内停在 356.7–360.4 MB，不再往下走。也就是说，释放把每轮抬起的量大头都还了，但每轮都会重新留下一段驱动高水位；它不随轮次叠加，也不会在几分钟内继续归零。空闲是 293.2 MB，三轮之后 360.4 MB，仍高出 67.2 MB。

### 五、复显：卡片先用旧图，采集随后重开

每轮最后都做一次 `surface show settings`。六次都成功、都有 `wait painted`，且都出现新的 `glass capture on ... Windows Graphics Capture` 行（会话确实重开了）：

| 轮次 | show 命令 → 可见 | → painted | → 采集重开行 |
| --- | ---: | ---: | ---: |
| 修后 D | 62 ms | 119 ms | 151 ms |
| 修后 A | 80 ms | 138 ms | 169 ms |
| 修后 B | 76 ms | 133 ms | 167 ms |
| 修后 C | 62 ms | 99 ms | 152 ms |
| 修后 E | 85 ms | 140 ms | 176 ms |
| 修后 F | 72 ms | 131 ms | 167 ms |

从 `surface show` 到卡片上屏是 99–140 ms，走的是幕布里留着的旧图；采集在 painted 之后 31–36 ms 重开，没有拖慢首帧。修前那次复显没有新的采集行（会话一直开着），painted 用 137 ms。复显后的 5 秒保持期内，线程与句柄回到 216–219 / 6,192–6,225，与显示中同量级。

### 六、对照：没有采集的 settings 只值 +15 MB

dev 树、`theme=light`、宠物关、扫描关（`logs/dev-run/attr-settings.json`），第一张 settings 首显再隐藏：

| 时刻 | 私有 MB | 线程 | 句柄 |
| --- | ---: | ---: | ---: |
| 空闲 60 s | 306.4 | 102 | 3,302 |
| 显示中 | 324.4 | 100 | 3,335 |
| 隐藏 +5 s | 321.0 | 99 | 3,314 |
| 隐藏 +90 s | 321.0 | 97 | 3,308 |

没有采集时，settings 自己的 QML 与场景图只留 +14.6 MB，线程不增反降。第二份对照（`theme=liquid`、`quality=standard`）因为该临时配置里 onboarding 一直可见且 live，基线被它占着，数不可用，只留下一个旁证：Standard 档下显示与隐藏 settings 前后线程 112 → 109，没有新增那 32 个驱动线程。Debug 的绝对数不与 release 比。

## 读法

**修的是会话，不是驱动的那一档。**释放行准点在隐藏 +10.0 秒，AMD 采集线程、`GraphicsCapture.dll` 线程与约 1,200 个句柄一起退掉，Private Data 从 244.7 MB 落到 115.3–215.0 MB。会话确实关了，下一次 live 幕布也确实重新开了（复显表里的重开行）。

**剩下的 100 MB 级差在驱动高水位，不在 Qt 也不在会话。**六轮修后的堆是 158.9–161.7 MB，线程与句柄的终值几乎一样，Private Data 却从 113.5 到 215.0 MB 相差 100 MB；干净轮的峰值也正好从 378.8 到 543.7 MB。最可能的解释是：每次显示期间被 WGC 拷回的帧量不同（宠物动画、扫描轨、卡片背后的桌面内容），AMD/NVIDIA 驱动按这段流量留下不同的进程内高水位；关会话只还它自己的帧池、staging 与设备对象。`IDXGIDevice3::Trim()` 的提示没有改变这一点（B、C、E 三轮含它，地板没有比 A 更低）。

**D1 的 +50 既不成立也不是完全成立。**两轮 +42.6 与 +42.0 MB 达标，另三轮 +55.5、+144.8、+62.8 MB 不达标，带中途 VMMap 的那轮 +129.0 MB。可以确定的是：修前地板固定在 +195 MB、120 秒不动；修后地板在 +42 到 +145 MB，且不再随轮次叠加（第 3 轮 356.7 MB 后 240 秒稳定）。要把它稳定压到 +50 以内，得动 “不创建第二个设备” 的那条路：把抓屏设备并入应用的共享设备（ADR 0191 的同一思路），或在玻璃空闲时对共享设备也调一次 `Trim()`，或把采集放进独立进程。这三项都超出本轮 “隐藏后释放会话” 的范围。

**复显与视觉不变。**卡片用旧图先上屏，采集随后在 30 ms 左右重开；`wait visible`、`wait painted` 全部一次通过，没有空卡、没有等待。10 秒宽限没有在真人快开合上验证过；这一轮只证明了 120 秒后的复显。

## 没覆盖到的

- **桌面内容不可控**：宠物动画、扫描轨与其它窗口每轮都不同，这是五份干净轮地板差 100 MB 的最可能来源。要把它钉住，需要一次静态参考桌面（宠物关、扫描关、背景不动）的同协议重跑；本轮没有做，因为这要改读者设置或另起会话。
- **共享设备没有 Trim**：`SharedGraphics` 在 `src/app/platform/`，不在本轮 `src/app/glass/` 的范围；玻璃空闲时对它调 `Trim()` 是下一项最便宜的实验。
- **10 秒宽限没有真人样本**：只在复显（隐藏 120 秒后）上验证过；5 秒内关开是否真的不重开采集，没有单独取点。
- **只看到 120 秒（另有一轮 240 秒尾巴）**：三轮开合的第 3 轮在 +240 秒仍停在 356.7–360.4 MB，再长没有测。
- **VMMap 的时序**：干净轮只有隐藏 +120 秒后的一份，落在取样窗口之后；D 轮的两份在隐藏 +60/+120 秒启动，导出在 1 秒后落盘，读数可能含着更晚的状态。cycles 轮最后一份导出与 `quit` 竞争，被截断成 138.5 MB 且没有类型行，未用。
- **分配点**：仍然只有 VMMap 的类型合计；驱动高水位落在哪个分配点，需要 UMDH 与管理员，本轮未做。
- **没有改**：刷新节拍、指纹门、模糊/折射参数、表面布局、其它表面、任何设置默认值。`Glass*` 单测 58/58 绿，没有改测试期望。

## 数字来源

`logs/glass-release/before-samples.csv`、`before-marks.csv`、`before-vmmap-hide-060s.csv`、`before-vmmap-hide-120s.csv`；`after-samples.csv`、`after-vmmap-hide-060s.csv`、`after-vmmap-hide-120s.csv`；`after-clean-*.csv`、`after2-clean-*.csv`、`after3-clean-*.csv`、`after4-clean-*.csv`、`after5-clean-*.csv`；`cycles-samples.csv`、`cycles-marks.csv`、`cycles-vmmap-tail.csv`；`control-light-*.csv`、`ctrl-liquid-standard-*.csv`；`threads-run.txt`；各轮的 `*-run.txt` 与 `*-lens.log`。全部 gitignored。VMMap 类型合计用 `logs/attr/vmmap-summary.py` 解析；线程归因用 `.workbuddy-ai/tools/threadmap.py`；单测命令 `powershell -File scripts/qa/gtest.ps1 -Filter 'Glass*'`（58/58）。
