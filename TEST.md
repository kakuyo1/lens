# 测试

> 权威：当前测试入口——框架、目录、目标、运行方式、样例集格式与记录约定。
> Profiling 与覆盖率见 `docs/metrics/profiling-and-coverage.md`；真机驱动与拍照见 `docs/qa.md`。
> `AGENTS.md` 的 Test 一节只留指针；设计总览见 `PRODUCT.md`。

## 1 框架与目录

框架是 GoogleTest，vendor 在 `third_party/googletest`（v1.18.0），照 `third_party/spdlog` 的做法：
`add_subdirectory` 之后只用它导出的 `GTest::gtest` / `GTest::gtest_main`，`BUILD_GMOCK` 与
`INSTALL_GTEST` 关掉（前者没用到，后者会往项目里塞安装规则）。

```
test/
├── eval_corpus.json      # 自检样例集，兼行为规格（§3）
├── records/              # 运行记录，gitignored（§6）
├── googletest/           # 不需要窗口的那一半
│   ├── support.h         # 仓库根 + 词表与屈折表的加载（无 Qt）
│   ├── llm_support.h     # 线上协议加载（含 Qt，只有 LLM 侧目标包含）
│   ├── unit/             # 离线，必绿
│   ├── integration/      # 真机 Windows API，人工执行
│   ├── perf/             # 只测量，不对时间下断言
│   └── smoke/            # 真模型，人工执行
└── qtest/                # 要一个窗口、要跑 QML 的那一半
    ├── main.cpp          # 每个 QTest 目标共用的 runner
    ├── setup.{h,cpp}     # 表面目标要的 Controller / Tray，见 §2
    ├── testutil.js       # 两个目录共用的走树助手
    ├── components/       # 组件级用例（tst_*.qml）
    └── surfaces/         # 表面级用例（tst_*.qml）加 settings.json
```

`googletest/` 的目录名按用例性质分组：`unit` / `integration` / `e2e` / `perf` / `smoke`。当前
`unit`、`integration`、`perf`、`smoke` 有内容；`e2e` 等第一个用例出现时再建目录——git 不跟踪空
目录。QML 与 Qt 侧的测试用 QTest，另立在 `test/qtest/`，与那里平行，不混在一起：它们要一个
`QGuiApplication` 和一个 Qt Quick 场景，gtest 两样都给不了。

## 2 目标与运行

| 目标 | 内容 | 何时跑 |
| --- | --- | --- |
| `lens_gtest_unit` | 样例集 / KnownStore 与 StatsStore 往返 / LLM 纯函数 / 词汇导入导出与日期标签 / app 接缝的纯部分 / 选区手势阈值 / 桌面宠物的状态机、数据解析与配饰 | 每次改动，可进 CI |
| `lens_gtest_integration` | 选区捕获：终端排除 / 钩子与剪贴板的真机往返；玻璃幕布：读屏的紧循环与按节奏对照 / 桌面复制的裁剪与旋转 | 动选区入口或玻璃采集时，人工执行 |
| `lens_gtest_perf` | FilterCore 吞吐 + profiling 报告 | 动内核时 |
| `lens_gtest_glass_probe` | 玻璃幕布的采集上限：适配器与输出归属 / 桌面复制可用性与一帧的代价 / GDI 读屏对照 / 抓屏排除在复制帧里是否生效 | 动玻璃采集或画质档位时，人工执行（CI 没有可复制的桌面会话） |
| `lens_gtest_smoke` | 1 词真模型往返，一次进程最多 3 次真实调用 | 动 LLM 链路时，人工执行 |
| `lens_qtest_components` | `qml/components/` 那八个的接线与交互 | 动组件时，无头，可进 CI |
| `lens_qtest_surfaces` | 表面：Main 的摆放与关闭、托盘菜单、通知卡片、三张面板、行动条 | 动表面时，无头，可进 CI |

运行前先把 `QT_ROOT` 设为 `config/paths.json` 的 `qtRoot`：

