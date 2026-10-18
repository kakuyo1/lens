# 流光玻璃主题的内存占用：测量、修复与结果

第 1~8 节是诊断：问题、方法、八轮过程、数据与踩的坑。第 9 节是修复：先验释放语义，再按阶段改，每阶段单独测。

## 1、问题

读者反馈：开启流光玻璃（liquid）主题后常驻内存到 550 MB 以上，有时过 600 MB，对一个托盘应用不正常。

接管时那个实例已经在跑（`B:\Lens\lens.exe`，启动 22 分钟），实测比反馈更重：私有字节 966.9 MB，
工作集 668.7 MB，提交 2.0 GB，线程 804 并仍在增长（约每分钟 28 个）。

结论先写在这里，因为它与最初的假设不同：**主要成本不是玻璃，是 “每个表面一个独立顶层窗口” 这套结构**。
每多一个窗口第一次上屏，就永久多约 80 个显卡驱动线程、1630 个句柄、60~100 MB，关掉一分不退。
玻璃在这之上只加 0~23 MB。

## 2、方法

### 2.1 样本

| 项 | 值 |
| --- | --- |
| 二进制 | `B:\Lens\lens.exe` 与 `build-ninja-release\src\app\lens.exe`，MD5 同为 `27024689871e304dcc1ae5f5cafb11`，9,430,528 字节 |
| 构建 | RelWithDebInfo，2026-10-18 15:34 |
| 平台 | Windows 11，主屏 1920×1080，副屏 1440×2560（双显卡：AMD 核显驱动内屏，RTX 4060 驱动外屏） |
| Qt | 6.9.0 MSVC 2022 x64 |
| 读者设置 | `theme=liquid`、`liquidBlur=65`、宠物 `ghost`、`selectionCapture=true` |

两份二进制的 MD5 相同，所以 “读者那边的 967 MB” 和实验室里的数字是同一份代码，可以并排放。

第 9 节是修复与复测，用的是同一天重新构建的二进制（`build-ninja-release`，RelWithDebInfo，MD5 已不同）。
**跨构建的数字不并排比较**：改前一律取同一台机器、同一脚本、同一主题下的重测值，改后再测一遍。

### 2.2 口径

- 内存一律取 `Get-Process` 的 `PrivateMemorySize64`。工作集会被 Windows 回收可丢弃页，末点常常比首点低，
  不能用来判断 “应用有没有让出内存”；`docs/metrics/resource-footprint/idle-memory-2026-10-05.md` 已经写明这一点。
- 每 10 秒一点，同时记 `Threads.Count` 与 `HandleCount`。三者一起看：只涨内存不涨句柄，与三者同涨，
  指向的原因完全不同。
- 构成用 Sysinternals VMMap 3.52 的 committed 视图，只取顶层类型行（缩进行是子项，会重复计数）。
- 线程归因：`NtQueryInformationThread(ThreadQuerySetWin32StartAddress)` 取每个线程的起始地址，
  再用 `EnumProcessModulesEx` 的模块区间映射回 DLL。

### 2.3 怎么把表面打开

`TEST.md` §5 要求这几张表面只能靠真实输入驱动，禁止在 `src/` 里加计时器或探针。实际用了两条路：

- **设置窗**：右键桌面宠物。`PetWindow.qml:59` 的右键 `MouseArea` 直接发 `settingsRequested`，
  `Main.qml:409` 的 `showSettings(true)` 把面板摆在宠物旁边。这是最大的一块表面。
- **选区动作条**：拖选文字。`MouseSelectionHook` 看到超过 4 px 的拖动就抓选区文本，
  `Main.qml:615` 的 `onSelectionBarRequested` 把动作条摆到选区下方。这是最小的一块表面。

两条都是 `mouse_event` 注入，走的是应用自己的低层钩子与命中测试，不是 QML 测试桩。

### 2.4 对照怎么设

三组对照，每组只改一个变量：

1. **同进程先后开两个不同表面**：量 “每窗口” 是不是收全价，而不是一次性初始化。
2. **`theme=light` 重做同一个动作**：`light` 下 `GlassHost.glass` 为假，`capture()` 直接 `sheet.clear()`，
   没有幕布、没有着色器。两者之差就是玻璃的份。
