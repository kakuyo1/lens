# 开发面实施方案

> 决定见 `docs/adr/0068-developer-surface-drives-the-ui-without-the-mouse.md`。这份文档只讲怎么建、
> 按什么顺序建、每步怎么验。目标读者是下一个要复测性能的人。

## 1、目标与验收

| 项 | 现在 | 建成后 |
| --- | --- | --- |
| 打开一个指定表面 | 截屏定位 + 注入鼠标，抢鼠标 | `surface show settings` |
| 知道它开没开 | `EnumWindows` 找 >300×300 的窗口 | `state` 的 `surfaces[].visible` |
| 知道切换完成了 | `sleep 3`，读到 283.9 而不是 260 | `state` + 首帧事件 |
| 知道幕布发布了几次 | 从 CPU 反推 | `state` 的 `surfaces[].publishes` |
| 知道生效主题 | 读 `settings.json`（被它坑过一次） | `state` 的 `theme` |
| 触发选区动作条 | 拖选屏幕上某处的文字 | `selection 160,175,620,70 "the quiet library"` |
| 换主题做 A/B | 改 `settings.json` 再恢复 | `setting set theme light` |

验收标准：**一条命令复现 “开设置窗 → 关闭 → 等释放 → 开动作条”，全程不移动指针，输出一份带 `state`
快照的 CSV**。这条今天要靠 `exp_two.ps1` 加三次失败重试才做得到。

## 2、现状：已有的入口与它的边界

`CMakeLists.txt:74` 的 `LENS_ENABLE_QML_DEBUG` → `QT_QML_DEBUG` → `main.cpp:302` 读
`LENS_QML_PROFILE_SCENARIO` → `Main.profileScenario(step)` → `ProfileScenario.qml` 的 `run(step)`，
五个剧本（`startup` / `tray` / `navigation` / `settings` / `bubble`）各是一串 `host.showXxx()`。

能复用的是**形态**，不能复用是**实现**，原因三条：

1. 只在 QML 调试构建里。项目自己在 `CMakeLists.txt:74` 写明它 “changes the executable's instrumentation”，
   而量内存必须用发布优化配置。
2. 剧本写死，`1500 + 900 × i` 的固定定时器，自己也不知道某一步何时完成。
3. 没有回读，也没有单步命令。

`ProfileScenario.qml` 的注释里那条规矩要保留：**这类代码不进 `Main.qml`**，因为它有大小上限
（`docs/QML.md` §1）。新文件叫 `DevSurface.qml`，同样只被 `Main.qml` 实例化一次。

## 3、构建开关与文件布局

```cmake
# CMakeLists.txt，接在 LENS_ENABLE_QML_DEBUG 之后
# Drives the surfaces from a command instead of from the mouse, for measuring what the reader
# cannot see: which window is up, when it finished its first frame, how often its sheet was
# published. Off by default and never shipped; see docs/adr/0068.
option(LENS_ENABLE_DEV_SURFACE "Compile the developer command surface in" OFF)
```

```cmake
# src/app/CMakeLists.txt
if(LENS_ENABLE_DEV_SURFACE)
  target_sources(lens PRIVATE dev_surface.cpp dev_surface.h)
  target_compile_definitions(lens PRIVATE LENS_DEV_SURFACE)
endif()
```

| 文件 | 作用 | 是否随开关 |
| --- | --- | --- |
| `src/app/dev_surface.h/.cpp` | 命令表、两条通道、`state` 的组装 | 是 |
| `src/app/qml/DevSurface.qml` | 命令 → `Main.qml` 的函数 | 是（`Main.qml` 里用 `#if` 无法控制 QML，改为 C++ 侧按开关决定是否实例化） |
| `src/app/glass_backdrop.cpp` | 每张幕布的 `publishes_` 计数 | 否（见 §5 的说明） |
| `src/app/capture_duty.cpp` | `beginSelection()` 多一个可选文本参数 | 否（参数默认空，行为不变） |
| `scripts/qa/lens-dev.ps1` | 命令客户端 | 否，随仓库 |
| `scripts/qa/lens-measure.ps1` | 采样 + 结算判定 + CSV | 否，随仓库 |

QML 侧怎么按开关决定是否实例化：`Main.qml` 里那个 `DevSurface { }` 块无法用 `#if` 包（QML 没有预处理器），
所以做法是 **C++ 侧注入**：`main.cpp` 在开关打开时把 `DevSurface` 注册成 QML 类型并在 `Main.qml` 里留一个
`property var devSurface`，由 C++ 赋值；关闭时该属性为 `null`，QML 里用 `devSurface ? devSurface.run(cmd) : ""`
兜住。这样出厂构建里 QML 侧只有三行判空。

