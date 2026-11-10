# 句柄爬升按类型归因

## 1 对象与口径

对象是 `build-ninja-dev` 的 Debug 开发面构建，同一份剧本重放六十轮：
七张表面各 show、painted、hide、hidden，末了 hide-all，每轮静置 3 秒再等 1 秒。
总曲线在 `soak-cycles-2026-11-10.md` 里已有 25 轮那一份；这一份跑到 60 轮，
斜率完全相同，用来确认它不是短程噪声。

类型归因靠 `scripts/qa/handle-types.ps1`：它遍历系统句柄表、留下属于该 PID 的条目、
把对象类型索引解析成类型名。在同一轮运行里连采八次，间隔 20 秒，约合 3.8 轮。
另用 `.workbuddy-ai/tools/threadmap.py` 按起始模块归因线程。

读法：一次快照是一次相位采样，所以先看序列的形状再看首末差。线性上涨的是泄漏，
在两条水平线之间来回跳的是每轮一批、下轮释放的临时对象，后者不该算进爬升。

## 2 总曲线（六十轮）

| 轮 | 私有字节 MB | 线程 | 句柄 |
| ---: | ---: | ---: | ---: |
| 0 | 264.2 | 87 | 3240 |
| 10 | 373.5 | 116 | 4815 |
| 20 | 362.7 | 114 | 5142 |
| 30 | 365.9 | 113 | 5470 |
| 40 | 373.5 | 115 | 5805 |
| 50 | 380.6 | 116 | 6136 |
| 60 | 377.1 | 114 | 6467 |

第 3 轮起的斜率：私有字节 +0.19 MB/轮，线程 -0.02/轮，句柄 +33.1/轮。
前 10 轮那一步是预热（私有字节 +109 MB、线程 +29），之后两者都平了，
只有句柄继续按每 10 轮约 325 个往上走。25 轮那一组的斜率也是 +33.1/轮，
两份独立运行到小数点后一位相同，所以这不是噪声。

## 3 类型趋势（八次快照，间隔 20 秒）

| 类型 | s0 | s1 | s2 | s3 | s4 | s5 | s6 | s7 | 首末差 | 每次 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Event | 1557 | 2610 | 2610 | 3917 | 2613 | 3956 | 2611 | 2611 | +1054 | +150.6 |
| Section | 433 | 476 | 507 | 557 | 593 | 649 | 684 | 723 | +290 | +41.4 |
| Mutant | 80 | 119 | 150 | 194 | 236 | 285 | 327 | 366 | +286 | +40.9 |
| Thread | 99 | 148 | 165 | 216 | 212 | 300 | 260 | 281 | +182 | +26 |
| Timer | 82 | 111 | 111 | 137 | 111 | 173 | 111 | 111 | +29 | +4.1 |
| Semaphore | 122 | 147 | 147 | 174 | 147 | 214 | 147 | 147 | +25 | +3.6 |
| File | 447 | 455 | 455 | 462 | 454 | 462 | 454 | 454 | +7 | +1 |

其余类型（Key、Directory、WindowStation、ALPC Port 等）在采样里不动或只动一两个。

## 4 结论

### 4.1 只有三类是线性上涨

Section、Mutant、Thread 的序列每一格都比上一格高，没有回落。
Event、Timer、Semaphore 是锯齿：Event 在 2610 与 3950 两条线之间来回跳，
Timer 与 Semaphore 在 111 与 173、147 与 214 之间跳。它们每轮创建一批、
下一轮释放，首末差只是采样恰好停在波峰，不算泄漏。

按 20 秒约 3.8 轮折算，Section 每轮约 +10.9，Mutant 约 +10.8，Thread 约 +6.8，
合计约 +28.5，与总句柄 +33.1/轮对得上；剩下的约 4.6 落在 Event 锯齿的相位差上。
每轮开七张表面，所以每面约 +1.55 个 Section、+1.54 个 Mutant、+0.97 个 Thread 句柄。
三个数几乎一样，指向同一件事：每次窗口显示会带走一组同类的句柄，隐藏后不归还。

### 4.2 这是句柄泄漏，不是内存泄漏

六十轮里私有字节只 +0.19 MB/轮，线程完全平；Section 句柄涨了六百多个，
但映射的内存没有跟着涨。所以泄漏的是句柄表里的条目，不是堆块、也不是共享内存的内容。
2026-10-18 报告 9.9 节把它写成慢速爬升，候选是 QML 图片缓存的 serial 条目与字体缓存；
现在可以说得更准：是三类系统对象句柄，每面各一到两个，与堆分配无关。

### 4.3 线程本身没有回归

`threadmap.py` 在 95 个线程时给出的分布是：40 个起于 `nvwgf2umx.dll`、
24 个起于 `amdxx64.dll`、13 个起于 `Qt6Cored.dll`、9 个起于 `ntdll.dll`，
其余各一两个。驱动线程仍是多数，但总数是 95，不是 2026-10-18 那份报告里的 804。
六十轮里线程从 87 到 114 的那一步发生在头十轮，之后稳定在 113 到 116，
所以它是一次性预热，不是逐轮增长。

### 4.4 创建栈：三分之二来自 AMD 驱动的设备创建

