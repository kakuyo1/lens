---
target: 批次① 面板家族（4.7-4.9+确认/更新/通知卡）第 2 轮（修复流①后）
total_score: 26
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\WordsPopup.qml"
target_fingerprint: "sha256:f19fb4121b4c939d0453dbfd21072d14d0f91667d83180a3ef9d00985ccbd1df"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\WordsPopup.qml"
timestamp: 2026-10-24T11-47-06Z
slug: src-app-qml-panels-wordspopup-qml
---
**Method: design review only (detector not applicable, native platform)** — Assessment A 子代理独立第 2 轮（re-critique，同表面集、同 slug，对照上轮 2026-10-24T09-38-19Z 报告逐条重判）。证据：panels 九件 QML 当前工作树逐行核对（含 components/controls/IconButton·Segment·FocusRing）、UI-SPEC.md 4.0/4.7/4.8/4.8b/4.9、DESIGN.md、PRODUCT.md:129。视觉降级如实声明：本子代理环境截图通道（CDN 中转）解析失败，静态像素未因修复流①改变（改动全在键盘/读屏行为层），像素结论沿用上轮同批截图的核实结果；焦点环视觉以 qtest 全绿（背景声明可信）+ DESIGN.md:262 焦点规格（2 px ink 外描边、偏移 2 px）交叉验证。边界：分段切换运行态、悬浮复习、导出/导入对话框、调色轮展开仍转 qtest/visual-qa。

# Lens 面板家族 · 设计评审（批次① 第 2 轮，修复流①后）

## 1. Design Health Score

| # | 启发式 | 分 | 关键发现 |
|---|---|---|---|
| 1 | 系统状态可见性 | 3 | 跨面板数字同源不漂；导出失败静默、筛选后标题计数不随段变、年份格无 hover 反馈（均未动） |
| 2 | 贴近真实世界 | 3 | 相对日期/¥/tokens 熟；「历史累计 30 词」vs 年度「845 words」同单位两语义仍互打脸；日均分母未说明 |
| 3 | 用户控制与自由 | 4 | **Esc 全通**：四面板+更新卡+确认卡+年度调色层一次做齐（spec 4.0 新规则落地）；删除无撤销是产品定死的例外，且有模态确认卡挡着 |
| 4 | 一致性与标准 | 3 | 控件层四件套（role/name/onPressAction+tab+Enter/Space+焦点环）跨 IconButton/StatRow/胶囊/分段统一落地；仍存：返回箭头 words/cost 居右、year 居左；spec 4.8 写三枚图标实现四枚；spec 600 字重与 DESIGN 两档互斥 |
| 5 | 错误预防 | 2 | 确认卡模态正确；「不再询问」默认开（spec 决定）、Remove/Cancel 同底同形、无撤销、导入零预览 |
| 6 | 识别而非回忆 | 2 | 年度记录唯一入口仍是点「词汇」标题（静止态无按钮相，且该 Text 无 role 无 tab——读屏与键盘依旧全盲）；folder=导入靠猜；无 tooltip |
| 7 | 灵活与效率 | 3 | **键盘导航落地**：Tab 全程可达、分段箭头键、判定/行点按 Enter/Space；仍缺：统计行→对应筛选断链、词表无搜索、无批量判定 |
| 8 | 美学与极简 | 3 | 灰黑白纪律、唯一绿、暗色明度台阶成立；噪音：每行两胶囊+常驻 × 列、365 天挤在关闭钮旁 |
| 9 | 错误恢复 | 1 | 未动：导出失败无提示、空筛选=白洞、删除后无撤销路径 |
| 10 | 帮助与文档 | 1 | 未动：判定语义后果、去重规则、日均口径全靠悟 |
| **合计** | | **26/40** | **Acceptable（22→26）：控件层键盘读屏这口气补上了，叙事与防护两处漏气依旧** |

认知负荷八项仍 2 项失败（最少选择、工作记忆）= 中等负荷；修复流①不改变选择结构，判定不变。

## 2. 设计专属性判定