```
PATH="$QT_ROOT/bin:$PATH" QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/googletest/lens_gtest_unit.exe

PATH="$QT_ROOT/bin:$PATH" QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/qtest/lens_qtest_components.exe

PATH="$QT_ROOT/bin:$PATH" QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/qtest/lens_qtest_surfaces.exe
```

- 改动后先跑**覆盖它的那条用例**，不跑整套：用例名直接跟在目标后面，如
  `./build-ninja/test/qtest/lens_qtest_surfaces.exe Main::test_x`，约 5 秒；整套 surfaces 249 例，
  全量留给收尾那一次。`scripts/qa/qtest.ps1 -Target <目标> -Case <用例>` 把环境变量与增量构建包在一起。
- 整套 surfaces 可以按文件并行：`bash scripts/qa/qtest-parallel.sh --build build-ninja --shards 2`。
  CI 用同一条命令；每个 `.qml` 一个进程，设置副本按 PID 隔离，成功时每个文件一行 `Totals`，红文件打印完整日志。
- 链接 `lens_llm` 或 `lens_app` 的目标需要 Qt DLL 在 `PATH` 上（即 `unit` / `integration` /
  `smoke` / `glass_probe`）。`lens_gtest_perf` 只链 `lens_core`，保持无 Qt，什么都不需要。
  `lens_gtest_glass_probe` 链 `lens_app`，因为它和应用读同一条桌面来源（`src/app/glass/glass_source.h`）。
- 上表七个目标全部带 `EXCLUDE_FROM_ALL`：默认构建只有主程序 `lens`，测试按名构建，如
  `scripts/build/build.bat --target lens_gtest_unit`。它们在改动 QML 或 `lens_app` / `lens_llm`
  的接口时才会失效，跟着每次改 `src/` 重编没有意义。CI 的构建步骤本来就把要跑的目标逐个列出。
- 选区捕获：`lens_gtest_integration` 的 13 例（该目标另有 2 例读屏测量与 1 例复制裁剪，见下面两条），默认 12 例绿、2 例 skip——一条要人手拖鼠标，一条要 `LENS_GLASS_DUP_TEST=1`。
  `SelectionGrab.DeclinesWhenTheForegroundWindowIsOurs` 取不到前台时也跳过，故偶尔是 11 绿 2 skip，两种都不是红。
  `LENS_HOOK_SMOKE=1` 放开它，`LENS_HOOK_SMOKE_TEXT` 指定要拖选的词（缺省 `ubiquitous`）。它先往剪贴板放一个哨兵串，再等人在**别的窗口**里拖选那个词，然后断言三件事：取到的文本相符、返回状态是
  `Captured`、哨兵串还在剪贴板上。剪贴板存还原本身另有 `ClipboardSnapshot.*` 三例自动覆盖，不依赖这一条——把它交给人工的那段时间里，它正是坏的。那三例直接操真实的剪贴板，而剪贴板是全机共享的：约 35 次运行里见过 3 次失败，都在别的进程刚写过剪贴板之后，按需复现不了，加重试也没挡住。所以那里单次失败先重跑一次再当信号看，成因仍未知，如实记着。提权窗口（任务管理器、管理员控制台）会被 UIPI 挡下注入，表现为超时而不是报错，别拿它试；终端类进程按名字排除，是故意的。
- 那两例真机用例会合成鼠标事件，指针会被移动并复位。合成拖拽的落点是一个顶层小窗口，它由一个**子进程**持有：钩子会挡掉落在本进程窗口上的按下（那条规则是 “不把自己的表面当选区”），所以落点窗口不能是本测试自己的。子进程就是测试可执行文件以 `--lens-probe-window` 再跑一遍，靠 job object 在用例结束时收掉。
- 采集上限：`lens_gtest_glass_probe`，4 例，只在交互桌面会话上跑得动——远程会话与 CI 都没有可复制的输出。
  它只打印数字、不对时间下断言，读法写在文件头；`LENS_PROBE_EXCLUSION=1` 才跑那条会往屏上放一个品红方块的用例。
  本机实测数字与结论在 `docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md`。
