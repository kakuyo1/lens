# 指标报告

> 权威：指标索引。带日期的文件是那一天的采样快照，不随源码漂移而改；`profiling-and-coverage.md` 是当前测量方法，只有设施或口径变化时才改。

## accessibility

| 文件 | 回答的问题 |
| --- | --- |
| [`contrast-2026-10-12.md`](accessibility/contrast-2026-10-12.md) | 暗夜与两套玻璃主题的文字对比度各是多少，哪些组合不达 WCAG AA |

## ocr

| 文件 | 回答的问题 |
| --- | --- |
| [`ocr-baseline-2026-10-28.md`](ocr-baseline-2026-10-28.md) | 合成语料的 Tesseract 英语、中文与混排基线；Windows OCR 在本机是否可用、为何不能作为交付默认；RapidOCR v0.2.0 的协议、退出与非 ASCII 模型路径审计；哪些后端与在线请求仍未验证 |
| [`windows-ocr-sparse-package-2026-10-28.md`](windows-ocr-sparse-package-2026-10-28.md) | 签名稀疏 MSIX 的探针矩阵：机器级证书信任下包标识、中文 Windows OCR、升级与卸载通过；英语 OCR 能力因 Windows Update 停滞未验证 |
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
| [`coverage/test-coverage-baseline-2026-10-05.md`](coverage/test-coverage-baseline-2026-10-05.md) | 哪些 C++ 行与 QML 求值位置被测试跑过；取代 2026-10-03 那份 |
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

## resource-footprint

| 文件 | 回答的问题 |
| --- | --- |
| [`package-size-2026-10-05.md`](resource-footprint/package-size-2026-10-05.md) | 安装包里哪些东西占地方，能减掉哪几项 |
| [`idle-memory-2026-10-05.md`](resource-footprint/idle-memory-2026-10-05.md) | 空闲时进程占多少内存，占在哪 |
| [`memory-leak-2026-10-05.md`](resource-footprint/memory-leak-2026-10-05.md) | 长时间运行与真实使用会不会让内存只涨不跌 |
| [`glass-memory-2026-10-18.md`](resource-footprint/glass-memory-2026-10-18.md) | 开了流光玻璃主题为什么占到 967 MB、804 线程，涨在哪，怎么量的 |

## 测量方法

| 文件 | 回答的问题 |
| --- | --- |
| [`profiling-and-coverage.md`](profiling-and-coverage.md) | profiling 怎么开、覆盖率两条线各数什么，为什么读数不可比 |

补测清单见 [`coverage/coverage-plan-2026-10-03.md`](coverage/coverage-plan-2026-10-03.md)。