## 4、命令表

十个动词，超出这个数就走 `lens_qtest_surfaces`，不要让它长成第二套 UI API。

| 命令 | 参数 | 调的真实入口 | 说明 |
| --- | --- | --- | --- |
| `surface show <name>` | `settings` `tray` `stats` `words` `yearWords` `cost` `bubble` `selectionBar` `notice` `update` `removal` `reviewSign` | `Main.qml` 的 `showSettings()` / `showStats()` / … | 名字取 `GlassBackdrop::surface()` 的索引名，两处必须一致 |
| `surface hide <name>` | 同上 | 该表面的 `visible = false` | 与用户点外部等价 |
| `surface hide-all` | — | `hidePanels(null)` + `bubble.dismiss()` + `bar.dismiss()` | 等价于一次外部点击 |
| `selection <x>,<y>,<w>,<h> "<text>"` | 物理像素矩形与文本 | `CaptureDuty::beginSelection(anchor, text)` | 见 §6 |
| `setting set <key> <value>` | `theme` `liquidBlur` `frostedBlur` `liquidRefraction` `animationsEnabled` … | `AppController::setTheme()` / `setLiquidBlur()` / … | 白名单，不是通用写入 |
| `state` | — | 见 §5 | 返回一行 JSON |
| `wait <condition>` | `visible:<name>` `hidden:<name>` `painted:<name>` | 轮询 `state` | 可选，harness 也能自己等 |
| `quit` | — | `QCoreApplication::quit()` | 让一条脚本能自己收尾 |
| `version` | — | 编译期的开关状态与构建类型 | 报告里记一行，防止跨构建比较 |
| `help` | — | 命令表本身 | — |

`version` 值得单列：2026-10-18 那份报告因为二进制 MD5 不同而不得不单独分节，就是因为没有这个字段。

## 5、`state` 的字段

一行 JSON，键名固定，缺项给 `null` 而不是省略 —— harness 才能不判空。

```json
{
  "build": {"devSurface": true, "config": "RelWithDebInfo", "qmlDebug": false},
  "theme": "liquid",
  "windows": {"total": 12, "visible": 2},
  "surfaces": [
    {"name": "settings", "visible": true, "painted": 41, "serial": 812,
     "publishes": 812, "live": true, "sheet": "1075x1231"}
  ]
}
```

| 字段 | 来源 | 为什么需要 |
| --- | --- | --- |
| `theme` | `Controller.settings.theme` | 今天靠读 `settings.json`，被它的过期值坑过一整轮 |
| `windows.total` | `root->findChildren<QQuickWindow*>()` | 验证 “12 个窗口” 这个前提 |
| `surfaces[].visible` | 窗口的 `isVisible()` | 替代 `EnumWindows` 找矩形 |
| `surfaces[].painted` | `QQuickWindow::frameSwapped` 计数 | 知道 “第一帧真的画出来了”，内存台阶才能对齐到帧 |
| `surfaces[].serial` | `GlassSheet::serial()` | 幕布版本 |
| `surfaces[].publishes` | `GlassSheet` 的计数器 | 替代 “从 CPU 反推发布次数” |
| `surfaces[].live` | `GlassSheet::live()` | 判断还在不在轮转 |
| `surfaces[].sheet` | `GlassSheet::sheet()` 的尺寸 | 幕布区域，换算成读取面积 |

**计数器不设开关。**`publishes_` 是一次整数自增，放在出厂二进制里也无所谓，而把它设成 gated 会让
“被测二进制” 与 “出厂二进制” 在热路径上分叉 —— 那正是这份方案要避免的事。gated 的只有**报告**：
`state` 的组装与 `frameSwapped` 的连接。

## 6、选区注入：从剪贴板改为可带文本

今天的 `exp_bar.ps1` 靠在屏幕 `(160, 175)` 拖选一段文字，前提是那里正好有可选的文本 —— 换个窗口布局就失效。
管线是：`MouseSelectionHook::selectionReleased(QPoint)` → `CaptureDuty::onSelectionReleased()` →
`beginSelection(anchor)` → 抓剪贴板 → 下游。

改动很小：`beginSelection(QPoint anchor, std::optional<QString> text = std::nullopt)`，为空时照旧抓剪贴板；
开发面传入文本，跳过抓取。**真实路径一行不变**，只是多一个默认参数。