- 读屏的节奏与面积：`GlassReadPace.PacedAgainstTight` 与 `GlassReadPace.AreaSetsThePrice`，在 `lens_gtest_integration`
  里，读真实屏幕但不建窗口，共约 90 秒，只打印不判时间。它们回答的是 “探针量到的读屏价能不能代表应用付的价”
  与 “读价随节奏还是随面积”（`src/app/glass/glass_desktop.h` 的抽取理由），数字同上那份报告 3.5 与 3.6。
- 复制裁剪与旋转：`GlassDuplication.CropFollowsTheRotatedScreen`，也在 `lens_gtest_integration` 里，默认跳过；
  `LENS_GLASS_DUP_TEST=1` 才跑。它在每块屏上显示一个四色窗口约一秒，验证复制帧的裁剪与旋转，并与 GDI 同区域逐点比较；
  本机结果见报告 3.9。它需要交互桌面会话，CI 不跑。
- 不接 ctest：`gtest_discover_tests` 会在构建期执行测试程序，等于要求构建环境也把 Qt DLL 摆在
  `PATH` 上，不值得这层耦合。QTest 侧同理，同样不接。
- QTest 的用例是 `.qml`，从源码树直接读，不进任何模块的 `QML_FILES`，所以 qmllint 不看它们，那
  条棘轮不受影响。它们要 `import Lens` 才跑得起来，因此目标链 `lens_app` 与 `lens_appplugin`：
  静态插件只有它的 init 对象被链进来才导入，少了这一条，组件会以 “不是类型” 而不是以断言失败
  的形式消失。
- 用例文件的根是一个 `Item`，`TestCase` 是它的孩子而不是根。`TestCase` 自带 `visible: false`，
  挂在它下面的东西于是收不到鼠标事件——而鼠标事件正是这个目标替掉人眼的那一半。
- 图标资源挂在 `lens_app` 上，不在可执行文件里：`:/icons/` 只在带着它的二进制里解析，而读图标的是表面。
  挂错地方时两个 QTest 目标每个用例刷一条 `QML Image: Cannot open`，一共三百多条。
- 剩下的是 Qt 自己发的两类：offscreen 平台不能抬窗口，而表面显示时 `raise()` 是必需的；Qt 6 不再随包带字体，
  `setup.cpp` 自己按文件载入。它们都走不带类别的 `qWarning()`，按类别过滤会连带盖掉其余无类别警告，所以留着，
  一次运行合计十条左右。
- 两个目标的差别只有一处：`lens_qtest_surfaces` 编译 `setup.cpp` 时定义 `LENS_QTEST_SINGLETONS`，
  组件目标不定义。`Controller` / `Tray` 的工厂在没人 `provide()` 时断言、Debug 下直接崩进程，而
  引擎自己造不出来（构造函数要 store、client 与 hook），所以 setup 照 `main()` 的做法造好再交出
  去——只是**不装鼠标钩子、不显示托盘图标**。组件用例碰不到这两样，也就不必背一份配置。
- 表面用例跑的是签入的 `surfaces/settings.json`，不是 `settings.local.json`，两个理由：断言要在每
  台机器上看到同一组值，而设置面板的 setter 会落盘——指向仓库里那份，一条切换主题的用例就会改掉一
  个被跟踪的文件。setup 把夹具拷进临时目录再加载，进程退出时删掉。
- 夹具里 `selectionCapture` 写的是字符串 `"false"`，不是 JSON 布尔。两种写法现在都读得出来（设置
  分区读时两种编码都认），所以手写成 `false` 不再被当作缺省值 `"true"` 静默忽略；但**写**下去时保留
  该键已有的编码，手写的字符串键不会变成真布尔，只有从未存在过的新键才按真类型出生（`docs/adr/0024`）。