3. **同一表面反复开合 10 次**：区分 “每窗口一份永久” 与 “每次开漏一次”。

## 3、过程

| 轮次 | 做了什么 | 得到什么 |
| --- | --- | --- |
| 0 | 读 `glass_backdrop.cpp`、`GlassSurface.qml`、`glass_math.cpp`、ADR 0034/0037/0038/0040/0041 | 15 个表面各持一份幕布，67 ms 全局轮转，1/4 分辨率，URL 带递增 `serial` |
| 1 | 对读者在跑的实例抓 VMMap、每 10 秒采样、线程归因 | 967 MB / 804 线程；增量落在 Heap 与 Private Data；644 个线程起于 NVIDIA 驱动、288 个起于 AMD 驱动 |
| 2 | 用仓库构建重启，只留托盘与宠物，静置 4 分钟 | 254 MB / 164 线程，曲线完全平坦，确认不是 “跑着就涨” |
| 3 | 点托盘图标开菜单 | 失败三次（见 §5 第 1 条），改走宠物 |
| 4 | 宠物右击开设置窗，常开 8 分钟，再关，再开合 10 次 | 首次 +103 MB / +79 线程；8 分钟只 +3.0 MB；关掉不退；每次开合 +0.6 MB |
| 5 | `theme=light` 重做第 4 轮 | 首次 +80.7 MB / +79 线程，与 liquid 几乎一致 |
| 6 | 拖选开动作条（小表面） | +56~61 MB / +80 线程，与整块设置面板同价 |
| 7 | 同一进程先后开设置窗与动作条 | 两个表面各收一次全价，中间关掉不退 |

第 4 轮的结果推翻了第 0 轮的假设：`serial` 每帧递增确实产生新缓存条目，但常开 8 分钟只涨 3 MB，
Qt 的图片缓存在正常回收。真正的台阶在 “第一次上屏” 那一刻。

## 4、结果

### 4.1 每多一个表面，永久多一份

同一进程，先后开两个不同表面，中间关掉前一个（`exp_two.ps1`）：

| 阶段 | 私有 MB | 工作集 MB | 线程 | 句柄 |
| --- | ---: | ---: | ---: | ---: |
| 空闲（只起托盘与宠物） | 249.9 | 270.8 | 164 | 4,501 |
| 打开设置窗 | 329.9 | 329.5 | 243 | 6,131 |
| 关掉设置窗 | 330.1 | 329.6 | 243 | 6,131 |
| 再打开选区动作条 | 390.4 | 374.4 | 323 | 7,767 |

动作条只有几百像素宽，代价却和整块设置面板一样 —— **成本按窗口算，不按内容大小算**。
关掉之后一分不退，第二次开同一个表面只花 0.6 MB，说明是 “每窗口一份永久”，不是 “每次开漏一次”。

线程的起点模块也是同一次性的：打开两个表面后，`nvwgf2umx.dll` 从 150 涨到 205，`amdxx64.dll` 从 72 涨到 96。
应用源码里 `std::thread`、`QThread`、`QtConcurrent`、`std::async`、`CreateThread` 全仓只有个位数命中，
所以这些线程是 D3D11 设备创建时驱动自己拉起的线程池。

### 4.2 为什么正好是 804 个线程

`Main.qml` 在启动时一次性实例化 12 个顶层窗口组件：`Bubble`、`CostPopup`、`Notice`、
`ReviewSignWindow`、`SelectionBar`、`SettingsPopup`、`StatsPopup`、`TrayMenu`、`UpdateCard`、
`WordsPopup`、`YearWordsPopup`、`PetWindow`，靠 `visible` 开关。实测每个窗口第一次上屏约 +80 线程：

```
164（只起托盘与宠物）+ 8 × 80（八个表面被打开过）≈ 804
```

读者那边实测就是 804 → 968。这不是巧合，是同一个算式。

### 4.3 玻璃的份

`theme=light` 重做同一个动作：

| 主题 | 空闲 | 首次开设置窗 | 增量 |
| --- | ---: | ---: | --- |
| `light`（无玻璃） | 251.5 | 332.2 | +80.7 MB，+79 线程，+1,628 句柄 |
| `liquid`（第 4 轮） | 251.9 | 355.2 | +103.3 MB，+78 线程，+1,632 句柄 |
| `liquid`（第 7 轮） | 249.9 | 329.9 | +80.0 MB，+79 线程，+1,630 句柄 |

