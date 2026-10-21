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
sh scripts/quality/coverage.sh                                    # 五个无人值守的目标
sh scripts/quality/coverage.sh test/records/coverage lens_gtest_unit   # 或者只点几个目标
```

每个目标一份，最后合并成 `test/records/coverage/coverage.xml`；该目录 gitignored。汇总那一行由
`scripts/quality/coverage-summary.py` 算出：Cobertura 里一个被多个二进制链进去的源文件会出现好几份，
根节点的 `line-rate` 把它们加在一起，直接读会偏低。`lens_gtest_smoke` 要 key 且花钱，不在内；
`lens_gtest_integration` 在默认列表里，但 CI 不点它（runner 没有真鼠标与真剪贴板）。

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

三条要注意的：

- **它数位置，不数行。**一个绑定只要对象被创建就会求值一次，所以 “两条分支都测过没有” 它答不上
  来。这不是行覆盖率的替代品。
- **无头不渲染，`qsTr()` 标签绑定永不被求值。**未执行的位置里约四分之一是这一条造成的，那不是
  缺口。
- **构建树必须是新的。**QML 编进二进制，改了 `.qml` 没重新链接测试目标就量到旧模块——本轮真踩过
  一次，同一个文件读 19% 与 52%。同理适用于 C++ 那条线。

数字读出来之后怎么写、往哪写，见 `docs/metrics/` 里按日期存的那份报告；补什么、按什么顺序补，
见 `docs/metrics/coverage/coverage-plan-2026-10-03.md`。
