# 空闲水位归因：启动平台、玻璃抓屏管线与表面的价

## 对象与口径

三次只读测量，同一天、同一台机器、没有管理员权限：

1. **release 启动序列**（第 1 轮）：`build-ninja-release/src/app/lens.exe`，2026-11-20 20:20:16.8 启动，PID 17868，`Lens is up` 写在 20:20:20.1，20:30:20 停止。VMMap 快照在启动后 10 秒、60 秒、600 秒各一次。
2. **release 启动序列**（第 2 轮）：同一二进制，20:35:40 启动，PID 20264，补第 1 轮 600 秒那次空导出，并在同一进程里做线程归因、开合设置与托盘两张表面，20:46:20 退出。
3. **dev 树逐表面**：`build-ninja-dev/src/app/lens.exe`（Debug，`LENS_ENABLE_DEV_SURFACE=ON`），20:31:07 启动，PID 24064，20:32:39 收到 `quit` 后退出。`scripts/qa/scenarios/soak-surfaces.txt` 的七张表面各开合一轮，随后在同一进程里做宠物与主题的 A/B。

两棵树的时新性：`build-ninja-release/src/app/lens.exe` 写于 2026-11-20 14:20:21，比 `src/` 最后一个提交（`2f4c3977`，14:15:32）新，等于 HEAD；`build-ninja-dev/src/app/lens.exe` 写于 2026-11-18 23:39，落后 HEAD 12 个提交，含今天的玻璃改动（`2f4c3977` 一档致密核、`eb237960` 铅笔层角标），所以 dev 树的绝对数只能与本轮内部比。

读者设置没有被本测量改动。release 两轮用的是 `%APPDATA%\Lens\settings.json`（`theme=liquid`、`blur=20`、`quality=3`、宠物 mochi、`autoScan=true`、`uiLanguage=en`），dev 轮用 `--settings-file=logs/dev-run/attr-settings.json` 的独立文档（缺省值，`theme=light`、宠物 mochi、扫描关）。release 运行期间应用自己写了 25 次 `settings saved`，并在第 1 轮 20:28 因扫描轨读到屏幕上的英文而写下一条判定（`marked` 267 → 269）：这是 `autoScan=true` 的既有行为，不是本测量的写入，但读水位时要把 “桌面有内容” 算进前提。

命令（逐字）：

```
powershell -NoProfile -ExecutionPolicy Bypass -File logs/attr/part1-release-series.ps1
.workbuddy-ai\tools\vmmap64.exe -p 17868 D:\lens-dev\logs\vmmap-attr-release-010s.csv
.workbuddy-ai\tools\vmmap64.exe -p 17868 D:\lens-dev\logs\vmmap-attr-release-060s.csv
.workbuddy-ai\tools\vmmap64.exe -p 17868 D:\lens-dev\logs\vmmap-attr-release-600s.csv
powershell -NoProfile -ExecutionPolicy Bypass -File logs/attr/part2-dev-surfaces.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File logs/attr/part3-release-extra.ps1
.workbuddy-ai\tools\threadmap.py 20264
```

dev 轮的管道命令只有 `version`、`state`、`surface show|hide`、`wait visible|hidden|painted`、`setting set`、`quit`，与 `docs/qa.md` 的首选路径一致；没有合成鼠标、没有键盘注入、没有截图。驱动脚本与原始 CSV 在 `logs/attr/` 与 `logs/`（gitignored）。

30 分钟的那个快照直接复用 `logs/vmmap-idle-20261120.csv`（PID 33948，20:07 取），没有重跑。

并发负载：另一代理在 `build-ninja` 上作业。开测时 20:27 的探针里没有 `cl.exe` / `ninja.exe` / `cmake.exe`，系统 CPU 25%；扫描轨全程在跑。

## 结果

### 一、release 启动序列