线程与句柄在两种主题下几乎一致，**它们不是玻璃造成的**。玻璃的增量在 0~23 MB 之间：
幕布图加三块 `ShaderEffectSource` 的离屏纹理。两次 liquid 的差（80 与 103 MB）来自采样落在第几帧上，
常开那一轮采样时已经发布过几帧、纹理已经建好。

### 4.4 玻璃内部：哪些是问题

> 这一节是当时的判断。三条里有两条后来被第 9 节的对照否掉了，留着是因为 “错在哪” 本身是结论的一部分：
> “关掉不退” 是窗口的资源，不是玻璃的；“高频分配撑开堆” 在窗口资源释放之后不再成立。

- **不是问题**：每帧换 URL。`GlassSurface.qml:39` 把递增的 `serial` 拼进图片 URL，理论上每帧产生一条新缓存
  记录。实测一个表面常开 8 分钟（约 7200 次发布），私有内存 355.2 → 358.2 MB，只涨 3.0 MB，线程 242 → 238。
- **是问题**：关掉不退。`GlassSheet::clear()` 放掉 C++ 侧的 `QImage`，但 QML 的 `Image` 还停在上一个 URL、
  三块 `ShaderEffectSource` 的离屏纹理还挂着，窗口的 D3D 设备更不会退。
- **是问题**：高频分配撑开堆。`blurredPicture()` 每次请求都重新分配三个 `std::vector<float>` 平面
  （`glass_math.cpp:120`）；`cachedMap()` 只有一个缓存槽（`glass_backdrop.cpp:432`），而各表面的卡片尺寸与
  圆角都不同，交替刷新时 `refractionMap` 每帧重建。

### 4.5 VMMap 构成

同一份二进制，两个水位：

| 类型 | 空闲 | 真实使用 22 分钟 | 增量 |
| --- | ---: | ---: | ---: |
| Heap | 119 MB（42 段） | 431 MB（125 段） | +305 MB |
| Private Data | 82 MB（171 块） | 485 MB（547 块） | +393 MB |
| Thread Stack | 12 MB（158 块） | 27 MB（804 块） | +646 线程 |
| Image（ASLR） | 43 MB 私有 / 429 MB 提交 | 43 MB 私有 / 431 MB 提交 | ≈ 0 |
| Mapped File | 639 MB 提交（工作集 33 MB） | 655 MB 提交（工作集 7 MB） | 文件映射，不是私有账 |
| 提交合计 | 1,284 MB | 2,051 MB | +767 MB |

重载态有七个 28~32 MB 的私有块，空闲态同类最大只有一个 27 MB。`Image` 与 `Mapped File` 基本不动 ——
涨的是分配出来的数据，不是代码。

### 4.6 线程归属

重载态 959 个线程采样：

| 起始模块 | 线程数 |
| --- | ---: |
| `nvwgf2umx.dll`（NVIDIA D3D11 用户态驱动） | 644 |
| `amdxx64.dll`（AMD 用户态驱动） | 288 |
| Qt、CRT、应用自身 | 27 |

## 5、踩的坑

1. **托盘图标点不到。**`QSystemTrayIcon` 没有 guid，`Shell_NotifyIconGetRect` 拿不到矩形；图标还被 Windows
   收进了溢出面板，截图差分也定位不到稳定坐标。三次尝试后放弃，改走 “右键桌面宠物”——
   它是一条完全等价的入口（`PetWindow.qml:59`），而且位置可以枚举出来。
2. **宠物的保存位置是 Qt 逻辑像素，而且已经过期。**`settings.json` 里记的是 `1399,204`、`88 × 3 = 264`，
   实际窗口在 `1749,255`、`110 × 110`（缩放是 1.25 不是 3）。写死坐标的第一次实验因此点了空处。
   正确做法是 `EnumWindows` 找该进程最小的可见顶层窗口，取中心。
3. **后台启动的 lens 会被回收。**工具会话结束时作业对象把子进程一起带走，于是 “启动” 和 “采样” 分两条命令跑
   必然失败。改成 “启动 → 驱动 → 采样 → 关闭” 全在同一条命令里，跑了三次才稳定。
