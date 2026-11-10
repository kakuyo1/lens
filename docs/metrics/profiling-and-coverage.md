# Profiling 与覆盖率

> 权威：当前测量方法。测试目标、运行方式与样例集见 `TEST.md`；本文只讲测量设施、构建树与读数口径。
> 按日期存放的数字报告在 `docs/metrics/` 下，索引见 `docs/metrics/README.md`。

## Profiling

`src/core/profile.{h,cpp}` 放 `lens_core` 内部的测量点，两个原语：`LENS_PROFILE_SCOPE` 是 RAII
作用域计时器，`LENS_PROFILE_COUNT` 是计数器。二者在 `LENS_PROFILE` 未定义时展开为空，测量代码一字
不改。

开关是 CMake 选项 `LENS_ENABLE_PROFILE`，挂在 `lens_core` 的 **PUBLIC** 上：调用点要不要发样本由
这个宏决定，写入侧与读取侧必须看到同一个值。**开它等于重编 `lens_core`，不是重跑。**

profile 数字要有优化才有意义，而两个开发 preset 都是 Debug，所以用第三个树：

```
cmake --preset ninja-qt6-perf
cmake --build --preset ninja-qt6-perf --target lens_gtest_perf
./build-ninja-perf/test/googletest/lens_gtest_perf.exe
```

（与 `ninja-qt6` 一样需要 MSVC 环境，见 `AGENTS.md` 的 Build 一节。）

三条必须守住：

- **不逐条打日志**。热路径按调用记日志会淹掉日志本身，并盖住要观察的现象。站点只累加，热阶段结束
  后用一次 `report()` 出汇总表；`profile.h` 自身不写任何日志。
- **计时器不免费**。同株关掉插桩作对照，插桩整体抬高约 16%，反推一次 `ScopeTimer` 约 60 ns（两次
  `steady_clock` 读 + 一次注册表查找）。对 `lemmatize` 这种每 token 进入的作用域不可忽略：先用计数器
  量分母，再决定要不要给内层计时。
- **一次结论至少三轮**。本机噪声大：同一次运行的 min 与 max 可差 4 倍，`filterWords` 在五轮里从
  4.36 ms 摆到 7.04 ms，而 `lemmatize` 稳定在 1.85–2.04 ms。报占比要报区间，不报点值。

注册表以字面量的地址为键，所以每次样本不必哈希或拷贝字符串；单线程无锁（`profile.cpp` 的
`ponytail:` 注释写了升级路径）。`report()` 是 “进程启动至今” 的累计，`lens_gtest_perf` 在计时轮次
前调 `reset()`，使报告只覆盖被测量的那一段。

## 覆盖率

两条线，两条完全不同的技术路线，**读数不可比，也不许平均**。哪个数字对应哪条线，看目标的语言。

### C++ 行覆盖