**成立，且仍在「接缝」上。** 本轮新增的一层：控件层出现了一套自家语法——同一副四件套（读屏名+Tab+Enter/Space+同款焦点环）从托盘菜单、设置、气泡一路贯到面板的每个胶囊每枚叉，Segment 走 radio-group 语义（一个 tab 站+箭头键+带 checked 的单元格）。这是「全键盘可走的学习账本」的产品性格，不是通用 analytics 卡能冒充的。卡面与上轮无变化：单卡仍是通用统计卡，年度表仍是 GitHub 活跃表直拷，日子→当天弹过的词仍没有路。

## 3. 整体印象

上轮的三大 P1 之一（键盘读屏全盲区）这轮被系统性修复而非点状打补丁：spec 先立规则（4.0 Esc 句、4.7/4.8 键盘句），实现按同一契约全覆盖，qtest 锁住交接边界。这是本仓库少见的好修复节奏。剩下的分数不在这里：**「账本」的口径（30 vs 845）与就地解释（判定语义、日均分母）仍是从 Acceptable 到 Good 的那一步**，成本依旧极低，且已被流②/③/⑦排队。

## 4. 做得好

1. **Esc 语义一次做对，交接边界想透了**：application 级+enabled 守卫（与取词蒙版同款）；问题卡在屏归它、退场中面板接管（WordsPopup.qml:171-176）；年度卡两段式先收调色层（YearWordsPopup.qml:136-141）；更新卡 Esc=closeCard=「not now」而非「skip this version」（UpdateCard.qml:149-154）——三个语义边界都有注释和 qtest 背书。
2. **控件层四件套是契约不是巧合**：IconButton.qml:29-41 自带全套（调用方只欠一个名字）；StatRow 可点=Button+焦点环、不可点=StaticText+一句话名（StatRow.qml:32-42）；胶囊与确认卡按钮同款。Segment 是教科书做法：整组一个 tab 站、左右箭头移动、单元格 RadioButton 带 checked（Segment.qml:27-41, 65-71）。
3. **读屏名起得有内容**：删除叉「Remove %1」点名该词（WordsListRow.qml:250），列表里三十枚叉听声可辨；StatRow 名把「标签+注+数值」念成一句账（StatRow.qml:33），不是三段碎词。

## 5. 问题清单（按严重度）

### P0
无。

### P1
1. **接力组数字自相矛盾：30 词 vs 845 words（未动，保留）** — 统计「历史累计」= 去重词数（StatsPopup.qml:133 `Controller.words.length`，跟 UI-SPEC.md:192「都是去重后的词数」），年度右侧总和 = 弹词次数（YearWordsPopup.qml:224 `sum(day.pops)` 标 "%1 words"），PRODUCT.md:129 说「统计历史累计仍是弹词次数」——实现违反 PRODUCT 且两文档互斥。修法：次数一律「次」或双口径并列「845 次 · 30 词」，ADR 钉死口径（流②/⑦）。→ /impeccable clarify
2. **年度记录唯一入口是隐藏按钮，且是键盘读屏修复的漏网（保留+强化）** — 入口仍是点「词汇」标题（WordsPopup.qml:223 TapHandler），Text 无 Accessible.role/name、无 tab：修复流①盖住了控件层，这个入口对读屏依旧不存在、对键盘依旧不可达，对鼠标依旧靠猜。学习闭环最深的一环三路全盲。修法：图标组加日历 glyph 或词表上方「年度记录 ›」行，spec 4.8b 同步。→ /impeccable clarify

