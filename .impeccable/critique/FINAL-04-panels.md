---
target: 批次④ 数据面板（词汇 / 统计 / 年度）· 最终版
total_score: 35
max_score: 40
p0_count: 0
p1_count: 0
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\WordsPopup.qml"
target_fingerprint: "sha256:4967d1fcc716ec13945de5d4d82f6d6f97105fe5284ac2b500df4a98a63e50e0"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\WordsPopup.qml"
timestamp: 2026-10-25T00-23-11Z
slug: src-app-qml-panels-wordspopup-qml
final: true
final_commit: 6d85341
code_commit: 5dbe839
---

# 数据面板 · 最终 critique

**方法**：最终源码逐条对照 + 战役内真机快照（`%TEMP%\lens-crit4\words-list-light.png`、`words-removal-light.png`、`words-removal-dark.png`、`%TEMP%\lens-keyboard-qa\year-words-light.png`、`year-words-dark.png`、`year-words-palette.png`、`year-words-closed.png`、`%TEMP%\lens-year-dual.png`）+ `tst_panels.qml`、`tst_year_keyboard.qml` 的末次全量。最终版没有重新拍摄统计 / 花费两卡；这两张按源码与既有快照记。

**范围**：`WordsPopup.qml`、`YearWordsPopup.qml`、`StatsPopup.qml`、`CostPopup.qml`、`WordsListRow`、`StatRow`、`RemovalQuestion`、导入与删除路径；UI-SPEC §4.7 / 4.8 / 4.8b / 4.9、ADR 0104。评分不把托盘数据行与 README 资产算进来。

## 1. 最终读数

**35 / 40（Good，六面最高）**。趋势：22 → 26 → 32 → 34 → **35**。

最后 +1 只给年度键盘链：371 格、5 枚图例格、12 枚色相点从「有名字无门」变成「一个 Tab 站 + 方向键 + Enter」。其余分数没有虚涨——选中的 1px 内环、不可见的深浅阈值、死掉的 Known / New 统计行都还在账上。

| # | 启发式 | 分 | 最终依据 |
|---|---|---|---|
| 1 | 系统状态可见 | 3 | 选中日在摘要行换成完整读数是强反馈；但格子只有 1px 内描边、无 hover，一臂距离外几乎看不见 |
| 2 | 贴合真实世界 | 3 | 「833 次 · 30 词」双口径与 ADR 0104 对齐；默认标签改成「Lookups · 365 days」，不再和右侧「30 words」撞词；残留是日期语法不统一 |
| 3 | 用户控制 | 4 | Esc 按视觉层级剥（调色层 → 年度卡），删除是模态确认，取消不罚，卡片可拖 |
| 4 | 一致与标准 | 3 | 四枚图标（日历 / 导出 / 导入 / 返回 / 关闭）写进了 §4.8，导入只追加的语义也有字；残留是总量行 12px 对规格 11.5px、日期完整 ISO 对词表短式 |
| 5 | 错误防范 | 4 | 只读数据、配色可逆；「不再询问」默认关、取消不动开关；删除的不可逆分量交给句子，不靠恐吓色 |
| 6 | 识别而非回忆 | 3 | 日历 glyph 让年度有了看得见的门；图例解释颜色编码，但 1 / 2–3 / 4–5 / 6+ 的阈值仍不可见 |
| 7 | 灵活与高效 | 4 | 年度一个 Tab 站、方向键按日 / 周移动、Page / Home / End 到边界；图例与色相点各有 Tab 与 Enter；词汇的筛选 / 导出随筛选齐全 |
| 8 | 美学与极简 | 4 | 11px 地板 + 2px 格距后，53 列精确铺满 340px；月份、星期、图例在 1x 下清晰，卡片安静 |
| 9 | 错误识别与恢复 | 4 | 空年份退化为全中性格仍可读；删除有完整取消；导入失败有状态行，且不改变卡片高度 |
| 10 | 帮助与文档 | 3 | 图例自明；阈值仍不可见，格子也没有 tooltip；导入的追加语义只在规格里，卡上没有一句 |

**认知负荷**：通过分组、视觉层级、一次一事、渐进披露；未完全通过的是「最少选择」——词汇页顶部有筛选、导出、导入、日历、返回、关闭六个入口，年度页又加调色层，行为都要先学一遍。

## 1.1 本面优先项

按全局 C 顺序（见 `FINAL-00-overview.md` §4）；「重做仪表盘」不在此列。