工具是 OpenCppCoverage，winget 装在 `C:\Program Files\OpenCppCoverage\`。它读 Debug 二进制的
PDB、在运行时插桩，所以**没有覆盖率专用树，也没有覆盖率编译标志**：量的是 `build-ninja` 里已有
的那批产物。这一点与上面那棵树相反——profiling 要优化过，覆盖率不要。

```
sh scripts/quality/coverage.sh                                    # 六个无人值守的目标
sh scripts/quality/coverage.sh test/records/coverage lens_gtest_unit   # 或者只点几个目标
```

每个非表面目标各跑一整份，表面套件按用例文件各起一个进程、两个并发（与 QML 那条线共用
`scripts/qa/qtest-cases.sh` 的排序）。CI 再把它拆成三个并行 job：四个小目标一个，表面套件按
`--cases K/N` 分到两个 runner；每个 job 只上传 `.cov`，由 `scripts/quality/coverage-merge.sh`
合并成 `test/records/coverage/coverage.xml`（该目录 gitignored），本地单机跑也由它收尾。
汇总那一行由 `scripts/quality/coverage-summary.py` 算出：Cobertura 里一个被
多个二进制链进去的源文件会出现好几份，根节点的 `line-rate` 把它们加在一起，直接读会偏低。
`lens_gtest_smoke` 要 key 且花钱，不在内；`lens_gtest_integration` 在默认列表里，但 CI 不点它
（runner 没有真鼠标与真剪贴板）。

两条口径，读数字之前先读它：

- **没报出来的文件不是 0%**。只被 `lens.exe` 引用的 `main.cpp` 与 `qt_log.cpp` 不会被静态链接拉进
  测试二进制，声明与宏居多的头文件也没有可执行行——两种都不等于 “没被测过”。
- **`profile.cpp` 的读数偏低是构建配置**。`LENS_ENABLE_PROFILE` 在这棵树上关着，测量点在调用点
  展开成空。

### QML 执行覆盖

Qt 没有 QML 的行覆盖率工具，这不是遗漏而是结构问题：`.qml` 主要在声明对象，而声明一个对象不是一
行 “执行”。会执行的是它里面的 JavaScript——属性绑定、signal handler、函数——唯一报告这些的是
QML profiler：每求值一次就记一条带文件与行号的事件，这些位置的并集就是执行集。

```
npm ci                    # tree-sitter，与 AST 度量共用同一个依赖
sh scripts/quality/qml-coverage.sh
```

分母来自 `scripts/quality/qml-coverage.js` 用 `tree-sitter-qmljs` 建的解析树：模块里每个非字面量的
`ui_property` 与 `ui_binding`，加上每个 `function_declaration`。字面量的绑定被排除在两侧之外
——编译器把它们折进对象的构造，引擎从不求值，profiler 也就永远报不出来；算进分母就是一笔永远
扣不掉的分。结果写 `test/records/qmlcov/`。profiler 启动的是那**两个 QTest 目标**本身，所以这是
测试套件覆盖到多少，不是手工跑一遍应用覆盖到多少。

表面套件的每个用例文件各起一个 profiler，两个并发，最后的并集由同一条命令合并。原因是 trace 的
体积：绑定事件按求值次数记，一个等动画的用例每帧都在求值，整个套件能写出超过 512 MB 的一份文件
——那已经越过 JS 字符串的上限，读不进来。拆开之后单份 trace 更小，耗时也是最长的那个文件而不是
总和；`qml-coverage.js` 本身按块扫描，不把整份 trace 变成字符串。

三条要注意的：

- **它数位置，不数行。**一个绑定只要对象被创建就会求值一次，所以 “两条分支都测过没有” 它答不上
  来。这不是行覆盖率的替代品。
- **无头不渲染，`qsTr()` 标签绑定永不被求值。**未执行的位置里约四分之一是这一条造成的，那不是
  缺口。
- **构建树必须是新的。**QML 编进二进制，改了 `.qml` 没重新链接测试目标就量到旧模块——本轮真踩过
  一次，同一个文件读 19% 与 52%。同理适用于 C++ 那条线。

数字读出来之后怎么写、往哪写，见 `docs/metrics/` 里按日期存的那份报告；补什么、按什么顺序补，
见 `docs/metrics/coverage/coverage-plan-2026-10-03.md`。

## 长时间运行与内存

这一层的设施一直只答一个问题：水线涨没涨。2026-10-05 的两份报告读
`Get-Process` 的私有字节、工作集、句柄与线程，十分钟采二十一个点；2026-10-18 那份
用 `.workbuddy-ai/tools/` 下的本机实验脚本做过八分钟常开与十次开合，加读 VMMap 的分段
和线程起始地址，但那些脚本不进仓库的 `scripts/qa/`。三者都是进程级计数，没有一个能说出
字节是在哪一行分配的；长的一次也只有八分钟，开合最多十次。所以这一层补的是三件事：
把负载变长、把负载变成仓库内可复现的循环、把分配点补上。

口径先定死，否则数字不可比：

- 私有字节是应用自己的账，工作集不是。Windows 会回收可丢弃页，十分钟里工作集从 180 MB
  掉到 130 MB 而私有字节几乎不动，那是系统在收页，不是应用让出来的。
- 一次性台阶与慢速爬升分开读。窗口第一次上屏建 D3D 设备、编译着色器、拉起驱动线程池，
  约 60 到 100 MB、80 线程、1630 句柄；它只发生一次，不是每次使用的增长。真正要盯的是
  台阶之后每轮还剩多少不归零的部分。
- 关闭后等十五秒再读。驱动还页是渐进的，三秒时读数仍然偏高。
- 一次结论至少三轮，报区间不报点值；开发构建的数字只与同构建的数字比，报告首行记 `version`。

`scripts/qa/lens-soak.ps1` 把一份 dev 面剧本重放 N 次，每轮记一行，同时给出这一轮的耗时。
驱动手段、三份剧本与参数见 `docs/qa.md` 的一轮以上的驱动一节。`scripts/qa/lens-measure.ps1`
作独立采样，加 `-Cpu` 多一列 `cpu_cores`，量玻璃持续重拍这类 CPU 成本，单位是核。
两者管道互斥，不要并行。

分配点这一层由 `scripts/qa/heap-diff.ps1` 补：UMDH 对同一个进程抓两份堆快照再做 diff，
按调用栈排增量。它需要 Windows SDK 的 Debugging Tools、一次管理员权限下的
`gflags /i lens.exe +ust`、重启目标进程，以及 `_NT_SYMBOL_PATH` 指向构建树的 PDB；
缺一样就只得到地址，得不到函数名。快照是进程堆的内容，读者的设置与密钥都在里面，
因此只准落在 `logs/` 这个 gitignored 目录，读完即删，绝不提交或外发。
它要验的第一个具体问题是 2026-10-18 报告 9.9 节那笔没定位的慢速爬升：
十次开合加 10.3 MB、38 个句柄，稳定但不归零，当时的候选是 QML 图片缓存的 serial 条目与字体缓存。
另一个明确的观察点是 `src/app/glass/glass_capture.cpp` 里 `ensure()` 的
`CoInitializeEx` 全仓没有配对 `CoUninitialize`，失败后五秒重试会再初始化一次。

2026-11-10 的归因把要分清的两类摆开了：堆泄漏用 UMDH，句柄泄漏要用句柄创建栈，
两者不是一套工具。UMDH 对句柄泄漏会一无所获，因为堆上没有对应的块；
句柄创建栈要靠 `gflags /i lens.exe +htc`、ETW 的 Kernel-Handle 提供者或
Application Verifier，三者都要管理员权限并重启目标进程。
`scripts/qa/handle-types.ps1` 先用对象类型回答是哪一类，再决定用哪套栈；
它只读、不需要提权，`resource-footprint/handle-types-2026-11-10.md` 是它的第一份结果。

线程与句柄单独成一条线。2026-10-18 的主因不是堆，是每个顶层窗口各带起约 80 个驱动线程；
`main.cpp` 的 `releaseHiddenSurfaces()` 已经把线程数从 804 压到 165，
`LENS_KEEP_GRAPHICS=1` 是同一份二进制的对照。复测时按起始模块归因线程，
并盯 hover 走全局 `QThreadPool` 的任务、扫描 track 的 jthread、发音的空闲销毁与同步更新的轮询。

2026-11-10 已经用这套设施复测过一次，数字在
`resource-footprint/soak-cycles-2026-11-10.md`：默认组 25 轮线程不涨，
对照组 12 轮就到 643 线程、首轮一次 460 MB 与 557 线程，说明释放路径没有回归；
但句柄在两种配置下都按每轮 25 到 33 个稳定爬升，而设置连打那组完全不涨，
所以这笔爬升与图形资源是否释放、与设置投影缓存都无关，候选缩到每次窗口显示时
新建、隐藏后不归还的那类句柄上。分配点仍未定位。

数字写进 `docs/metrics/resource-footprint/` 或 `runtime-performance/` 下按日期命名的报告，
索引加进 `docs/metrics/README.md`；原始 CSV、JSONL、trace 与堆快照留在 `test/records/`
和 `logs/`，两者都 gitignored。值得留档的一次运行按 `TEST.md` 第 6 节写单列窄栏 HTML。