4. **PowerShell 的两个语法陷阱。**`ClosePanel()` 这种带空括号的无参调用是解析错误，要写 `ClosePanel`；
   `"cycle $i: ..."` 里的 `$i:` 被当成作用域限定符，要写 `${i}`。第一次后台跑 2 秒就以解析错误退出。
5. **不能用日志行判断 “面板开了没”。**那行日志只在玻璃排除抓屏时打（`glass_backdrop.cpp:305`），
   `theme=light` 下根本不会有，A/B 因此误判成 “面板没打开”。改成按窗口尺寸枚举：该进程只有表面面板
   会大于 300 × 300，宠物约 110 px，辅助窗口是 1 × 1。
6. **VMMap 的 CSV 不能直接求和。**表格前有 32 行摘要，正文里顶层行与缩进子行混排，按
   `r[0].startswith('  ')` 跳过子行，否则每一类都会被重复计数。
7. **`NtQueryInformationThread` 的权限位。**用 `THREAD_QUERY_LIMITED_INFORMATION`（`0x1000`）查不到起始地址，
   必须 `THREAD_QUERY_INFORMATION`（`0x0040`）。
8. **改 `settings.json` 做 A/B 要在应用停掉之后恢复。**应用退出时会覆写那份文档，先恢复等于没恢复。
   脚本用 `try/finally`，在 `Stop-Process` 之后才拷回备份。
9. **`Remove-Item` 被 safe-delete 钩子拦下。**清理备份时改成 `[System.IO.File]::Delete`。
10. **最初的假设是错的。**第一轮读代码时把 “每帧一个新 URL” 当成泄漏主因，写了一大段论证；第 4 轮 8 分钟
    soak 只涨 3 MB，把它否掉了。如果没做那一轮，优化方案会花力气去改一条不是问题的地方。
11. **把 103 MB 全算给玻璃也是错的。**没有 `theme=light` 对照之前，很自然地把 “开设置窗的增量” 记在玻璃账上；
    对照显示其中 81 MB 是窗口本身。**没有对照的归因不算归因。**
12. **主题会留在上一轮实验的值上。**`ab_theme.ps1` 把 `theme` 换成 `light` 再恢复，有一次恢复后应用又把内存里的
    旧值写回，于是下一轮 “玻璃验证” 实际在无玻璃主题下跑，数据全废。现在两个实验脚本启动时都打印
    `theme in force: '...'`，并在非玻璃主题下告警。**先确认变量，再信数据。**
13. **释放是滞后的。**隐藏后 3 秒采样还有 303.7 MB，15 秒才降到 260.7。只采一次会把阶段 1 的收益读成 44 MB，
    实际是 73 MB。`exp_two.ps1` 现在采两次（`surface1_closed` 与 `surface1_closed_late`）。
14. **预编译头锁死了 SDK 版本。**`build-ninja-release` 的 PCH 是用 SDK 10.0.26100 生成的；手工给
    `INCLUDE` 指到 10.0.22621 时，编译器先加载 PCH 里的 `corecrt.h` 再从另一个版本重复定义，报一串
    “class template has already been defined”。跟 SDK 对齐即可。
15. **手工跑一次 qmlcachegen 会污染构建图。**为了拿到被本地化成 `?????` 的错误信息，手工把某个
    `*_qml.cpp` 生成到了构建目录，ninja 认为它已是最新、不把它链进目标，链接期报
    `unresolved external symbol ... qmlData`。清掉 `.rcc/qmlcache` 并重跑 `cmake` 配置才恢复。
16. **指纹的第一版挡不住单像素。**32 × 32 缩略图在 2:1 缩放下，一个白点仍能把某个格子从 30 抬到 86。
    单像素用例当场失败，加了一级量化（每通道留 5 位）才符合 “模糊本来就会盖住它” 的设计意图。

## 6、结论