| 时刻 | 私有 MB | 工作集 MB | 线程 | 句柄 | VMMap 私有合计 | 堆 | Private Data | Image | 栈 | 提交 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 s（第 1 轮） | 288.3 | 336.8 | 153 | 4,788 | 284.4 | 147.6 | 80.8 | 44.6 | 11.5 | 1,478.8 |
| 60 s（第 1 轮） | 289.2 | 337.9 | 152 | 4,788 | 285.4 | 148.5 | 81.0 | 44.6 | 11.4 | 1,479.8 |
| 600 s（第 1 轮） | 316.2 | 342.5 | 184 | 4,979 | 导出为空 | — | — | — | — | — |
| 10 s（第 2 轮） | 289.2 | 338.6 | 153 | 4,834 | 285.9 | 148.0 | 81.8 | 44.6 | 11.5 | 1,480.0 |
| 60 s（第 2 轮） | 290.4 | 344.4 | 154 | 4,891 | 288.2 | 148.0 | 84.0 | 44.7 | 11.6 | 1,487.2 |
| 600 s（第 2 轮） | 304.8 | 334.8 | 150 | 4,894 | 305.0 | 161.5 | 87.3 | 44.7 | 11.6 | 1,504.9 |
| 30 分钟（既有） | 306.5 | 309.0 | 180 | 4,941 | 304.6 | 161.1 | 86.6 | 44.7 | 12.2 | 1,504.6 |

堆的块数：632（10 s，第 1 轮）、644（60 s，第 1 轮）、658（10 s，第 2 轮）、660（60 s，第 2 轮）、744（600 s，第 2 轮）、721（30 分钟）；最大一块从头到尾是 15.8 MB，没有变化。

第 1 轮 600 秒那次导出只有表头，`Process:` 与 `PID:` 都是空串；同一时刻 `Get-Process` 读得到 316.2 MB，所以不是进程退出。第 2 轮的五次导出（600 秒一次、两张表面各两次）都拿到了完整地图，但文件都在 CLI 返回之后数秒才落盘。

第 1 轮 60 秒到 600 秒之间多了 27.0 MB 私有、32 个线程、191 个句柄。日志在同一时段只有两件事：20:28:06 铅笔层（`pencil_duty.cpp:46`，全屏、`passthrough=true`）第一次显示，20:28:08 写下一条判定（`verdict marked: 'unbind' -> mastered`），随后是成批的 `settings saved`。这 27 MB 里有多少是铅笔层本身、多少是同批保存，本轮切不开；但线程与句柄的台阶是一次窗口首显的签名。

第 2 轮没有抬起任何表面：600 秒时线程 150，比 60 秒少 4 个；堆却从 148.0 涨到 161.5 MB，Private Data 从 84.0 涨到 87.3 MB。也就是说，**在没有新窗口的情况下，堆也会在头十分钟涨 13.5 MB**，而且涨到的正是 30 分钟基线的 161.1 MB。

### 二、代码级估算：随包数据能占多少

条目数取自本次 release 启动的日志（`logs/lens.log`，20:20:16 到 20:20:17），每项开销按 MSVC 标准库的容器布局推算（`std::string` 32 字节、SSO 15 字符以内不另分配，`_List_node` 两个指针，malloc 按 16 字节取整）。

| 结构 | 条目 | 每项字节 | 小计 | 依据 |
| --- | ---: | ---: | ---: | --- |
| `g_rank`（`unordered_map<string, size_t>`，`filter_core.cpp:23`） | 152,058 | 64 | 9.3 MB | 节点 16 + 串 32 + 值 8 = 56，取整到 64 |
| 词表中 15 字符以上的键 | 2,811 | 32 | 0.1 MB | 平均词长 8.89，中位 9 |
| `g_rank` 的桶数组 | 约 152,000 个桶 | 8 | 1.2 到 1.6 MB | 素数桶数，负载因子 1 |
| `g_scanPool`（`unordered_set<string>`） | 78,669 | 48 | 3.6 MB | 节点 16 + 串 32 |
| `g_scanPool` 的桶数组 | 约 78,000 个桶 | 8 | 0.8 到 1.0 MB | 同上 |
| `g_books`（`unordered_map<string, uint8_t>`） | 14,311 | 64 | 0.9 MB | 节点 16 + 串 32 + 1，取整到 64 |
| `g_irregulars`（`unordered_map<string, vector<string>>`） | 5,757 | 80 | 0.5 MB | 节点 16 + 串 32 + 向量 24，另加基部串约 0.2 |
| `g_informal` 与读者词库（known 23、mastered 198、cached 242） | 约 580 | 48 | 0.03 MB | 日志 `settings loaded` |
| LLM 协议（11 通道，`data/llm`） | 11 | — | 约 0.2 MB | 4 KB 目录加各通道提示词 |
| **合计** | | | **17 到 18.5 MB** | 占 10 秒堆 147.6 MB 的 **12%** |

