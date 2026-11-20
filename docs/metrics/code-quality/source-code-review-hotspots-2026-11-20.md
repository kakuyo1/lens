# 源码审阅热点报告

## 1、报告信息

| 项目 | 内容 |
| --- | --- |
| 分析范围 | `src/` 下的 C++、C++ 头文件和 QML 文件 |
| 生成日期 | 2026-11-20 |
| C++ 分析工具 | `lizard 1.24.0` |
| QML 分析工具 | `scripts/quality/qml-ast-metrics.js`，解析器 `tree-sitter-qmljs 0.3.1` |
| 分析方式 | C++ 使用函数级静态度量；QML 使用 AST 结构与 JavaScript 复杂度，文件清单来自 `lens_app.rsp` |
| 第三方代码 | 未纳入 |
| 构建产物 | 未纳入 |
| 对比基线 | [`source-code-review-hotspots-2026-10-03.md`](source-code-review-hotspots-2026-10-03.md) |

本报告用于定位需要优先审阅的代码热点，不把单个指标当作代码正确性或架构质量的充分证明。与 10-03 版相比，取数规模扩大了一个量级，绝对数字不能与旧报告直接相减，见第 7 节。

## 2、执行摘要

`src/` 共包含 498 个源码文件，物理行数为 92,182 行：C++ 和头文件 381 个（66,822 行），QML 117 个（25,360 行）。

C++ 共分析出 2,644 个函数，文件级 NLOC 为 45,138 行，函数级 NLOC 为 33,716 行，平均圈复杂度为 3.53。整体平均值与 10-03 持平（当时 3.61），但规模从 155 个函数涨到 2,644 个，超阈值的函数随绝对数量一起放大：

- 151 个函数的圈复杂度大于 10，33 个大于 20。
- 102 个函数的物理长度大于 50 行，225 个函数的 NLOC 大于 30。
- 140 个函数超过 3 个参数，25 个超过 5 个参数。
- 最高圈复杂度为 54，有两处并列：`core::pet::parseAnimations` 与 `capture::parseTesseractTsv`。
- 单个函数最深嵌套为 11 层，位于 `core::telemetry::accumulate`；`ocr::parseResponse`（Google 实现）为 10 层。嵌套 4 层及以上的函数有 57 个。

QML 共 117 个文件、2,002 个对象、7,636 个属性绑定、528 个 signal handler。最高 JavaScript 圈复杂度为 43（`PreviewSample.qml::payload`），最高 cognitive complexity 同为 43；12 个方法的圈复杂度大于 10，5 个方法的 cognitive complexity 大于 15。

本轮同时完成了 `lemmatize` 与 `parseExplanations` 路径的拆分，前后数字见 4.3：`lemmatize` 的 CCN 从 14 降到 8，`parseExplanations` 所在的内容解析函数从 38 降到 8。

## 3、范围和规模

### 3.1 文件和物理行数

物理行数含空行与注释。

| 范围 | 文件数 | 物理行数 |
| --- | ---: | ---: |
| `src/app` C++ 和头文件 | 201 | 36,974 |
| `src/capture` C++ 和头文件 | 30 | 3,968 |
| `src/core` C++ 和头文件 | 73 | 13,795 |
| `src/llm` C++ 和头文件 | 8 | 3,413 |
| `src/ocr` C++ 和头文件 | 20 | 2,287 |
| `src/pictures` C++ 和头文件 | 7 | 975 |
| `src/reporter` C++ 和头文件 | 1 | 302 |
| `src/speech` C++ 和头文件 | 16 | 1,398 |
| `src/sync` C++ 和头文件 | 8 | 1,517 |
| `src/update` C++ 和头文件 | 4 | 1,015 |
| `src/util` C++ 和头文件 | 13 | 1,178 |
| C++ 合计 | 381 | 66,822 |
| `src/app/qml` QML | 117 | 25,360 |
| `src` 合计 | 498 | 92,182 |

### 3.2 C++ 模块分布

函数 NLOC 只统计函数体内的逻辑行，文件级 NLOC 另含命名空间、全局与类型声明，两者不可相加对比。

