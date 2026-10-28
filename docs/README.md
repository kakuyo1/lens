# 文档地图

先在这里找该读哪一份，再打开对应文档。`AGENTS.md` 只保留约束与这张图的短版。

## 该读哪一份

| 我要… | 先读 | 需要时再读 |
|---|---|---|
| 改产品行为、判定或默认值 | `PRODUCT.md` | `GLOSSARY.md`、`docs/adr/` |
| 设计 UI、改共用视觉约束 | `DESIGN.md` | `UI-SPEC.md`、`docs/adr/` |
| 改某个表面的外观或交互 | `DESIGN.md` → `UI-SPEC.md` §4.x | `ui-prototypes/README.md`、`docs/QML.md` |
| 改 QML 实现：窗口、阴影、定位、线程 | `docs/QML.md` | `CODING_STANDARDS.md`、`docs/adr/` |
| 改代码结构、加模块或搬文件 | `CODING_STANDARDS.md` 的 “分层” | `docs/adr/0077` |
| 改请求、响应、校验、错误码或价目 | `API.md` | `data/llm/`、`SECURITY.md` |
| 加测试或决定跑哪条 | `TEST.md` | `docs/qa.md`、`docs/metrics/profiling-and-coverage.md` |
| 在真机上看界面、拍照或驱动 | `docs/qa.md` | `.claude/skills/run-lens/SKILL.md` |
| 量性能、覆盖率或看历史数字 | `docs/metrics/profiling-and-coverage.md` | `docs/metrics/README.md` |
| 改隐私、数据边界或同步 | `SECURITY.md` | `PRODUCT.md` 的存储形状、`docs/adr/0042` |
| 改脚本、构建或发布 | `scripts/README.md` | `AGENTS.md` 的 Build、`config/README.md` |
| 改界面文案或翻译 | `AGENTS.md` 的 Translations | `i18n/*.ts` |
| 查一个术语 | `GLOSSARY.md` | 词条指向的 owner 文档 |
| 查某个决定为什么这样做 | `docs/adr/README.md` | 对应 ADR |
| 查被取代的旧规格、阶段计划或 2026-10 的 critique | `docs/history/` | `docs/history/impeccable/`、对应 ADR |
| 开发本地与在线多引擎 OCR | `docs/ocr-development.md` | `docs/adr/0130-local-and-online-ocr-are-separate-modes.md`、`docs/adr/0131-windows-ocr-waits-for-a-signed-package.md`、`docs/adr/0132-rapidocr-model-paths-stay-ascii.md`、产品与安全目标约定 |
| 查 OCR 外部调研与实验依据 | `docs/ocr-research-2026-10.md` | `TODO.md` 的 OCR 段、`docs/adr/0014` |
| 改 HTML 原型 | `ui-prototypes/README.md` | `DESIGN.md`、`UI-SPEC.md` |

## 权威分层

| 层 | 文件 | 回答 | 不写什么 |
|---|---|---|---|
| 约束与路由 | `AGENTS.md`、本文件 | 必须遵守什么、先读哪里 | 产品细节 |
| 当前规格 | `PRODUCT.md`、`DESIGN.md`、`UI-SPEC.md`、`API.md`、`SECURITY.md`、`TEST.md`、`GLOSSARY.md`、`docs/QML.md`、`docs/qa.md` | 现在是什么 | 历史、实测明细、被取代的做法 |
| 决定 | `docs/adr/` | 为什么这样定、代价是什么 | 当前状态的完整复述 |
| 证据 | `docs/metrics/` | 数字是多少、怎么量的 | 应然规则 |
| 调研 | `docs/ocr-research-2026-10.md` | 外部产品怎么做、候选方案与实验口径 | 当前规格、决定 |
| 待实现开发方案 | `docs/ocr-development.md` | 已批准方向的技术契约、实施拆分与验收门 | 已上线能力、实测结论 |
| 历史 | `docs/history/` | 以前是什么 | 当前行为 |
| 对照稿 | `ui-prototypes/` | 设计长什么样 | 行为契约 |