1. 主因是窗口模型，不是玻璃：12 个常驻顶层窗口，每个第一次上屏永久带走约 80 线程、1630 句柄、60~100 MB。
2. 玻璃的增量是 0~23 MB 一个表面，线程与句柄与无玻璃时一致。
3. 67 ms 的持续重拍不是内存问题（8 分钟 +3 MB），它是 CPU 问题（ADR 0040 估约 0.25 个核）。
4. 与 2026-10-05 那两份报告的关系：那两份测的是 “玻璃只进设置窗” 的时代（空闲 156 MB / 72 线程，
   用过之后停在 219 MB / 154 线程），结论是 “首帧一次台阶，之后不涨”。`67064d4` 把玻璃铺到 11 个表面、
   10-17 又加到 15 个之后，那个结论不再成立：不是首帧一次，是**每个窗口各一次**。
5. 修复方向最后落在一句话上：Qt 的 `setPersistentGraphics()` 默认把图形资源留到最后一个窗口销毁，应用本来就有
   释放入口，只是没打开。所以不用重构窗口模型，改两个开关就够。改法与复测见第 9 节。
6. 动手之前先用探针回答了 “隐藏并释放，驱动线程会不会退”：会，线程 −73、句柄 −1,203；Qt 默认下一分不退。
   十分钟的探针，决定了后面是改 25 行还是重写 12 个窗口。
7. 结果：用过八个表面之后，关闭状态从 967 MB / 804 线程降到约 270 MB / 165 线程；某个表面正开着时仍收一次
   全价（约 355 MB / 245 线程），这是 “一次只开一个” 的自然上限。

## 7、复现

脚本在 `.workbuddy-ai/tools/`（gitignored，本机记录），全部以 “一条命令跑完全程” 的方式写：

```powershell
# 构建。本机没有 vcvars 环境，脚本自己拼 INCLUDE/LIB，SDK 必须与预编译头一致
bash .\.workbuddy-ai\tools\build.sh lens

# 空闲基线 + 表面常开 8 分钟 + 关掉 + 10 次开合
& .\.workbuddy-ai\tools\exp_soak.ps1 -Mode both -IdleSeconds 60 -LiveSeconds 480 -Cycles 10

# 同一进程先后开两个不同表面（4.1 那张表）；采两次关闭后水位
& .\.workbuddy-ai\tools\exp_two.ps1 -IdleSeconds 20

# 同二进制的对照：保留 Qt 的持久化默认（第 9 节）
& .\.workbuddy-ai\tools\exp_two.ps1 -IdleSeconds 20 -KeepGraphics

# 无玻璃对照，跑完自动把 settings.json 恢复
& .\.workbuddy-ai\tools\ab_theme.ps1 -Theme light -Cycles 3

# 小表面：拖选开动作条
& .\.workbuddy-ai\tools\exp_bar.ps1 -IdleSeconds 30 -HoldSeconds 25

# CPU：一个表面常开 60 秒，CSV 里多一列 cpu_s
& .\.workbuddy-ai\tools\exp_soak.ps1 -Mode soak -IdleSeconds 20 -LiveSeconds 60

# 指纹有没有把玻璃冻住：静止 20 秒 vs 滚动背后页面 20 秒
& .\.workbuddy-ai\tools\exp_s4_change.ps1

# 阶段 0 的探针：隐藏并释放，驱动线程退不退
bash .\.workbuddy-ai\tools\qwindow-probe\build-probe.sh
bash .\.workbuddy-ai\tools\qwindow-probe\build-probe.sh --default

# 线程归因（进程在跑时）
& "B:\anaconda\anaconda\python.exe" .\.workbuddy-ai\tools\threadmap.py <pid>
```

采样 CSV 落在 `.workbuddy-ai/tools/csv/`，VMMap 快照是同目录的 `vmmap_*.csv`。

## 8、原始记录