四条旁证：

- 词表文件 1.43 MB、扫描池 0.72 MB、词书 8 份共 0.44 MB、不规则表 0.10 MB（按 MiB 计，`wc -l` 与 `ls -la`），读进堆后放大 6 到 8 倍，与上表一致。
- 字体 `C:/Windows/Fonts/NotoSansSC-VF.ttf` 17.8 MB，VMMap 里是 **Mapped File**，提交 17,360 KB、工作集 300 KB、私有 0（`vmmap-attr-release-010s.csv`，`000001CEA5790000` 行）。可变字体被 DirectWrite 解析后是否另有私有副本，本轮没有工具能看见，不计入上表。
- QML 25,360 行、117 个文件、368 个 `Text`、61 个 `Repeater` / `ListView`，全部经 qmlcachegen 编进 19.2 MB 的 exe；没有 QML 磁盘缓存。`Main.qml` 的根项下直接实例化 25 个顶层组件、没有 `Loader`，dev 面 `state` 报 `windows.total=29`。
- 宠物 mochi 在磁盘上 410 KB（`sprites` 93 KB），解码后 1 到 2 MB 量级；`onnxruntime.dll`（sherpa-onnx 的依赖，Image 16.4 MB）只在首次发声时建 TTS 引擎，空闲时不建会话。

结论先说：**随包数据解释不了 148 MB 的堆**，它能解释的是 17 到 18.5 MB。

### 三、线程归属（release，第 2 轮 60 秒）

`.workbuddy-ai/tools/threadmap.py 20264`，154 个线程：

| 起始模块 | 线程数 |
| --- | ---: |
| `nvwgf2umx.dll`（NVIDIA D3D11 用户态驱动） | 79 |
| `amdxx64.dll`（AMD 驱动） | 48 |
| `Qt6Core.dll` | 8 |
| `ntdll.dll` | 7 |
| `ucrtbase.dll` | 4 |
| `UIAutomationCore.DLL` | 2 |
| `lens.exe`、`mswsock`、`combase`、`msvcrt`、`WINMM`、`amdihk64` | 各 1 |

**127 / 154（82%）的线程起于两家 GPU 驱动**。形态与 10-18 报告 §4.6 一致（那台重载态实例采样了 959 个线程，其中 644 个起于 `nvwgf2umx`、288 个起于 `amdxx64`）；量级不同是因为这边只是空闲、只建了一个共享设备（ADR 0191）。一个共享 D3D11 设备之后，两家驱动的线程池仍然都在进程里。

### 四、dev 树逐表面（Debug，只看本轮内的差）

七张表面全部 `show` → `wait painted` → 快照 → `hide` → `wait hidden` → 等 5 秒 → 快照，**没有一次绘制等待被拒**。基线（`theme=light`、宠物开、扫描关、`windows.total=29`）：私有 362.9 MB、工作集 447.1 MB、184 线程、4,955 句柄。

| 表面 | show 后私有 MB | show 增量 | hide 后 5 s | 回收 | 线程 | 句柄 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| settings | 381.4 | +18.5 | 381.3 | −0.1 | 185 → 184 | 4,979 → 4,969 |
| tray | 382.0 | +0.7 | 374.0 | −8.0 | 185 → 184 | 4,983 → 4,971 |
| stats | 380.0 | +6.0 | 372.1 | −7.9 | 185 → 183 | 4,990 → 4,975 |
| words | 377.6 | +5.5 | 371.0 | −6.6 | 184 → 183 | 4,993 → 4,979 |
| yearWords | 383.6 | +12.6 | 381.1 | −2.5 | 184 → 183 | 4,997 → 4,984 |
| cost | 386.7 | +5.6 | 382.3 | −4.4 | 184 → 183 | 5,002 → 4,989 |
| profile | 385.6 | +3.3 | 375.9 | −9.7 | 184 → 183 | 5,007 → 4,994 |
| 七轮之后 | 375.9 | 累计 +13.0 | — | — | 183 | 4,994 |