规格说 “现在是什么”，ADR 说 “为什么”，metrics 说 “数字是多少”，history 说 “以前是什么”。

## 文档清单

| 文件 | 拥有 | 不拥有 |
|---|---|---|
| `README.md` | 产品概览、构建、运行、打包入口 | 设计细节 |
| `AGENTS.md` | agent 约束、构建 / 测试 / 翻译规则、路由短版 | 产品与 UI 细节 |
| `PRODUCT.md` | 产品定位、通道、取词与通道规则、模型与成本、存储边界 | 视觉规格、wire format |
| `DESIGN.md` | 设计定位、设计令牌（含 frontmatter 机器层）、字体、圆角与阴影、动效原则、无障碍、文案约束 | 逐表面规格、QML 实现细节、实测数字 |
| `.impeccable/design.json` | 机器本地文件（gitignore，不入库）：`DESIGN.md` 的 Impeccable 边车，`/impeccable document` 再生成 | 任何规格事实（以 `DESIGN.md` 为准） |
| `UI-SPEC.md` | 表面结构、逐表面组件规格、动效明细 | 共用令牌与约束、QML 实现细节、实测数字 |
| `GLOSSARY.md` | 术语与 `_Avoid_` | 行为规则 |
| `API.md` | 线上协议、请求 / 响应、校验、错误码、价目 | 本地判定、UI |
| `SECURITY.md` | 数据与信任边界、防护与限制、漏洞报告 | 协议细节 |
| `TEST.md` | 测试入口、目标、样例集、记录约定 | 测量方法、真机驱动 |
| `docs/qa.md` | 真机驱动、拍照、快照、像素判读 | 测试目标 |
| `docs/metrics/profiling-and-coverage.md` | profiling 与覆盖率方法 | 某次报告的数字 |
| `docs/metrics/` | 按日期存放的实测报告 | 当前规格 |
| `docs/ocr-research-2026-10.md` | 同类产品与 OCR 引擎的调研、候选方案与实验口径 | 当前规格、决定 |
| `docs/ocr-development.md` | 多引擎 OCR 的待实现技术契约、配置、协议适配、任务生命周期、验收与阶段依赖 | 已上线声明；产品目标与安全约定分别由对应 owner 拥有 |
| `docs/QML.md` | QML 模块、窗口、阴影、定位、线程、字体事实 | 外观规格 |
| `CODING_STANDARDS.md` | 审查阶段能判的判断题 | 机械格式检查 |
| `docs/adr/` | 重大取舍与代价 | 当前规格全文 |
| `docs/history/` | 被取代的阶段规格 | 当前行为 |
| `docs/history/impeccable/` | 2026-10 六面 critique 的最终报告、过程稿与修复计划 | 当前行为、当前规格 |
| `scripts/README.md` | 脚本目录、验证入口、发布命令 | 测试口径细节 |
| `ui-prototypes/` | 每张表面的 HTML 对照稿 | 行为契约 |
| `CHANGELOG.md` / `TODO.md` | 已发布变更 / 待做事项 | 设计依据 |

## 维护规则

- **一个事实只有一个 owner**。同一规则只写在清单里标为 “拥有” 的那一份，其他文档用链接；发现重复时删掉一份，不要同步两份。
- **`DESIGN.md` 与 `UI-SPEC.md` 同一次改动同步**。改表面只改 `UI-SPEC.md`；一旦引入或改变令牌、字重、圆角、动效规则，同一次改动回写 `DESIGN.md` 的对应小节。
- **规格只写当前状态**。带日期的决定、被取代的做法、一次性实测数字不进入规格；分别放 ADR、`docs/history/`、`docs/metrics/`。
- **冲突不要静默选边**。规格与代码不一致时，在当前改动里改掉 owner 文档，并在提交或 PR 说明哪一边是错的。
- **读者可见文案以 `i18n/*.ts` 为准**。`DESIGN.md` 与 `UI-SPEC.md` 只写设计意图，不维护文案表。
- **新增文档先登记**。没有出现在本文件清单里的文档，agent 不会主动读；被取代的规格移进 `docs/history/`。