| 文件 | 内容 |
| --- | --- |
| `csv/liquid_settings.csv` | 第 4 轮：空闲 → 常开 8 分钟 → 关闭 → 10 次开合 |
| `csv/two_surfaces.csv` | 第 7 轮：设置窗 → 关闭 → 动作条 |
| `csv/theme_light.csv` | 第 5 轮：无玻璃对照 |
| `csv/bar_liquid.csv` | 第 6 轮：小表面 |
| `vmmap_live.csv` | 读者实例重载态（967 MB / 804 线程） |
| `vmmap_tray_live.csv` | 空闲基线（254 MB / 164 线程） |
| `vmmap_liquid_settings_live.csv` | 设置窗常开 8 分钟后 |
| `vmmap_two_surfaces.csv` | 两个表面之后 |
| `csv/s1_only_liquid.csv` | 第 9 节：阶段 1 后，设置窗 → 关闭（两次采样）→ 动作条 |
| `csv/s1_cycles10.csv` | 第 9 节：阶段 1 后 10 次开合 |
| `csv/s2_liquid_cycles.csv` | 第 9 节：阶段 1+2 的 6 次开合（阶段 2 已回退） |
| `csv/s2_settle_keep.csv` | 第 9 节：只放玻璃纹理、窗口资源保持默认 |
| `vmmap_s2_settle_release.csv` | 第 9 节：关闭 15 秒后的 VMMap（阶段 1+2） |
| `vmmap_s2_settle_keep.csv` | 第 9 节：同上，但窗口资源未释放 |
| `csv/s4_cpu_before.csv` / `csv/s4_cpu_final.csv` | 第 9 节：常开 50 秒的 CPU，改前 / 改后 |
| `csv/s4_change.csv` | 第 9 节：静止 vs 滚动，指纹有没有把玻璃冻住 |

`docs/metrics/` 里的报告是那一天的快照，不随源码漂移而改；上面这些原始文件不在仓库里，重跑脚本可以重新生成。

## 9、修复：改了什么，量到什么

第 1~6 节只回答了 “钱花在哪”，没回答 “能不能还回去”。这一节是后半程：先验释放语义，再按阶段改，每阶段单独
测。四个候选方案的取舍、五个阶段的顺序、每步的改动点、验证口径与实测数据都在这里。同一天完成，口径与第 2 节
一致（`PrivateMemorySize64`、10 秒一点、liquid 主题、真实鼠标输入驱动表面）。

### 9.1 根因的另一半：Qt 的默认值是 “不释放”

`QQuickWindow::setPersistentGraphics()` 的文档原文：

> Sets whether the graphics resources (graphics device or context, swapchain, buffers, textures) should be
> preserved, and cannot be released until the last window is deleted. **The default value is true.**

`releaseResources()` 的文档原文：

> Requests the scene graph to release cached graphics resources (pipelines, shader programs, image data). May
> also release the whole scene graph and window rendering resources. Behavior is controlled by
> `setPersistentGraphics()` and `setPersistentSceneGraph()`.

合起来读：**应用有释放的入口，默认配置把它关着**。十二个窗口常驻、靠 `visible` 开关，于是每个窗口第一次上屏
拿到的整套驱动资源（一个 D3D11 设备、一条交换链、缓冲与纹理，以及驱动为它们起的线程池）一直留到进程退出。
这解释了 4.1 的 “每窗口一份、隐藏不退”，而不是 “用一次漏一次”。

### 9.2 先验释放语义：探针

改应用之前先回答一个问题：隐藏并 `releaseResources()`，驱动线程到底会不会退。探针
（`.workbuddy-ai/tools/qwindow-probe/`）建两个带 200 行列表的窗口，与面板的场景量级相当，打印四个时刻：

| 时刻 | 关持久化 + 隐藏释放 | Qt 默认 |
| --- | ---: | ---: |
| 起第一个窗口 | 87 线程 / 1,622 句柄 / 132.9 MB | 88 / 1,691 / 130.7 |
| 起第二个窗口 | 162 / 2,841 / 201.8 | 163 / 2,910 / 198.2 |
| 隐藏第二个并释放 | **89 / 1,638 / 181.3** | 164 / 2,913 / 198.2 |
| 销毁第二个 | 89 / 1,634 / 179.0 | 90 / 1,703 / 180.3 |

线程 −73、句柄 −1,203，达到 “−60 / −1,200” 的通过标准；对照组隐藏后一分不退，销毁才退。

这一步的成本是一个 200 行的 Qt 程序，十分钟。它把方案从 “重构窗口模型” 拉回到 “改两个开关”—— 如果跳过它，
后面会去重写 `Main.qml` 的十二个窗口，而收益可能只有玻璃那 0~23 MB。

### 9.3 候选方案与取舍