- 冒烟：`LENS_SMOKE_WORD=ubiquitous ./build-ninja/test/googletest/lens_gtest_smoke.exe`。词取自
  `LENS_SMOKE_WORD`，缺省 `ubiquitous`。`settings.local.json` 缺 URL / MODEL / API-KEY 时报 skip
  而不是失败——要 key、要花钱、结论靠人看，所以它不进 CI。密钥读进内存后不打印、不进日志、不进
  失败消息。
- 冒烟会自己数钱：每发一次真实请求计一次，累计到上限（`LENS_SMOKE_MAX_CALLS`，缺省 3）之后改为
  skip，理由写在 skip 消息里。上限按**一次进程**算，不是按用例算——钱是按进程花出去的。放着不管的
  二进制因此不会一直花，而 skip 也不是失败，跑完仍是一份绿报告。
- `LENS_LOG_LEVEL=trace` 提高日志级别。

## 3 样例集

`test/eval_corpus.json` 是**行为规格**。每段一项：`text`（必填）、`expect`（必填，surface 序列）、
`expectKind`（必填，`"Word"` / `"Entity"` / `"Sentence"` / `"Translation"`；Entity 与 Translation 的 `expect` 必须为空）、`expectLemmas`（选填）、
`minFreqRank`（选填，缺省 0）、`known`（选填）、`note`（选填）。`minFreqRank` 与 `known` 现在只给
候选**标状态**，不删候选，所以带这两项的段落，`expect` 与不带时相同——这正是
它们要钉住的那条。

改 FilterCore 的行为先改样例集，`lens_gtest_unit` 红了再动 `src/`。样例集撞出误判就在那里加一段，
不要先把断言放宽。

`scripts/data/check-eval-corpus.py` 独立校验样例集（不走实现）：Word / Sentence
段落中每个 token 要么进 `expect`，要么有一条具名规则把它排除；`expectKind` 另按选区核对——含汉字的一律为
Translation（语言先于形状判定），其余按形状：单 token 在词表内为 Word、不在为 Entity（命名实体），多 token
名称短语（Title Case 或全大写缩写）为 Entity、其余为 Sentence。改完样例集跑一次，它是期望值之外的那道独立核对。

## 4 Profiling 与覆盖率

测量设施、构建树、读数口径与两条覆盖率线的差异见 `docs/metrics/profiling-and-coverage.md`。

## 5 UI 表面

**顺序**：先开发面，再离屏 QTest，最后才是真实鼠标与截图。能用 `surface show|hide`、`surface route`、
`setting set`、`state`、`wait` 回答的状态、触发与可见性，一律先走开发面（`scripts/qa/lens-dev.ps1`，构建开关
`LENS_ENABLE_DEV_SURFACE=ON`，见 `docs/qa.md`）。只有像素问题（字形、阴影、对齐、图标）或必须
经过真实钩子与命中测试的输入，才用 `ui-input.ps1` 和 `ui-capture.ps1`；用之前先说明是哪一种，并在
运行记录里记下。开发面不移动指针、不抓屏，也不该为了 “看一眼” 而动用真机驱动。

真机驱动、拍照、离屏快照与像素判读的细节见 `docs/qa.md`。

## 6 记录

`test/records/` 存运行快照，**gitignored**——那是本机的，不进仓库。一次值得留档的运行写一个 HTML，
文件名是日期加主题（如 `2026-10-02-full-run.html`）。

页面形态：白底、系统字体、单列窄栏、无装饰。表格只有横向细分隔线，数字用等宽字体，段落不缩进。CSS
内联，不引任何外部资源，离线可看，打印可读。

转写规则：数字照抄原始输出，不重算、不四舍五入、不补齐；一次结论至少三轮（`docs/metrics/profiling-and-coverage.md`）；对不上就重跑，
不要修数字。

记录目前是手工从运行输出转写的。等它变成例行动作再写生成器——在那之前，生成器只是多一个会腐烂的
中间层。