`selection` 命令给的矩形用于摆放动作条（`Main.qml` 的 `onSelectionBarRequested` 需要一个锚点），
文本用于让下游有内容可处理。

## 7、两条通道

```
lens.exe --dev-pipe=lens-dev-1                    # 建管道，等人下命令
lens.exe --dev-script=scripts/qa/scenarios/two.json  # 起来就跑，跑完退出
```

- 共用 `DevSurface::execute(QString line) -> QString json`，一行一条命令，一行一条结果。
- 管道名必须显式给。没给就不建 —— 出厂构建里连 `QLocalServer` 的构造都不存在（在 `#if` 里）。
- 管道只接受本机、本用户的连接：`QLocalServer::setSocketOptions(UserAccessOption)`，并校验
  `peerPid` 的会话。开发面不是服务，宁可拒绝也不要放宽。
- 脚本格式：纯文本，一行一条命令，`#` 开头是注释。不引入 JSON 剧本格式 —— 那会让 “先写剧本再跑” 变成
  一门小语言。

## 8、harness

两个脚本，都在 `scripts/qa/`，都随仓库（这是这份方案对 “把脚本放 scripts 下” 那个想法的回应：
**机制进产品，harness 进仓库，数据进 gitignore 目录**）。

### `lens-dev.ps1` —— 客户端

```powershell
# 对着一个运行中的实例下命令，打印返回的 JSON
& .\scripts\qa\lens-dev.ps1 -Pipe lens-dev-1 -Command "surface show settings"
& .\scripts\qa\lens-dev.ps1 -Pipe lens-dev-1 -Script .\scripts\qa\scenarios\two.txt

# 自己起一个、跑完关掉（等同今天的 exp_*.ps1 的外壳）
& .\scripts\qa\lens-dev.ps1 -Launch -Script .\scripts\qa\scenarios\two.txt
```

### `lens-measure.ps1` —— 采样与结算

```powershell
# 采样到 CSV，字段与今天的一致（多一列 state 快照）
& .\scripts\qa\lens-measure.ps1 -Pid 1234 -Seconds 60 -Interval 10 -Out .\logs\two.csv

# 结算：私有字节在 N 次采样内波动 < 2 MB 才返回
& .\scripts\qa\lens-measure.ps1 -Pid 1234 -Settle -Samples 3 -Tolerance 2
```

**结算判定留在 harness**，产品只回答状态。理由是它是测量语义：多稳算稳、采几次，取决于要回答的问题，
不该编进应用。

## 9、迁移：今天哪些东西被替代

| 今天的脚本 | 建成后 | 收益 |
| --- | --- | --- |
| `exp_two.ps1`（开设置窗 → 关 → 开动作条） | 一份三行脚本 + `lens-measure.ps1` | 不再枚举窗口、不再抢鼠标 |
| `exp_soak.ps1 -Mode cycles` | 脚本里的 `repeat 10 { show; sleep; hide; settle }` | 去掉固定 `sleep`，用 `settle` |
| `exp_bar.ps1`（拖选开动作条） | `selection 160,175,620,70 "text"` | 不再依赖屏幕上恰好有可选的文字 |
| `ab_theme.ps1`（改 `settings.json` 再恢复） | `setting set theme light` | **根除主题残留这个坑** |
| `probe_windows.ps1` / `trayshot.ps1` / `find_new_icon.py` | 删除 | 只为定位而生 |
| `exp_s4_change.ps1`（滚动验证指纹） | **保留** | 它测的是 “桌面变了”，需要真实输入，属输入层 |
| `threadmap.py` | 保留 | OS 级线程归因，与界面无关 |
| `qwindow-probe` | 保留 | 它是独立探针，不是应用的一部分 |

七个里五个消失。

## 10、分期

### 阶段 A：命令表与状态层（半天）

**改动**：`CMakeLists.txt`（开关）、`src/app/CMakeLists.txt`（源文件与宏）、`dev_surface.h/.cpp`、
`qml/DevSurface.qml`、`Main.qml`（一个 `property var devSurface` 与三个判空调用点）、
`glass_backdrop.h/.cpp`（`publishes_` 与 `serial()`/`live()`/`sheet()` 的只读访问）、
`scripts/qa/lens-dev.ps1`。