共享设备之后，表面的首显价从 10-18 报告的 60 到 100 MB 落到 1 到 19 MB。`settings` 那一行是首次上屏把字体、图片与场景图缓存一次性建好，之后不再回收；其余六张在隐藏后 5 秒回收 2.5 到 9.7 MB。七轮累计残留 +13.0 MB、句柄 +39，Debug 数，不外推到 release。

配置 A/B 也在同一进程里做完（`setting set`，只改本次的 `logs/dev-run/attr-settings.json`）：

| 步骤 | 私有 MB | 线程 | 句柄 | 相对上一步 |
| --- | ---: | ---: | ---: | --- |
| 七轮之后 | 375.9 | 183 | 4,994 | — |
| 宠物开（本来就是开） | 375.1 | 183 | 4,990 | −0.8（噪声） |
| 宠物关 | 338.2 | 103 | 3,365 | **−36.9 MB、−80 线程、−1,625 句柄** |
| 换 liquid 主题 | 593.1 | 137 | 4,582 | **+254.9 MB、+34 线程、+1,217 句柄** |
| 换回 light | 485.2 | 137 | 4,582 | −107.9 MB，仍比换主题前高 147.0 MB |

宠物那一行是干净的：隐藏宠物窗口后 `releaseHiddenSurfaces()` 在 500 ms 后释放，5 秒内线程与句柄几乎全退，Debug 下值 36.9 MB、80 线程。主题那两行不是 “liquid 比 light 贵 255 MB”：dev 日志显示换主题时玻璃后台把 onboarding 窗口当作可见表面，20:32:19.5 起了 Windows Graphics Capture（`glass_capture.cpp:272`，`device on AMD Radeon(TM) Graphics`），+255 MB 是那条抓屏管线，换回 light 后管线停下，10 秒内只退 108 MB。10-18 报告 §4.3 的 0 到 23 MB 是跨进程、按主题重启的口径，两者不可比。

### 五、release 树的一张玻璃表面（附加）

第 2 轮在同一进程里用开发面开了两表面，每步前后各采一次 `Get-Process`，导出 VMMap。基线是 600 秒的 304.8 MB。

| 步骤 | 私有 MB | 工作集 MB | 线程 | 句柄 | VMMap 合计 | 堆 | Private Data |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 600 秒空闲 | 304.8 | 334.8 | 150 | 4,894 | 305.0 | 161.5 | 87.3 |
| 设置面板显示中 | 327.4 | 346.4 | 186 | 5,042 | 616.2 | 186.5 | 371.7 |
| 设置面板隐藏后 5 秒 | 491.2 | 394.1 | 219 | 6,238 | 486.9 | 183.8 | 245.1 |
| 托盘面板显示中 | 494.7 | 396.7 | 221 | 6,263 | 459.8 | 186.8 | 214.9 |
| 托盘面板隐藏后 5 秒 | 421.5 | 386.2 | 220 | 6,246 | 417.2 | 183.8 | 175.4 |

两列数字的差不是矛盾，是时间：`Get-Process` 在命令返回后立刻读，VMMap 的导出要几秒才落盘，所以 “显示中” 那一行的地图读到了更晚的状态。时间线由日志给出：

- 20:45:46.6 设置面板显示，46.9 起 `glass refresh on the smooth tier paced at 6 ms: the 144 Hz screen sets it`；
- 20:45:46.9 三块屏与适配器列出（NVIDIA 两屏，AMD 无屏），47.0 日志 `glass capture ... Windows Graphics Capture, 1920x1080, device on AMD Radeon(TM) Graphics`；
- 20:45:52.7 设置面板隐藏，20:46:03.8 托盘显示，20:46:09.9 托盘隐藏，20:46:20.9 退出。

读法是三件事：

