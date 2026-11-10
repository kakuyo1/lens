# 指标报告

> 权威：指标索引。带日期的文件是那一天的采样快照，不随源码漂移而改；`profiling-and-coverage.md` 是当前测量方法，只有设施或口径变化时才改。

## accessibility

| 文件 | 回答的问题 |
| --- | --- |
| [`contrast-2026-10-12.md`](accessibility/contrast-2026-10-12.md) | 暗夜与两套玻璃主题的文字对比度各是多少，哪些组合不达 WCAG AA |

## ocr

| 文件 | 回答的问题 |
| --- | --- |
| [`ocr-baseline-2026-10-28.md`](ocr-baseline-2026-10-28.md) | 合成语料的 Tesseract 英语、中文与混排基线；RapidOCR v0.2.0 的协议、退出与非 ASCII 模型路径审计；哪些后端与在线请求仍未验证 |
| [`ocr-installer-2026-10-28.md`](ocr-installer-2026-10-28.md) | 安装产物的随包 Tesseract 是否能输出 TSV、`chi_sim` 是否被排除、设置/截图/密钥是否进入 portable 与 payload；覆盖升级、运行中卸载拒绝与清理复验 |

## compile-performance

| 文件 | 回答的问题 |
| --- | --- |
| [`full-build-baseline-2026-10-04.md`](compile-performance/full-build-baseline-2026-10-04.md) | 全量编译的时间花在哪，预编译头与并行度各值多少 |

## code-quality

| 文件 | 回答的问题 |
| --- | --- |
| [`test-coverage-baseline-2026-10-03.md`](code-quality/test-coverage-baseline-2026-10-03.md) | 哪些 C++ 行与 QML 求值位置被测试跑过 |
| [`source-code-review-hotspots-2026-10-03.md`](code-quality/source-code-review-hotspots-2026-10-03.md) | 哪些源码文件与函数应优先审阅 |
| [`selection-classification-accuracy-2026-10-04.md`](code-quality/selection-classification-accuracy-2026-10-04.md) | 单层选区类型判定错在哪里，是否需要多层级策略 |

## coverage

| 文件 | 回答的问题 |
| --- | --- |
| [`coverage/test-coverage-baseline-2026-10-30.md`](coverage/test-coverage-baseline-2026-10-30.md) | 哪些 C++ 行与 QML 求值位置被测试跑过；取代 2026-10-05 那份；C++ 为四个无头目标口径（pet 后接入未计入），含 1 条采集时红例（已修） |
| [`coverage/test-coverage-baseline-2026-10-05.md`](coverage/test-coverage-baseline-2026-10-05.md) | 上一份覆盖率基线；取代 2026-10-03 那份 |
| [`coverage/what-coverage-counts-2026-10-05.md`](coverage/what-coverage-counts-2026-10-05.md) | 覆盖率数的是代码行数还是别的：两条线各数什么，为什么不可比 |
| [`coverage/coverage-plan-2026-10-03.md`](coverage/coverage-plan-2026-10-03.md) | 按性价比排序的补测清单与顺序 |

## runtime-performance

| 文件 | 回答的问题 |
| --- | --- |
| [`qml-user-workflow-baseline-2026-10-03.md`](runtime-performance/qml-user-workflow-baseline-2026-10-03.md) | 真实操作流程下 QML 的时间花在哪 |
| [`qml-automated-scenario-baseline-2026-10-03.md`](runtime-performance/qml-automated-scenario-baseline-2026-10-03.md) | 无鼠标自动化场景下的可重复基线 |
| [`qml-language-list-lazy-loading-validation-2026-10-03.md`](runtime-performance/qml-language-list-lazy-loading-validation-2026-10-03.md) | 语言列表延迟加载是否把成本移出启动 |
| [`qml-tray-menu-lazy-loading-experiment-2026-10-03.md`](runtime-performance/qml-tray-menu-lazy-loading-experiment-2026-10-03.md) | 托盘菜单正文延迟加载前后对比 |
| [`glass-capture-capability-2026-10-19.md`](runtime-performance/glass-capture-capability-2026-10-19.md) | 玻璃幕布的帧率上限卡在哪：哪块屏能桌面复制、一帧值多少、GDI 读屏的真实代价 |
| [`filtercore-throughput-2026-11-10.md`](runtime-performance/filtercore-throughput-2026-11-10.md) | 当前语料与词表规模下的 FilterCore 吞吐新基线，以及插桩开与关的两树对照 |
| [`qml-scenario-2026-11-10.md`](runtime-performance/qml-scenario-2026-11-10.md) | 八步 QML 场景与 10-03 基线的对比：wall 稳定，工作量随表面与向导翻了约五倍；附鼠标停顿探针 |
| [`a3-probes-2026-11-10.md`](runtime-performance/a3-probes-2026-11-10.md) | 采集上限、读屏节奏、旋转裁剪、抓屏排除与 21 张 OCR 语料的复测，对照 10-19 与 10-28 |

## resource-footprint

| 文件 | 回答的问题 |
| --- | --- |
| [`package-size-2026-10-05.md`](resource-footprint/package-size-2026-10-05.md) | 安装包里哪些东西占地方，能减掉哪几项 |
| [`idle-memory-2026-10-05.md`](resource-footprint/idle-memory-2026-10-05.md) | 空闲时进程占多少内存，占在哪 |
| [`memory-leak-2026-10-05.md`](resource-footprint/memory-leak-2026-10-05.md) | 长时间运行与真实使用会不会让内存只涨不跌 |
| [`glass-memory-2026-10-18.md`](resource-footprint/glass-memory-2026-10-18.md) | 开了流光玻璃主题为什么占到 967 MB、804 线程，涨在哪，怎么量的 |
| [`idle-sample-2026-11-10.md`](resource-footprint/idle-sample-2026-11-10.md) | 当前 release 实例的一分钟只读采样：`-Cpu` 列的验证，以及长稳复测的起点水位 |
| [`soak-cycles-2026-11-10.md`](resource-footprint/soak-cycles-2026-11-10.md) | 四组循环负载：释放路径有没有回归、9.9 节的慢速爬升还在不在、释放的毫秒代价 |
| [`handle-types-2026-11-10.md`](resource-footprint/handle-types-2026-11-10.md) | 那笔爬升是哪类句柄：Section、Mutant、Thread 各约每轮十个，Event 与 Timer 是锯齿不是泄漏 |

## cost

| 文件 | 回答的问题 |
| --- | --- |
| [`cost/lookup-cost-2026-11-03.md`](cost/lookup-cost-2026-11-03.md) | 一次查词在 DeepSeek 上花多少钱；首次引导里 “1 元约 1000 次” 凭什么成立 |

## 测量方法

| 文件 | 回答的问题 |
| --- | --- |
| [`profiling-and-coverage.md`](profiling-and-coverage.md) | profiling 怎么开、覆盖率两条线各数什么，为什么读数不可比 |

补测清单见 [`coverage/coverage-plan-2026-10-03.md`](coverage/coverage-plan-2026-10-03.md)。