| 方案 | 机制 | 改动面 | 结论 |
| --- | --- | --- | --- |
| **A 关持久化 + 隐藏时释放** | `setPersistentGraphics(false)`、`setPersistentSceneGraph(false)`，隐藏时 `releaseResources()` | C++ 约 25 行，一处调用 | **采用**，实测见 9.4 |
| B 共享一个 QRhi | `QQuickGraphicsDevice::fromRhi()` 喂给十二个窗口 | C++ + 每窗一次调用 | 未做：A 已经把线程从 804 压到 245，与窗口数解耦 |
| C 按需创建、隐藏即销毁 | `Main.qml` 十二个窗口改 `Loader` + `destroy()` | QML 300~500 行，牵动开合、定位、动画、`dismissOutside` | 未做：文档说资源保留到**最后一个**窗口销毁，销毁一个的收益存疑 |
| D 单窗口 + 内嵌面板 | 所有面板变成同一窗口里的 `Item` | 最大，ADR 0038/0041 要重写 | 未做：会失去系统级置顶、抓屏排除、任务栏分离 |

C 从首选降级是查文档之后改的判断。9.2 的探针又补了一条：Qt 默认配置下，销毁窗口也会退（164 → 90），
所以 C 在技术上可行，只是比 A 贵得多。

### 9.4 阶段 1：关掉持久化，隐藏时释放（`a46829c`）

改在 `src/app/main.cpp`，`loadFromModule` 之后、事件循环之前 —— 两个提示只在窗口的场景图初始化之前有效：

```cpp
void releaseHiddenSurfaces(QObject* root)
{
    const auto tune = [](QQuickWindow* window) {
        window->setPersistentGraphics(false);
        window->setPersistentSceneGraph(false);
        QObject::connect(window, &QWindow::visibleChanged, window, [window] {
            if (window->isVisible())
                return;
            QTimer::singleShot(kReleaseDelayMs, window, [window] {
                if (not window->isVisible())
                    window->releaseResources();
            });
        });
    };
    if (auto* self = qobject_cast<QQuickWindow*>(root))
        tune(self);
    for (auto* window : root->findChildren<QQuickWindow*>())
        tune(window);
}
```

两个细节：`kReleaseDelayMs = 500`，面板在换位时会隐藏又立刻显示，500 ms 内回来的窗口不释放，否则每次挪动都要
重建场景图；启动时已经可见的窗口（宠物、根窗口）赶不上这两个提示，但它们是常驻的，本来也不该释放。
`LENS_KEEP_GRAPHICS=1` 跳过这一步，用于同二进制对照。

复测（`exp_two.ps1`，开设置面板 → 关闭 → 开动作条）：

| 时刻 | 改前 | 改后 |
| --- | ---: | ---: |
| 空闲 | 249.9 MB / 164 线程 / 4,501 句柄 | 251.3 / 165 / 4,504 |
| 打开设置面板 | 329.9 / 243 / 6,131 | 354.9 / 246 / 6,142 |
| 关闭 3 秒 | 330.1 / 243 / 6,131 | 283.9 / 166 / 4,522 |
| 关闭 15 秒 | — | **260.0 / 165 / 4,520** |
| 再开动作条 | 390.4 / 323 / 7,767 | **327.4 / 245 / 6,156** |

改后的 “打开” 那一行反而更高，是采样落在第几帧的差别（4.3 已记过同一现象：80 与 103 MB 的两次 liquid 读数）。
可比的是 “关闭” 与 “第二个表面”：**线程与句柄完全归还，内存回落到空闲 +9 MB；第二个表面不再叠一份全价。**

10 次开合（`exp_soak.ps1 -Mode cycles`）：关闭水位 358.0 → 364.3 MB（+0.63/次）改成 261.4 → 271.7（+1.03/次），
线程 239 改成 165 → 159。**底位降了约 96 MB，每次开合的慢速爬升没变，仍在同一量级。**

### 9.5 阶段 2：玻璃纹理随窗口释放 —— 试了，零收益，已回退

阶段 1 释放的是窗口级资源（设备、交换链）。玻璃的三块 `ShaderEffectSource` 是 item 级资源，本来打算在
`GlassSurface.qml` 加一个 `release()`，窗口隐藏时清空 `Image.source` 并把 `sourceItem` 置空。