1. **一张玻璃表面自己的 QML 与堆只值 22 到 25 MB**（堆 161.5 → 186.5，5 秒后 183.8）。这与 dev 树 `settings` 的 +18.5 MB 同量级。
2. **贵的是它点燃的实时抓屏管线**：第一次显示玻璃表面会建 Windows Graphics Capture 会话，并在 **AMD 适配器上另建一个 D3D11 设备**（两块屏都挂在 NVIDIA 适配器上，应用的共享设备由此而建，`surface_graphics.cpp:61`），这一步在 4 秒内把 Private Data 从 87.3 抬到 371.7 MB，同时 +36 线程、+148 句柄；那一刻的导出读到 616.2 MB 私有，是 “显示中” 的最高读数，但不一定是真峰值。
3. **它退得慢**：隐藏最后一张表面 5 秒后仍有 421.5 MB，Private Data 175.4 MB（比空闲多 88.1 MB），堆多 22.3 MB。10-18 报告 §9.9 记过驱动还页要 15 秒量级，本轮没有取 15 秒与 60 秒的点，所以 “回落到多少” 还答不了。

另外一条与本轮结论直接相关的发现：**release 树带开发面**。`version` 答 `{"build":{"config":"optimized","devSurface":true,"qmlDebug":false}}`，`state`、`surface show|hide`、`wait painted` 全部可用，`state.scan.autoScan=true`、`pet=mochi`、`windows.total=29`。`build-ninja-release/CMakeCache.txt` 里确实是 `LENS_ENABLE_DEV_SURFACE:BOOL=ON`。`idle-sample-2026-11-20.md` 里 “release 树没有开发面” 的判断不成立：2026-11-20 这棵 release 树可以逐表面测量，不需要重建。

## 读法

### 161 MB 的堆是启动平台，不是增长

10 秒时堆已经是 147.6 到 148.0 MB，两轮在 10 秒与 60 秒两个点上只差 0.9 与 1.2 MB；30 分钟值是 161.1 MB，**91% 的水位在启动后 10 秒就位**。

剩下的 9% 分两种，都不是泄漏：

- **十分钟的暖机，+13.5 MB**：第 2 轮没有任何新窗口（线程 150 比 60 秒少 4 个），堆从 148.0 涨到 161.5、Private Data 从 84.0 涨到 87.3，然后停住；30 分钟基线从第 650 秒到第 1602 秒的斜率只有 +0.0014 MB/s。这一段日志里没有事件，未归因。
- **表面首显的台阶**：第 1 轮 20:28 的铅笔层首显（60 秒到 600 秒的 +27 MB、+32 线程、+191 句柄，其中有多少是铅笔层本轮切不开），30 分钟基线的 20:02（+3.8 MB、+34 线程、+121 句柄，同时多出一张 65 × 33 的窗口）。水位取决于桌面上有没有可读的英文、扫描轨要不要抬层，不取决于跑了多久。

### 谁占着：应用数据 12%，其余没有 owner

| 层 | 量 | 证据强度 |
| --- | ---: | --- |
| 随包词表、扫描池、词书、不规则表、协议 | 17 到 18.5 MB | 日志条目数 × 容器布局，静态估算 |
| 图片与字形缓存 | ≤ 10 MB（Qt 文档的默认上限，代码里没有 `setCacheLimit`） | 代码里没有设置 |
| 字体 | Mapped File 16.9 MB，私有 0 | VMMap 行 |
| 其余约 130 MB | 未知 | VMMap 只有构成，没有分配点 |

其余 130 MB 在堆里，最可能的持有人按现有证据排序是：D3D11 与 DXGI 及其两家 GPU 驱动的进程内堆（82% 的线程起于驱动）、Qt 的 QML 类型与对象树、DirectWrite 与 SAPI 的缓存、堆碎片。**这是候选清单，不是结论**：VMMap 的 CSV 只导地址图，Heap 一行都没有导出来，只有块数与最大块；要落到分配点需要 UMDH 双快照，而它要 `gflags /i lens.exe +ust`（管理员）加一次重启，本轮按规则没做。

### 地板怎么定，哪个杠杆值钱