**验证**：`-DLENS_ENABLE_DEV_SURFACE=ON` 构建，跑
`surface show settings` → `state`（`visible:true`，`painted` 递增）→ `surface hide settings` → `state`。
再用它复现 2026-10-18 的 `exp_two.ps1`，两组数字应当吻合（关闭 15 秒后 ~260 MB / 165 线程）。

**退出标准**：一条命令跑完 “开 → 关 → 开另一个”，CSV 与 `state` 都落到磁盘，且**全程指针没动过**。

### 阶段 B：管道与选区注入（半天）

**改动**：`dev_surface.cpp`（`QLocalServer`、连接校验）、`capture_duty.cpp/h`（`beginSelection` 的可选文本）、
`scripts/qa/lens-measure.ps1`、两份场景脚本。

**验证**：同一个进程里连续 `show/hide` 五个不同表面，`state` 的 `windows.total` 不变、`visible` 只在 0 与 1
之间切换；`selection` 注入后动作条出现且文本正确。

**退出标准**：`state` 在第五个表面之后仍能给出每个表面的 `publishes`，且不需要杀进程。

### 阶段 C：防腐（半天）

**改动**：`.github/workflows`（新增一个 `LENS_ENABLE_DEV_SURFACE=ON` 的构建 job 与冒烟脚本）、
`TEST.md` §5（加一段分工说明）、`.githooks/pre-commit`（`check_probes` 的说明与例外）、
`docs/QML.md`（若涉及 `Main.qml` 的判空约定）。

**验证**：CI 里那个 job 绿；把开关关掉的构建仍能通过全部既有测试；`grep -rn "devSurface" src/app/qml`
只有预期的那三处判空。

**退出标准**：两个构建配置都在 CI 里跑；`TEST.md` 说清了 “状态与资源问题归开发面，像素问题归 `ui-capture`”。

## 11、测试与 CI

| 层 | 覆盖 | 归属 |
| --- | --- | --- |
| 命令表解析 | 合法/非法命令、缺参数、未知表面名 → 错误 JSON 而不是崩溃 | `lens_gtest_unit`（`dev_surface` 的解析部分要能不依赖窗口单测，所以解析与执行分成两个类） |
| 状态组装 | 给定窗口集合，`state` 的 JSON 结构 | 同上 |
| 端到端 | 起应用 → 三行脚本 → 断言 `state` | CI 的 dev job，PowerShell |
| 出厂构建 | 开关关闭时编得过、跑得起来 | 既有 CI 已覆盖，加一条 `grep` 确认没有 `dev_surface` 符号 |

## 12、风险与对策

| 风险 | 表现 | 对策 |
| --- | --- | --- |
| 开发面与真实路径分叉 | 命令走状态层，绕过了钩子与命中测试，量到的不是用户走的那条 | 每个表面保留一条人工/`ui-input` 的端到端用例；`TEST.md` 写明分工 |
| gated 代码腐烂 | 半年后 `-DLENS_ENABLE_DEV_SURFACE=ON` 编不过 | 阶段 C 的 CI job 是硬要求，不是可选项 |
| 报告之间不可比 | 开发面构建与出厂构建的内存略有差异 | `version` 命令输出开关状态，报告首行记录；跨配置不并排比较 |
| 接口膨胀 | 每个新表面都要加一条命令，最后变成第二套 UI API | 动词上限十个；新增表面优先复用 `surface show <name>` 的映射表，不新增动词 |
| 安全 | 开发构建被误发出去，管道可被本机任意进程驱动 | 编译开关默认 OFF、管道名必须显式传、`UserAccessOption` + 会话校验；打包脚本不传该选项 |
| 命名撞钩子 | `check_probes` 拦下含 `PROBE` / `console.log` 的 `src/` 文件 | 命名用 `dev_surface` / `LENS_DEV_SURFACE`；阶段 C 在钩子注释里说明为什么开发面不算探针 |
| 开发构建改写用户配置 | 启动时拉取模型目录，`KnownStore` 随之保存 8 次，`%APPDATA%\Lens\settings.json` 被改写（2026-10-18 两次运行） | 未决，见 §14：隔离 profile，或由用户接受。决定之前不在真实 profile 上运行开发脚本 |

## 13、不做的事

- **不做系统输入层**：不移动指针、不注入 `mouse_event`。不确定性来自窗口位置、DPI、任务栏与溢出面板，
  工具解决不了 —— 今天在托盘图标上折三次就是这个结论。