| 模块 | 文件数 | 函数数 | 函数 NLOC | 文件级 NLOC | 圈复杂度合计 | 平均圈复杂度 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `app` | 201 | 1,633 | 18,647 | 24,936 | 4,890 | 2.99 |
| `capture` | 30 | 138 | 2,042 | 2,839 | 584 | 4.23 |
| `core` | 73 | 509 | 7,022 | 9,415 | 2,341 | 4.60 |
| `llm` | 8 | 88 | 1,823 | 2,238 | 489 | 5.56 |
| `ocr` | 20 | 77 | 1,290 | 1,732 | 354 | 4.60 |
| `pictures` | 7 | 30 | 509 | 634 | 105 | 3.50 |
| `reporter` | 1 | 8 | 170 | 229 | 38 | 4.75 |
| `speech` | 16 | 46 | 525 | 791 | 132 | 2.87 |
| `sync` | 8 | 49 | 811 | 1,069 | 168 | 3.43 |
| `update` | 4 | 27 | 441 | 603 | 101 | 3.74 |
| `util` | 13 | 39 | 436 | 652 | 135 | 3.46 |
| 合计 | 381 | 2,644 | 33,716 | 45,138 | 9,337 | 3.53 |

`llm` 的平均圈复杂度最高（5.56），主要来自两处：协议加载与响应解析的长分支；`core` 的复杂度合计最大（2,341），集中在词形处理、持久化读取、宠物配置与遥测；`app` 的函数数和 NLOC 最多，平均值反而最低（2.99），因为表面接线与 duty 编排大多是短函数。

## 4、C++ 指标

### 4.1 函数级阈值统计

| 指标 | 结果 | 本次采用的警戒线 |
| --- | ---: | ---: |
| 函数总数 | 2,644 | - |
| 平均圈复杂度 | 3.53 | 10 |
| 圈复杂度大于 10 | 151 | 10 |
| 圈复杂度大于 20 | 33 | 20 |
| 函数长度大于 50 行 | 102 | 50 |
| NLOC 大于 30 | 225 | 30 |
| 参数数大于 3 | 140 | 3 |
| 参数数大于 5 | 25 | 5 |
| 单个函数最深嵌套 | 11 层 | 3 |
| 嵌套 4 层及以上 | 57 | 3 |

lizard 的汇总口径：至少触发一条警戒线的函数有 210 个（占 7.9%），这些函数的 NLOC 占文件级 NLOC 的 30%（约 13,541 行）。阈值用于排序和复查，不是对所有超阈值函数自动判定为缺陷；初始化器、平台适配代码和错误处理分支可能合理地增加函数长度。

### 4.2 最高优先级热点

取数在 4.3 的两次拆分之前；两行拆分过的函数同时给出前后值。位置按取数时的行号。