### P2
3. **年度卡格子层键盘不可达（修复流①的剩余空白）** — 日期格只有 name 无 role 无 tab（YearWordsPopup.qml:297-315），图例格有 role+name 无 tab（:327-341），色相点 21 px 有 role+name 无 tab（:481-509）；点击日期格是「那天我查了什么」的唯一路，Sam 全程够不着；调色层整层无键盘入口。修法：图例格入 tab 序（它有 role 无键，半成品），日期格给行级替代（摘要行下拉或左右键走日期）。→ /impeccable harden
4. **通知卡 action 胶囊无读屏（新发现）** — 同为无焦点窗口，更新卡三粒按钮都补了 role/name/onPressAction（UpdateCard.qml:319-321/360-362/399-401），通知卡的「现在解释」action 胶囊一个没有（Notice.qml:141-169）；关闭图标倒是传了名（Notice.qml:119）。同一契约两种待遇。修法：action 胶囊补 role/name/onPressAction，与更新卡对齐。→ /impeccable harden
5. **删除防护只有确认没有退路（保留，措辞按 spec 立场收窄）** — 无撤销（PRODUCT 定死）；「不再询问」默认开（RemovalQuestion.qml:75，spec 4.8 有明确论证：一次一问、取消不改开关，尊重该立场）；Remove/Cancel 同底同形是 spec 明文「分量由句子承担，不靠颜色吓退读者」。剩余真问题：删后 4.5 通知卡不带「撤销」，错点 Remove 即永久失词。修法：删除瞬间通知卡带撤销胶囊（known-set 在控制器侧可回写）。→ /impeccable harden
6. **年度卡排版低于全库底线（未动，流③）** — 月份/星期/Less·More 8 px（YearWordsPopup.qml:255/279/326/343）、色档 caption 8 px（:596）、环心色名 9 px（:555）、cellGap 1 px（:33）——DESIGN.md:302「不小于 11 px」；「365 天」10 px 挤在关闭钮旁（:186-203）。修法：小字提档或 hover tooltip 替代，「365 天」移入摘要行，格距 1→2 px。→ /impeccable typeset

### P3
7. 统计行「已会/新词」与词汇分段是断着的天然加速器（StatsPopup.qml:111-120 不可点，UI-SPEC 未接线）。
8. View 按钮主样式（UpdateCard.qml:346-351 ink 底与 Download 同级，流④）——「查看发行页」与「下载」同为 ink，决策权重不分。
9. tokens 无千分位（CostPopup.qml:135/141/147），246100 读起来费力。
10. 日均无分母（CostPopup.qml:150-156）：「¥0.08」不知除以几天；预算对照行（今日已用/上限）仍缺席。
11. 导出失败静默（WordsPopup.qml:414-428 注释自认）；空筛选段无文案。
12. spec 漂移两处：UI-SPEC.md:196 写「三个 15 px 线稿图标」实现四枚（导入无条目）；UI-SPEC.md:187 写 600 字重、11.5/12.5 px，实现 700/12/13 px（DESIGN.md:215 两档字重为准，spec 该改）。
13. Notice 不认 Esc（spec 4.0:40 有意划界——更新卡在名单、通知卡不在）；同契约读者心智略分裂，可接受，记录在案。
14. Segment 的 Grouping 自身无 name（Segment.qml:28），单元格有名有 checked，组语义（「筛选」）靠位置猜——顺手补一句即可。

## 6. Quick Wins（半天内可落）
- Notice action 胶囊补三件 role/name/onPressAction（对齐更新卡，P2-4）。
- 年度图例格补 activeFocusOnTab+Keys+FocusRing（role/name 已有，P2-3 的可速成半）。
- UI-SPEC.md:196 三枚改四枚、:187 字重字号对齐实现（纯文档，P3-12）。
- tokens 千分位（Format.js 一处）。

## 7. 画像红旗
- **Alex**：键盘全程可达+Esc 全通+分段箭头键，这轮从全盲变全通；仍缺统计行点入筛选、词表搜索、批量判定。
- **Sam**：控件层读屏名齐、焦点环遵循 DESIGN 规格；剩余盲区：年度卡格子层（P2-3）、「词汇」标题入口（P1-2）、Notice action（P2-4）；20 px 胶囊点击目标仍偏小（可 Tab 补偿）。
- **沉浸读者**：不变，这组最好——只在召唤时出现、复习绝不发请求、确认卡点外不抢答。
- **预算敏感者**：日均仍无分母、tokens 无千分位、预算对照行仍缺席（P3-9/10）。

## 8. 开放问题
1. 「历史累计」口径（PRODUCT 次数 vs UI-SPEC+实现 去重词数）ADR 何时钉死——两个 P1 挂在同一根因上。
2. 导入词汇是有意发布的功能吗？spec 漏写第三轮了。
3. 年度卡格子层键盘方案：格子入 tab 序（365 站不现实）还是摘要行给日期选择器？需要一次小设计决定。