| 序 | 候选 | 项 | 级别 |
|---|---|---|---|
| 1 | C4 | 年度选中 / 阈值 / 筛选空态 | P2 / P3 |
| 2 | C5 | 统计 Known / New 下钻 | P2 |
| 3 | C14 | 数字与日期 | P3 |

## 2. 专属性判定

**Authored。** 色相环的最短弧旋转与直立环心、双口径并列的摘要行、`noteRowWidth` 的宽度握手、删除时列表先折叠再落地的编舞，都无法移植到通用仪表盘。词汇卡本身朴素，但和年度卡说同一家族方言：次数是事件，词是去重后的词汇；这条口径已经在界面、读屏与规格三处统一。

## 3. 最终切片里真正改掉的

1. **年度有了键盘与读屏路径**：`YearWordsPopup.qml:317-350` 把 371 格收成一个 Tab 站，`Accessible.role: Table`，方向键按日 / 周移动，PageUp / PageDown 按周，Home / End 到首尾；`selectDayAt()` / `moveSelectedDay()` 是纯函数，qtest 可离线驱动。选中、焦点与 hover 共用一个外置环：选中 1px `text`，网格有焦点时 2px `ink`，hover 1px `muted`（`:366-380`），4px 的格子不在格内挤三个状态。
2. **图例与色相点进了键盘链**：图例格外层 16×16 的命中区（`:405-433`），视觉仍是 9px；每格有名称、按压动作、Enter / Space、FocusRing。12 枚色相点各有名称、Enter / Space、FocusRing 与外置选中环（`:575-620`）。
3. **年度入口显性化**：词表标题行加了日历 glyph（`WordsPopup.qml:280-285`），保留点标题作为快捷键；qtest 覆盖日历入口与标题入口两条路。
4. **摘要口径与空态**：默认标签改成「Lookups · 365 days」（`:232-235`），和右侧次数 / 去重词双口径不再撞词；词汇卡空态加了「Words you look up will appear here.」（`WordsPopup.qml:395-399`），不再只有计数和分段。
5. **删除默认恢复询问**：`RemovalQuestion.qml` 的 `dontAsk` 默认从 true 改为 false，UI-SPEC 4.8 同步改；删除仍用 `danger` 字，但不填墨底。
6. **导入有了状态与语义**：导入成功 / 失败是一行会自清的 `importStatus`，放在计数行里，卡片不因它长高（`WordsPopup.qml:72-74`、`:350-352`、`:471-486`）；§4.8 写明导入只追加、已有词跳过、文件内去重、新词记为新词。

## 4. 问题账（已关闭）

| 来源 | 问题 | 结局 |
|---|---|---|
| 第 4 轮 P1 | 年度卡键盘 / 读屏死区：371 格、图例、色相点全是 TapHandler-only | 一个 Tab 站 + 方向键 + Page / Home / End；图例与色相点各有 Tab / Enter；qtest 两例 |
| 第 2 轮 P2 | 标题行四枚图标对规格三枚，导入语义不存在 | §4.8 改为日历 / 导出 / 导入 / 返回 / 关闭，并写清导入只追加 |
| 第 3 轮 P3 | 「Words explained · 365 days」与「30 words」同词两义 | 改成「Lookups · 365 days」 |
| 第 3 轮 P3 | 词汇卡空态只有计数与分段 | 加一行弱文字兜底 |
| 第 4 轮 P3 | 图例 9px 格命中区偏小 | 外层 16px 命中区，视觉 9px 不变 |
| 第 3 轮 P3 | 选中格 1px 描边画在 4px 格内、无 hover | 已实现（C4）：一个外置环，选中 1px `text`、焦点 2px `ink`、hover 1px `muted` |
| 第 3 轮 P3 | 月份标签在最后两周可能画出右缘 | **未修**，仍是潜在结构问题 |
| 第 4 轮 P3 | 日期读数完整 ISO，词表用短式 | **未统一**，仍在账上 |

## 5. 仍然开着的问题