| 函数 | 位置 | NLOC | CCN | 函数长度 | 嵌套 | 原始 MI |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `core::pet::parseAnimations` | [`pet_config.cpp:199`](../../../src/core/domain/pet/pet_config.cpp#L199) | 157 | 54 | 179 | 3 | 26.4 |
| `capture::parseTesseractTsv` | [`ocr_tsv.cpp:91`](../../../src/capture/ocr_tsv.cpp#L91) | 145 | 54 | 160 | 4 | 28.9 |
| `core::telemetry::planEventSync` | [`sync_plan.cpp:151`](../../../src/core/telemetry/sync_plan.cpp#L151) | 140 | 51 | 166 | 4 | 31.5 |
| `llm::parseExplanationContent` | [`llm_pure.cpp:502`](../../../src/llm/llm_pure.cpp#L502) | 141 | 38 → 8 | 188 → 65 | 5 | 34.2 |
| `ocr::parseResponse`（Google） | [`ocr_google.cpp:103`](../../../src/ocr/ocr_google.cpp#L103) | 97 | 35 | 104 | 10 | 42.9 |
| `core::dev::parseCommand` | [`dev_command.cpp:285`](../../../src/core/dev/dev_command.cpp#L285) | 85 | 34 | 100 | 3 | 45.2 |
| `core::KnownStore::load` | [`known_store.cpp:286`](../../../src/core/document/known_store.cpp#L286) | 72 | 34 | 86 | 5 | 48.3 |
| `app::pet::PetScenes::load` | [`pet_scenes.cpp:111`](../../../src/app/pet/pet_scenes.cpp#L111) | 105 | 33 | 120 | 5 | 40.2 |
| `app::CaptureDuty::setSetting` | [`capture_duty.cpp:346`](../../../src/app/duties/capture_duty.cpp#L346) | 95 | 29 | 97 | 2 | 47.1 |
| `core::filterWords` | [`filter_core.cpp:679`](../../../src/core/domain/filter_core.cpp#L679) | 78 | 28 | 105 | 3 | 50.8 |
| `core::suffixForms` | [`filter_core.cpp:243`](../../../src/core/domain/filter_core.cpp#L243) | 43 | 28 | 55 | 2 | 61.4 |
| `llm::scanJsonObject` | [`llm_pure.cpp:189`](../../../src/llm/llm_pure.cpp#L189) | 81 | 27 | 85 | 4 | 49.8 |
| `core::lemmatize` | [`filter_core.cpp:572`](../../../src/core/domain/filter_core.cpp#L572) | 38 | 14 → 8 | 56 → 36 | 3 | 69.2 |

其余大于 20 的函数集中在同一类形状：整段响应或配置文件的双层遍历（`capture::parseRapidOcrResponse`、`llm::loadLlmProtocol`、`core::telemetry::buildReport`、`ocr::parseResponse` 的阿里云、百度、Space 实现），以及把一次操作的多个阶段写在同一函数里的状态推进（`app::pet::PetController::sync`、`app::CaptureDuty::onOcrFinished`、`core::subtitle::assVisibleText`）。

原始 MI 使用 10-03 报告的同一公式计算：

```text
MI = 171 - 5.2 × ln（Halstead Volume） - 0.23 × CCN - 16.2 × ln（NLOC）
```

Halstead Volume 来自 `lizard -Ehalstead`。MI 不是 `lizard` 的原生输出，只用于同一份报告内的相对排序。

### 4.3 本轮已完成的两处拆分

两处都是纯重构：规则顺序、频率优先级、拒绝消息与日志文本逐字保留，测试未改动。

`lemmatize`：10-03 报告记为 CCN 38，本轮开始前实测已是 14（期间 `suffixForms` 等步骤已被抽出）。本轮把剩余三个阶段提为 `filter_core.cpp` 匿名命名空间里的命名步骤：`irregularBases`（查表候选）、`lemmaCandidates`（后缀规则候选加词表内的原形）、`mostFrequent`（频率排名选择），`baseFormOf` 复用同一步。CCN 14 → 8，NLOC 38 → 20，长度 56 → 36；`baseFormOf` 14 → 6。

`parseExplanations` 路径：10-03 报告记为 CCN 18，本轮开始前 envelope 已独立在 `parseExplanations`（CCN 4），主体在 `parseExplanationContent`（CCN 38）。本轮按三层职责拆开：`choiceContent`（envelope 检查）、`parseResultEntry` 加 `readResultSenses`、`emptyFieldProblem`（单条结果校验）、`orderForInputs` 与 `foldRepeatedTitles`、`orderWordResults`（顺序恢复与重复标题折叠），计数消息收进 `echoedCountFailure`。`parseExplanations` CCN 4 → 2，`parseExplanationContent` 38 → 8，拆分后最大的新步骤是 `parseResultEntry`（9）。

守卫与性能：`CoreTest.*` 与 `WordbookTest.*` 共 31 例通过；`LlmTest.*` 与 `StreamAssembly.*` 共 113 例通过。`CoreTest.FilterWordsThroughput` 在 Debug 树（`build-ninja`）同一口径下三次运行的中位数：拆前 1,391.7 ms、`lemmatize` 拆后 1,386.0 ms、两处都拆后 1,365.8 ms，200 轮候选数三次都是 3,471,200，没有退化。Debug 数字不能与 release 基线（约 19.5 ms）比较，只用于同树前后对照。

### 4.4 热点解释

#### `core::pet::parseAnimations`

代码位置：[`pet_config.cpp:199`](../../../src/core/domain/pet/pet_config.cpp#L199)。该函数同时处理动画表的结构校验、每一帧的字段读取与默认值补齐。CCN 54 与函数长度 179 行都处于本次最高一档（CCN 与 `parseTesseractTsv` 并列）。建议按 “表结构 → 单个动画 → 单帧” 三层拆开，拆分前先补一份非法配置样例集，当前只有合法配置的解析用例。

#### `capture::parseTesseractTsv`

代码位置：[`ocr_tsv.cpp:91`](../../../src/capture/ocr_tsv.cpp#L91)。TSV 的列数校验、坐标解析、逐行合并与词框建立混在一起，长度 160 行。与 `parseRapidOcrResponse`（CCN 26）形状相同，两处可以共用同一套 “行列切分 + 矩形校验” 的步骤命名。

#### `core::telemetry::planEventSync`

代码位置：[`sync_plan.cpp:151`](../../../src/core/telemetry/sync_plan.cpp#L151)。一次同步计划的生成把事件筛选、去重、分批与远端状态合并在一个循环里，CCN 51、NLOC 140。这是阶段 2 契约测试之外最值得先拆的纯函数：输入输出都是数据结构，拆完可以直接用现有用例守住。

#### `llm::parseExplanationContent`

已在 4.3 拆分。剩下的 `scanJsonObject`（CCN 27）是流式预览的半成品 JSON 读取器，`parsePartialExplanation` 依赖它逐段前进，建议保留现状，只补边界输入用例。

#### `core::KnownStore::load`

代码位置：[`known_store.cpp:286`](../../../src/core/document/known_store.cpp#L286)。文件读取、顶层校验、等级与语言读取、known 与 cache 解析仍在同一函数（CCN 34）。10-03 报告已给过同样的拆分建议，本次未动，理由是本轮只拆两个函数。

#### `core::filterWords` 与 `core::suffixForms`

代码位置：[`filter_core.cpp:679`](../../../src/core/domain/filter_core.cpp#L679) 与 [`filter_core.cpp:243`](../../../src/core/domain/filter_core.cpp#L243)。两者的 CCN 都是 28，仍高于 15 的验收线。`filterWords` 的短路顺序有成本含义，拆分时必须保留条件求值顺序；本条留给下一批，见第 8 节。

## 5、QML 指标

QML 的文件清单来自 `build-ninja/src/app/.rcc/qmllint/lens_app.rsp`，共 117 个文件，全部解析成功，没有 `ERROR` 或 `MISSING` 节点。`qtest` 下的 `tst_*.qml` 不在清单内。

### 5.1 文件与目录规模

物理行数按文件换行计数。

| 目录 | 文件数 | 物理行 | QML 对象 | 属性绑定 | signal handler 函数 | 最大对象深度 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 根目录（`Main.qml` 等） | 3 | 993 | 34 | 43 | 46 | 2 |
| `bubble/` | 11 | 2,462 | 144 | 667 | 31 | 6 |
| `capture/` | 2 | 402 | 30 | 122 | 10 | 5 |
| `components/chrome/` | 7 | 992 | 57 | 171 | 15 | 7 |
| `components/content/` | 5 | 1,354 | 75 | 333 | 15 | 7 |
| `components/controls/` | 13 | 1,797 | 113 | 434 | 26 | 7 |
| `components/preview/` | 2 | 163 | 2 | 4 | 5 | 1 |
| `dev/` | 1 | 39 | 1 | 0 | 0 | 1 |
| `panels/` | 20 | 6,073 | 579 | 2,236 | 111 | 12 |
| `pencil/` | 1 | 173 | 14 | 62 | 8 | 6 |
| `pet/` | 9 | 1,467 | 100 | 400 | 23 | 6 |
| `selection/` | 2 | 859 | 57 | 196 | 16 | 5 |
| `selection/bodies/` | 4 | 1,072 | 102 | 402 | 18 | 8 |
| `settings/` | 6 | 1,180 | 102 | 260 | 48 | 6 |
| `settings/pages/` | 16 | 4,248 | 429 | 1,640 | 138 | 9 |
| `settings/previews/` | 6 | 1,652 | 147 | 657 | 16 | 9 |
| `theme/` | 9 | 434 | 16 | 9 | 2 | 2 |
| 合计 | 117 | 25,360 | 2,002 | 7,636 | 528 | 12 |

绑定最集中的单文件是 [`ReportCard.qml`](../../../src/app/qml/panels/ReportCard.qml)：79 个对象、275 个属性绑定、对象树深度 12，三项都是全模块第一；其次是 `BubbleCard.qml`（254 个绑定）、`OnboardingWizard.qml`（251 个）、`SettingsSyncPage.qml`（250 个）。

### 5.2 AST 结构和 JavaScript 复杂度

| 指标 | 数值 |
| --- | ---: |
| 文件数 | 117 |
| AST QML 对象 | 2,002 |
| AST 属性绑定 | 7,636 |
| 绑定成员根引用 | 972 |
| 绑定边 | 5,238 |
| signal handler 函数 | 105 |
| 命名 JavaScript 方法 | 337 |
| 最大对象树深度 | 12 |
| 单个绑定最大成员根数 | 16 |
| 最高 JavaScript 圈复杂度 | 43 |
| 最高 cognitive complexity | 43 |
| 圈复杂度大于 10 的方法 | 12 |
| cognitive complexity 大于 15 的方法 | 5 |
| 物理长度大于 50 行的方法 | 5 |

复杂度最高的方法：

| 方法 | 文件 | 行数 | CCN | cognitive |
| --- | --- | ---: | ---: | ---: |
| `payload` | [`components/preview/PreviewSample.qml`](../../../src/app/qml/components/preview/PreviewSample.qml) | 74 | 43 | 43 |
| `show` | [`bubble/Bubble.qml`](../../../src/app/qml/bubble/Bubble.qml) | 108 | 41 | 40 |
| `placeAt` | [`selection/SelectionBar.qml`](../../../src/app/qml/selection/SelectionBar.qml) | 71 | 23 | 26 |
| `layFrom` | [`components/content/RevealText.qml`](../../../src/app/qml/components/content/RevealText.qml) | 73 | 17 | 19 |
| `dismissOutside` | [`SurfaceHost.qml`](../../../src/app/qml/SurfaceHost.qml) | 46 | 17 | 18 |
| `testLine` | [`settings/pages/SettingsOnlineOcrPage.qml`](../../../src/app/qml/settings/pages/SettingsOnlineOcrPage.qml) | 27 | 16 | 15 |
| `containsPoint` | [`selection/SelectionBar.qml`](../../../src/app/qml/selection/SelectionBar.qml) | 23 | 14 | 14 |
| `figureFor` | [`panels/ReportCard.qml`](../../../src/app/qml/panels/ReportCard.qml) | 20 | 14 | 12 |
| `relayout` | [`settings/previews/VocabPreview.qml`](../../../src/app/qml/settings/previews/VocabPreview.qml) | 50 | 12 | 12 |

`PreviewSample.qml` 是设置页的预览样本，`payload` 是一个按场景拼装样例数据的调试入口；它与 `Main.qml::profileScenario` 同类，不进入产品路径，但 43 的 CCN 会让 “最高复杂度” 长期指向它，建议移入只在测量构建里加载的文件。产品侧最高的是 `Bubble.qml::show`（41 / 40），其次 `SelectionBar.qml::placeAt`（23 / 26），两者都是布局决策函数。

组件扇入（AST 中以组件类型名出现的对象计数）：`Chip` 40、`Icon` 32、`GlassSurface` 27、`IconButton` 与 `DropdownField` 各 24、`SwitchRow` 23、`GlassHost` 与 `ShadowCard`、`FocusRing` 各 21。该计数不含 singleton 或动态加载建立的依赖。

10-03 版记录的 `Main.qml::dismissOutside` 已迁到 [`SurfaceHost.qml`](../../../src/app/qml/SurfaceHost.qml)，`Main.qml` 现在只剩容器接线（396 行）。

## 6、其他观察

源码中找到 4 个 `TODO` 标记，都与 `TODO.md` 的条目对应，不是未处理的临时注记：

- [`explanation_duty.h:91`](../../../src/app/duties/explanation_duty.h#L91)：气泡悬停抖动条目。
- [`filter_core.cpp:778`](../../../src/core/domain/filter_core.cpp#L778)、[`filter_core.cpp:856`](../../../src/core/domain/filter_core.cpp#L856) 与 [`filter_core.h:143`](../../../src/core/domain/filter_core.h#L143)：标记只标注候选、不删除候选（item 0）。

单文件行数：`pet_controller.cpp`（2,371 行）与 `capture_duty.cpp`（1,454 行）超过 1,500 行的预警线；`llm_pure.cpp` 在本次拆分后为 1,415 行、`app_controller.cpp` 为 1,234 行，都在逼近。拆分一个函数会增加声明与注释的行数，所以 “降复杂度” 和 “降行数” 是两条线，本报告分开看。

## 7、阅读注意事项

- 函数计数包含匿名命名空间函数与头文件内联实现；宏展开不计，lambda 不单列。
- CCN 把每个 `if` / `for` / `while` / `case` 以及 `&&` / `||` 各计 1，因此布尔守卫密集的解析函数会偏高。
- 文件级 NLOC（45,138）与函数级 NLOC（33,716）是两种口径，后者只统计函数体内的逻辑行；模块表两列都给出，不要跨列相加。
- 第 4.2 节的取数在本轮两次拆分之前；拆分过的两行已标注前后值，其余行需要现状时按第 9 节重跑。
- MI 由本报告的公式计算，不是 `lizard` 的输出；不同工具对注释与归一化的处理不同，只在报告内做相对排序。
- QML 指标来自 `lens_app.rsp` 的 117 个文件，不含 `qtest` 的用例文件；AST 指标是趋势指标，`tree-sitter-qmljs` 的已知限制是 grouped binding 记法可能被解析成对象定义，所以对象数与绑定边不是绝对事实。
- QML 的 CCN 与 cognitive complexity 不能与 C++ 的 CCN 直接比较。
- 本轮没有重跑 `qmllint` 与 `qmlprofiler`：`scripts/quality/qml-lint.sh` 的警告棘轮仍是 QML 诊断的权威，运行时数字在 `docs/metrics/runtime-performance/` 各自的报告里。
- 4.3 的性能数字来自 Debug 树，只用于同一棵树上的前后对照；与 release 基线（[`filtercore-throughput-2026-11-10.md`](../runtime-performance/filtercore-throughput-2026-11-10.md)）不是同一口径。

## 8、建议处理顺序

1. 先拆 `parseAnimations`、`parseTesseractTsv`、`planEventSync` 三个 CCN 超过 50 的解析函数；三者输入输出都是纯数据，拆完由现有用例与样例集守住。
2. 再拆 `KnownStore::load`（CCN 34），保留 “文件存在但 JSON 损坏时不重置” 的行为。
3. `filterWords` 与 `suffixForms`（各 28）留给下一批，拆分时保留短路顺序与频率优先级，样例集必须零变化。
4. OCR 的五个同名 `parseResponse` 先核对拒绝口径的差异，再决定是否共用步骤，不要为降 CCN 统一错误语义。
5. 行数收口：`pet_controller.cpp` 与 `capture_duty.cpp` 按职责移到独立文件；`llm_pure.cpp` 1,415 行，下一个改动优先把流式解析整块移出。
6. QML 侧先处理 `PreviewSample.qml::payload`（调试入口移出产品模块），再按界面回归处理 `Bubble.qml::show` 与 `SelectionBar.qml::placeAt`。
7. 不建议现在把阈值设为构建失败条件；本报告的用途是排序，不是门禁。

## 9、复现命令

在仓库根目录执行 C++ 度量：

```powershell
python -m lizard src -l cpp -Ehalstead -ENS -C 10 -L 50 -a 5
```

执行 QML AST 度量（需要 `npm ci` 与已配置的构建树）：

```powershell
npm run qml:ast-metrics
```

本轮两处拆分的守卫与性能对照：

```powershell
powershell -NoProfile -File scripts/qa/gtest.ps1 -Filter 'CoreTest.*:WordbookTest.*'
powershell -NoProfile -File scripts/qa/gtest.ps1 -Filter 'LlmTest.*:StreamAssembly.*'
powershell -NoProfile -File scripts/qa/gtest.ps1 -Target lens_gtest_perf
```

拆分后重测单文件复杂度：

```powershell
python -m lizard src/core/domain/filter_core.cpp src/llm/llm_pure.cpp -l cpp -Ehalstead -ENS -C 10 -L 50 -a 5
```

参数含义：

- `-l cpp`：只分析 C++。
- `-Ehalstead`：计算 Halstead 指标。
- `-ENS`：计算嵌套结构深度。
- `-C 10`：将圈复杂度大于 10 的函数列入警告。
- `-L 50`：将函数长度大于 50 行的函数列入警告。
- `-a 5`：将参数数大于 5 的函数列入警告。

QML 脚本自动选择生成的 `.rsp` 文件；构建目录不存在或未配置 QML response file 时，脚本会失败并提示先配置构建树。