- **不做 HTTP / RPC**：本机测量工具，命名管道够用。多一层协议就多一处要维护的东西。
- **不用运行时开关代替编译开关**：编进去就会泄漏，只是时间问题。
- **不替代 `lens_qtest_surfaces` 与 `ui-capture.ps1`**：offscreen 不建 D3D 设备，量不到驱动线程；
  像素与观感只能看。三者分工不同。
- **不在 `src/` 里留临时探针**：`TEST.md` §5 的规矩不变，开发面是常设接口，不是探针。

## 14、实施记录

### 阶段 A：代码完成，运行时部分验证

提交：`6a66b15`、`e2dc0f2`、`618017a`、`e3f4052`，分支 `feat/dev-surface`，尚未推送。

与前文不一致的地方，都是实施中定下的，列在这里，前文不改写：

1. 不建 `DevSurface.qml`。C++ 直接调用 `Main.qml` 已有的 `showX()`，窗口按 `objectName` 查找。原因：`Main.qml` 已有 32671 字节，逼近 `docs/QML.md` §1 的 32768 字符上限，只剩约 97 字节。
2. `Main.qml` 没有 `devSurface` 属性与判空。各表面的根 `Window` 直接带 `objectName`，生产构建也带，无害。`Bubble` 的实例标在 `Main.qml`，因为 `Bubble.qml` 同时也是设置页预览，在它里面标会重名。约定：`src/core/dev_command.h` 的 `kSurfaceNames` 与各表面根窗口的 `objectName` 必须逐字一致，改名时两处同改。
3. 解析放在 `lens_core`（无 Qt）：`src/core/dev_command.*`。执行器在 `src/app/dev_surface.*`，只随 `LENS_ENABLE_DEV_SURFACE` 编译。
4. 增加 `--dev-out=<file>`。GUI 程序没有标准输出，应答只能写进文件。
5. `publishes` 单独计数（`GlassSheet::publishes_`）。`serial_` 在 `clear()` 时也会递增，不能代替它。
6. 阶段 A 的 `surface show` 只覆盖 `settings`、`tray`、`stats`、`words`、`yearWords`、`cost`。其余六个表面返回 `ok: false`，阶段 B 决定是否补上。
7. §10 阶段 A 退出标准里的 “CSV” 属于阶段 B 的 `lens-measure.ps1`，阶段 A 只输出 JSON 行。
8. 阶段 C 的验证里 “`grep -rn devSurface src/app/qml` 只有预期的三处判空” 已不适用：本实现没有 `devSurface` 判空，这条 grep 应为 0 处。改为检查各表面的 `objectName` 与 `kSurfaceNames` 一致。
9. §9 的 `repeat 10 { show; sleep; hide; settle }` 与 §7 的 “一行一条命令” 脚本格式冲突。阶段 B 有两条路：由 `lens-measure.ps1` 循环发送单条命令，或为解析器增加控制行。后者会让脚本变成小语言，§7 明令不做，建议走前者。

实测（2026-10-18，`scripts/qa/scenarios/two.txt`，Debug 构建，第 4 次运行）：

- 十三条命令全部 `ok: true`。`show` 之后 `painted` 递增，`hide` 之后 `visible` 为 false，`state.theme` 读出生效主题。
- 幕布计数可读：`wait hidden:tray` 之后，设置与托盘各为 `publishes: 1`。
- 未验证：“全程指针没动过”。没有测量。

阶段 B 要承接的缺口：

- 首次显示时，设置窗与托盘的 `opacity` 在 `captureTimer` 触发前为 0，实测约 1.73 秒与 0.66 秒，原因未定，记在 `TODO.md`。
- `hide-all` 对 `bubble`、`selectionBar` 调用 `dismiss` 的结果被忽略，失败时仍答 `ok: true`。
- `hide-all` 之后托盘与设置还在淡出，脚本必须先 `wait hidden:` 再读状态，`two.txt` 已经这样写。

### 阶段 B 开始前的未决问题

- **开发构建会改写用户配置。**启动时拉取模型目录（`GET https://openrouter.ai/api/v1/models`），拉取完成后 `KnownStore` 保存了 8 次。2026-10-18 的两次运行都改写了 `%APPDATA%\Lens\settings.json`。这与 `AGENTS.md` 的 API key 规定相冲突，也违反 `e2e-side-effect-safety` 的隔离要求。待定两条路：用私有 profile 隔离（先要验证 Lens 是否尊重 `APPDATA` 重定向，并且不能把 API key 复制进临时目录），或者由用户接受这次写入。决定之前，不在真实 profile 上运行开发脚本。