| 优先级 | 问题 | 证据 | 为什么还没关 |
|---|---|---|---|
| P2 | 统计卡的 Known / New 两行不可下钻 | `StatsPopup.qml:111-126` 只有 Cost 与 All time 设 `clickable: true`；Known / New 是 StaticText | 已实现（C5）：两行可点、可键盘，打开词汇卡并预设对应筛选 |
| P3 | 选中格仍只有 1px 内描边，无 hover | `YearWordsPopup.qml:363-366`；格子 4px，描边在格内 | 已实现（C4）：选中环与 hover 环外置，焦点环仍是 2px `ink` |
| P3 | 深浅四档阈值不可见 | 图例只画 5 个色块，1 / 2–3 / 4–5 / 6+ 的数字没有；格子无 tooltip | 已实现（C4）：图例下补一行 `1 · 2–3 · 4–5 · 6+ lookups` |
| P3 | 筛选结果为空时没有一句话 | 空态只在 `Controller.words.length === 0` 时出现（`WordsPopup.qml:377-399`）；筛选排除全部时列表高 0，没有任何文字 | 已实现（C4）：筛选 settle 后给一行 “No words in this filter.”；夹具两侧都有词时该用例按 skip 记录 |
| P3 | 统计 / 花费的 token 数没有千分位 | `CostPopup.qml:135-147` 用 `String(...)` 直接拼「%1 tokens」 | 第 2 轮遗留；四位数以上的 token 读起来要数零 |
| P3 | 日期语法两套 | 年度读数用完整 ISO（`:233-234`），词表用「Today / Yesterday / MM-DD」（`WordsPopup.qml:140-148`） | 第 4 轮 P3；同族表面最好一种日期语法 |
| P3 | 导入的追加语义只在规格里 | 卡上只有成功 / 失败，没有「已有词跳过」之类的提示 | 导入是低频动作，可以只留在规格；但它确实要求读者先读文档 |
| P3 | 总量行 12px 对 §4.8 的 11.5px | 设计阶梯允许 11–13，但两份文档该统一 | 规格漂移，不影响可读 |

## 6. 边界（不是待办）

- **年度卡整卡可拖，词汇卡只许标题行拖**：两种拖拽语法是刻意的——词汇列表占着手势，整卡拖会吃掉行的点击；年度卡的网格没有滚动，整卡拖更自然。这不是漂移。
- **年度卡是奖励面，不是统计工作台**：它回答「今年查了多少」，不承担筛选与下钻；把 Known / New 的下钻放回统计卡，方向更对。
- **调色层是年度卡的次表面**：它不是弹出层，不铺 78% 白，沿用卡片玻璃；Esc 先收调色层再收卡，层级正确。
- **月度标签的溢出风险**是结构性的：月份文本框宽 18px，月份起于最后两周时可能画出内容右缘；当前 53 列宽度下未在真机看到，但 Item 不裁剪，改 cellSize 前要先修。

## 7. 画像

- **Sam（读屏 + 键盘）**：年度卡这轮真的能用了——一个 Tab 站、方向键、Home / End，格子名字带日期与次数；图例与色相点也能按；筛选空态与 Known / New 下钻已补。仍在的是调色层阈值只在图例行末，以及 11px faint 月份在浅色卡上刚过 AA 的边缘。
- **Riley（赶时间）**：想确认 streak，目标仍是 4px 格 + 1px 描边，手一滑会碰到旁边的格子；调色层现在有键盘，但指针仍要精准点 9px 的视觉格（16px 命中区好一些）。
- **Wei（首日新用户）**：词汇卡空态第一次说人话；年度卡空年份仍可读；token 千分位是下一层的小刺，筛选空态已有自己的句子。
- **预算敏感者**：花费卡的 token 数没有分组，金额有；两栏一起读时，token 那一栏更费力。

## 8. 开放问题

1. 色阶阈值既然已经站在 11px 地板上，能不能在图例格下直接标 1 / 3 / 5 / 6+？图例解释了一半编码，另一半仍是读者自己猜。
2. Known / New 统计行要不要复用 Cost / All time 的下钻？「今天已会」点进去看词表，比再开一次词汇卡自然。
3. 筛选为空时，是该说「这个筛选下没有词」，还是直接把筛选退回全部？前者诚实，后者省一步；两种都比现在的空白好。
4. 年度卡要不要记住上次选中的日期？现在每次打开都从今天开始，键盘用户想接着看昨天，要重新走一遍。

## 9. 证据与未采样

**证据**：`%TEMP%\lens-crit4\words-list-light.png`、`words-removal-light.png`、`words-removal-dark.png`；`%TEMP%\lens-keyboard-qa\year-words-light.png`、`year-words-dark.png`、`year-words-palette.png`、`year-words-closed.png`；`%TEMP%\lens-year-dual.png`；`tst_panels.qml` 的导入 / 导出 / 删除 / 空态 / 日历入口；`tst_year_keyboard.qml` 的箭头、图例、色相点、焦点进入即选日。

**未采样**：玻璃两主题下的年度卡、真实色弱读者对五档色的区分、调色层展开 / 收起的逐帧、筛选为空的实际布局、月份标签在最后两周的溢出、真实读屏器朗读 371 格的顺序。