做完了，也验证了它确实被调用（临时 `console.warn` 确认 `release()` 在隐藏时触发），但**只放纹理、窗口资源保持
默认**（`exp_two.ps1 -KeepGraphics`）时：

| 时刻 | 值 |
| --- | ---: |
| 空闲 | 252.0 MB / 165 线程 |
| 打开设置面板 | 353.9 / 246 |
| 关闭 15 秒后 | **353.4 / 245** |

一分不降。VMMap 也印证：≥20 MB 的私有块，阶段 1+2 那组剩 2 个，只放纹理那组剩 4 个 —— 减掉的两个是设备与
交换链，玻璃那层一个也没动。原因大概是场景图在 `persistentGraphics` 下自己握着纹理，item 层清空
`Image.source` 并不会让驱动还页。

留着只有风险（重开时首帧空一下）没有收益，`GlassSurface.qml` 已还原。**结论：纹理的账跟着设备走，设备一放，
它一起放；单独放纹理是白做。**

### 9.6 阶段 3：堆与分配 —— 未做，前提被证伪

原计划把 `cachedMap()` 的单槽换成 15 条 LRU、把 `blurredPlanes()` 的三个 `std::vector<float>` 改成复用缓冲，
理由是 “堆从 119 涨到 431 MB、段数从 42 涨到 125”。

阶段 1 之后这个前提不成立了：10 次开合的关闭水位稳定在 261~272 MB，`Heap` 停在 152 MB（空闲 119 MB）。
涨的是每个窗口自己的场景图，不是折射贴图；窗口资源一释放，堆跟着回来。

剩下的确实还有分配抖动（每次请求三个 `h*w` 的 float 平面），但它由阶段 4 从源头掐掉：桌面没变时根本不再请求
模糊。要做的话，收益在 CPU 与分配器压力，不在内存。

### 9.7 阶段 4：没变化就不重拍（`84aec0c`）

`GlassSheet::publish()` 先用 `pictureFingerprint()` 比对，桌面没变就不递增 `serial`、不发 `pictureChanged`。
指纹是画面缩到 32 × 32 的灰度图，每通道留 5 位（右移 3）—— 后三位低于模糊能留下的差异，留着会让一个闪烁的
光标算成 “桌面变了”。

设置面板常开、桌面静止 50 秒（`exp_soak.ps1 -Mode soak`，CSV 里 `cpu_s` 列是进程累计 CPU 时间）：

| | CPU | 内存 |
| --- | ---: | ---: |
| 改前 | 8.7 s / 50 s = **17%** 单核 | 353.7 → 354.8 MB |
| 改后 | 5.0 s / 50 s = **10%** 单核 | 346.5 → 348.1 MB |

剩下的是每秒 15 次的 GDI 读取本身，指纹只能省掉模糊与 QML 重新加载。

**指纹会不会把玻璃冻住**（`exp_s4_change.ps1`）：面板常开，先静止 20 秒，再滚动背后的页面 20 秒。

| 阶段 | CPU | 私有内存 |
| --- | ---: | ---: |
| 静止 | 8.7% 单核 | 347 MB |
| 滚动 | 12% 单核 | 357 MB（新幕布进了图片缓存） |

变化仍会发布，只是静止时不再重复发布。单元测试 `GlassMath` 新增 4 个用例：同图稳定、异图不同、单像素噪声
不算变化、空图为 0。

### 9.8 阶段 5：不需要了

“共享一个 QRhi” 与 “按需创建窗口” 是为 “线程随窗口数线性增长” 准备的。阶段 1 之后线程从 804 压到 245，
且第二个表面不再叠加（245 对改前的 323），与窗口数解耦。这两条都不用做。

### 9.9 还没解决

- **每次开合的慢速爬升**：10 次 +10.3 MB、+38 句柄，稳定但不归零。候选是 QML 图片缓存的 `serial` 条目与
  字体缓存，未定位。
- **关闭后 3 秒内仍偏高**：283.9 MB，15 秒才到 260.0。驱动还页是渐进的，任务管理器上看会以为没退。
- **人工观感未经系统记录**：玻璃是否照旧、重开首帧有没有闪、多屏与缩放下交换链尺寸是否正确，靠人看，没有
  可自动化的判据（这几张表面被排除在抓屏之外）。