2026-10-18 已决：用户同意 `--settings-file=<path>`。Lens 的设置路径来自 `QStandardPaths::AppDataLocation`，代码里没有覆盖入口，所以不采用 `APPDATA` 重定向（这是对 Qt 的判断，未在 Windows 上核实）。开发构建只在 `LENS_ENABLE_DEV_SURFACE` 下接受该参数；指定后不调用 `settingsPath()`，不复制旧文件；路径为空则拒绝启动。

### 阶段 B 实施记录

与前文不一致的地方，都是实施中定下的，列在这里，前文不改写：

1. `selection` 的锚点取矩形右下角 `(x+w, y+h)`，与左到右拖选的释放点一致。§6 未规定。
2. 开发路径调用 `beginSelection`，不经过 `onSelectionReleased`，所以不受 “选区捕获” 开关与 “预算暂停” 约束。用户关了选区捕获，动作条仍会出现。
3. `beginSelection` 由私有改为公开，多一个可选的 `text`。鼠标路径不传，行为不变。
4. `setting set` 白名单五项：`theme`（light、dark、frosted、liquid）、`liquidBlur`、`frostedBlur`、`liquidRefraction`（整数）、`animationsEnabled`（on、off）。§4 的 “…” 之后的项未加。
5. 动词恰好十个。`quit` 写完应答后结束脚本；带管道时，脚本跑完不退出，直到收到 `quit`。
6. `selection` 的文本必须双引号、非空，内部不能有引号；w、h 须大于 0，x、y 可为负。
7. `--dev-pipe=<name>`：只有显式给名字才创建，空名拒绝启动。`QLocalServer::UserAccessOption`；对端用 `GetNamedPipeClientSessionId` 与当前会话比较，失败即拒绝；一次只接一个客户端；读缓冲 64 KiB；单行 4 KiB，超长即切断；排队 256 行。§7 的 `peerPid` 在 Qt 6.9 的 `QLocalSocket` 上不存在，已改为会话校验。
8. 修复：`startDevSurface` 在 `app.exec()` 之前调用，而 `QCoreApplication::exit` 在那时无效，原先 “脚本读不到” 不会退出。现在用 `singleShot(0)` 延后到事件循环里。

验证（2026-10-18，Debug 构建，私有设置文件）：

- 单测：`DevCommand*` 11 个、`DevLineFramer*` 6 个，全部通过。framer 的红灯是编译失败（头文件不存在），已观察到。
- 启动拒绝：`--dev-script` 指向不存在的文件、`--dev-pipe=` 空名，两次退出码都是 1。
- 管道会话：合法客户端被接受，说明 `socketDescriptor()` 返回的是有效的管道句柄；第二个客户端被拒；被拒后原客户端仍可用；`wait` 按序应答；超长行在 4096 字节处切断；`quit` 后退出码 0。
- 五表面（`scripts/qa/scenarios/five-surfaces.txt`）：同一进程内，设置、托盘、统计、词、年度词依次显示、隐藏。`windows.total` 始终为 18，各表面 `visible` 只在 0 与 1 之间切换，退出码 0。`yearWords` 的 `publishes` 为 0，玻璃幕布本次运行没有发布（`sheet` 为 null），不计为发布过。
- `setting set theme dark` 之后 `state.theme` 为 `dark`；两次进程之间私有设置文件保持设置。用户 profile 的 `settings.json` 修改时间早于所有运行，未被改动。

未验证：

- 跨会话拒绝路径：这台机器上制造不了不同会话的客户端。
- `UserAccessOption` 在 Qt 6.9 Windows 上的实际 ACL：只看了头文件，没有实现源码。
- 两个实例使用同一管道名时的拒绝：未运行。
- 出厂构建（开关 OFF）尚未编译，留到阶段 C。
- `scripts/qa/lens-measure.ps1` 的固定时长模式由用户在 PowerShell 里运行过一次（30 秒，6 行，`state` 每行都是有效应答，用户 profile 未被改动）。结算模式尚未运行。worktree 不能运行 `.ps1`，运行由用户完成。列名按交接里记过的指标定（私有字节、工作集、线程、句柄、`state`），没有与旧的 `exp_*.ps1` 核对，那些脚本不在仓库里。

已知残留风险：

- 同用户的客户端持续发送被拒的行，会在日志里逐行写 INFO，没有限速。
- 排队满时的拒绝应答可能先于更早排队的应答到达，客户端需按 `cmd` 字段对应。