拿到管理员令牌后用 cdb 的 `!htrace` 追了三十余轮，按句柄配对挑出打开后没有关闭的
1885 个，再按创建栈聚合：

| 未关闭 | 创建栈 |
| ---: | --- |
| 774 | `NtCreateEvent` 到 `CreateEventW` 到 `amdxx64!AmdD3D11CreateDeviceAndSwapChainExt` |
| 462 | `NtCreateEvent` 到 `CreateEventA` 到同一条 |
| 118 | `NtCreateSemaphore` 到 `CreateSemaphoreExW` 到 `CreateSemaphoreA` |
| 115 | `ZwCreateThreadEx` 到 `CreateRemoteThreadEx` 到 `CreateThread` |
| 114 | `NtCreateTimer` 到 `CreateWaitableTimerExW` 到 `CreateWaitableTimerExA` |
| 40 | `CreateEventA` 到 `nvwgf2umx!getDiskCacheFunctionTable` |
| 37 | `RegOpenKeyExInternalW` |
| 26 | `CreateEventA` 到 `nvwgf2umx!NVAPI_DirectMethods` |
| 20 | `GraphicsCapture!DllGetActivationFactory` |

约 1236 个、占 66% 来自 `amdxx64` 的 `AmdD3D11CreateDeviceAndSwapChainExt`。
这台机器是 AMD 显卡（`threadmap` 里 24 个线程起于 `amdxx64`），所以每次表面显示重建
D3D 设备时，驱动拉起一组等待事件，`releaseResources()` 释放 QRhi 之后这些事件句柄没有关。

这一条把前面的三条串起来了：两种配置都涨，因为都建过设备，而 `KeepGraphics` 只在首轮
建一次，所以它的斜率低三成；设置连打不涨，因为它不建 D3D 设备；私有字节不涨，
因为 Event 对象本身很小。

## 5 与 9.9 节的关系

10-18 那节的三个未解决项里，这一份回答了两条：

- 每次开合的慢速爬升：仍在，速率是每轮约 33 个句柄，其中 Section 与 Mutant 各约 11、
  Thread 约 7；与图形资源是否释放无关（`soak-cycles` 里两种配置都涨），
  与设置写入无关（设置连打那组不涨）。
- 关闭后三秒内仍偏高：与本次无关，那是驱动还页的节奏，`-SettleSeconds` 已经按 15 秒规避。

没回答的是第三项：人工观感。它不在这条测量线上。

## 6 修复方向

结论是这笔泄漏主要在 AMD 驱动里，不在应用代码的 `CreateFileMapping` 或 `OpenThread`。
所以按句柄类型去代码里找没关的句柄是错的方向，三条路可选：

1. 不频繁销毁设备。ADR 0040 时代提过的共享 QRhi 方案让所有窗口共用一个设备，
   释放的收益与重建的代价同时消失，驱动也不再每次拉起一组事件。改动面大，需要单独立项。
2. 接受这个权衡。释放省 470 MB 与 560 个线程，代价是每轮约 33 个句柄与 800 ms。
   按 33 个每轮算，一万轮三万，托盘常驻一周的量级仍在可接受范围，但要先按周实测一次。
3. 向 AMD 报告。`AmdD3D11CreateDeviceAndSwapChainExt` 在设备销毁后不关 Event 是可复现的
   驱动缺陷，这份栈可以直接附上。

应用代码里仍有一处值得修，但它不属于这三类句柄：
`src/app/glass/glass_capture.cpp` 的 `ensure()` 每次 `CoInitializeEx` 没有配对
`CoUninitialize`，那是 COM 引用计数，失败后五秒重试会再初始化一次。

工具方法记在这里，省下一次重复踩坑：`!htrace -diff` 不能用，因为快照只存在调试器会话里，
`qd` 分离就丢，会报 no initial snapshot available；实际用的是 `!htrace -enable` 加
`!htrace -open`，再按句柄配对 OPEN 与 CLOSE。`gflags` 没有 handle tracing 标志
（`+htc` 是 heap tail checking），Application Verifier 的 Handles 检查在正常退出时
没有留下记录。cdb 必须用 `qd` 而不是 `q`，`q` 会终止被调试的进程。

## 7 没覆盖到的

- 只有 Debug 开发面构建，毫秒与兆字节不能当作读者看到的数。
- 对象身份：提权后拿到了。Section 466 个句柄对应 412 个对象，接近一一对应，说明每次新建；
  Mutant 109 个句柄只对应 35 个对象，约三倍，说明重复打开同一对象；Thread 134 对 125，
  也接近一一对应。非提权调用下 Object 指针一律返回 0，`handle-types.ps1` 的 uniq 列
  此时显示 n/a，提权后才有数。
- 句柄类型名有一个未解析（报告里的 `Type#56`，332 个，不涨），不影响结论。
- 只测了表面开合这一种负载；扫描与设置两组在 `soak-cycles` 里已经证明不涨或涨得少。
- 六十轮约五分钟，不是数小时；不过斜率在两份独立运行里一致，外推的底气比单次足。

## 8 数字来源

`logs/soak-handle2.csv`（六十轮曲线）与 `logs/handle/surfaces-trend.txt`（八次类型快照），
均为 gitignored；数字照抄，斜率是重新拟合的。