- **预算按地板 +10% 定**：301 到 305 MB 的平台段是实测地板，预算取 332 到 335 MB 量级，并写清口径是 “空闲、没有表面显示”。D1 的 250 MB 与任何一个空闲读数都差 45 MB 以上，不成立。
- **最大的杠杆是玻璃的实时抓屏管线，不是词表**：一张玻璃表面显示时最高读到 616 MB 私有，其中 Private Data 371.7 MB；隐藏后 5 秒仍有 421.5 MB、Private Data 比空闲多 88.1 MB。它另建一个 AMD 上的 D3D11 设备、+36 线程、+148 句柄，而表面自己的 QML 只有 22 到 25 MB。要动的话有三条路，成本从低到高：把抓屏设备并到应用的共享设备上（ADR 0191 的同一思路）；不让 WGC 会话在隐藏后继续跑；给抓屏帧池设上限。三项都改行为，都会碰 ADR 0034、0037、0040、0041 的口径，得先量 15 秒与 60 秒的回落再定。
- **扫描轨自抬的铅笔层**：release 一次首显 +27 MB、+32 线程、+191 句柄。把它改成按需创建、或没有标记时不建全屏交换链，能拿回这一档，代价是首标延迟。
- **宠物窗口**：Debug 下隐藏即释放，−36.9 MB、−80 线程、−1,625 句柄；release 的同一动作本轮没测（管道已通，下一轮一条命令就能补）。宠物是产品特性，不能默认关，但 “读者离开时释放” 这条路已经有机制。
- **词表结构**（flat 容器、前缀共享或把 `g_rank` 的键改成偏移表）最多省 5 到 8 MB，占水位 2%，优先级最低。
- **图片缓存上限显式化**：空闲时几乎无图可缓，收益接近 0，只影响长期使用后的峰值；玻璃纹理单独驻留，10-18 报告 §9.5 已证零收益。

## 没覆盖到的

- **抓屏管线回落到哪**：本轮只取到隐藏后 5 秒。421.5 MB 还在往下走，15 秒与 60 秒的点没有；10-18 报告说驱动还页要 15 秒量级。这是下一轮最便宜的一条，一条命令两分钟。
- **分配点**：堆的 148 到 161 MB 没有 owner。UMDH 与 `gflags` 需要管理员；`config/paths.default.json` 里的 `cdbFallbacks` 在本机是 MISSING，连调试器都没有。这是唯一能把 130 MB 拆开的工具，需要一次明确的管理员同意。
- **十分钟暖机的 13.5 MB**：没有日志事件，没有采样序列（本轮只有 10、60、600 秒三点），只能说是 “涨完就平”。要拆它得在 300 到 700 秒之间按 10 秒取一串 VMMap，或在管理员同意下取分配栈。
- **release 的宠物与主题 A/B**：管道通了之后可以做，但换主题要动读者设置，本轮禁止；dev 树的换主题读数夹着抓屏管线，不可比。
- **VMMap 的导出时序**：dev 轮的 20 份 CSV 里，相邻阶段有 7 对字节完全相同，而 `Get-Process` 同期变动 5 到 15 MB；单进程探针又看到 CLI 返回后文件要 3 秒以上才出现，大进程更久，最后一份还在进程退出时被截断（`Total` 一行四列同值、没有类型行）。所以逐表面的类型差不可用，本报告只用 `Get-Process` 那一列与两张大台阶的 VMMap。
- **长时水位与静态桌面**：30 分钟不是数天；本轮桌面有英文文本，扫描轨抬起过铅笔层并写下判定，完全没有可读英文的桌面会更低，这一档没量。

## 数字来源

`logs/vmmap-attr-release-010s.csv`、`logs/vmmap-attr-release-060s.csv`、`logs/vmmap-attr-release-600s.csv`、`logs/attr/release-idle-10s-2.csv`、`logs/attr/release-idle-60s-2.csv`、`logs/attr/release-idle-600s-2.csv`、`logs/attr/release-rel-*.csv`、`logs/vmmap-idle-20261120.csv`（30 分钟）、`logs/attr/dev-counters.csv`、`logs/attr/dev-*.csv`、`logs/attr/dev-attr-answers.jsonl`；全部 gitignored，数字照抄。条目数与事件取自 `logs/lens.log` 与 `build-ninja-dev/src/app/logs/lens.log`，线程归属取自 `.workbuddy-ai/tools/threadmap.py`，词长统计取自 `data/wordlist.txt` 与 `data/scan-words.txt`。
