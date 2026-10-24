---
target: 批次① 托盘菜单（4.2）第 2 轮（修复流①后）
total_score: 30
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
target_fingerprint: "sha256:3cb4b21391b932cfa78255fb9e19bf3be51799ef62b2a718848882fca1d365ea"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
timestamp: 2026-10-24T11-39-06Z
slug: src-app-qml-panels-traymenu-qml
---
**Method: design review only (detector not applicable, native platform)** — Assessment A 由本子代理按主线 brief 执行（native QML 无 Assessment B；交付按 brief 以报告文件为载体、经主线转述）。证据：TrayMenu.qml / MenuRow.qml / FocusRing.qml 当前源码（工作树未提交态）+ SurfaceHost.qml 接线段 + app_controller.cpp:774 + UI-SPEC.md §4.0（含本轮新增「Esc 收面板」规则）/§4.2 + DESIGN.md + tray-light/dark.png（上轮真机截图沿用）+ focus-ring-switch/icon-button.png（FocusRing 组件在设置表面的渲染快照）+ qtest 三例全绿 / qml-lint 0 警告 / 四语 .ts 490/490 零未完成（主线提供，可信未重跑）。上轮 28/40，本轮重判：修复流①瞄准的 H3/H7 各进一分，未动发现如实保留，另新核实两处规格漂移。

# Lens 托盘菜单 · 设计评审（批次① 第 2 轮 · 修复流①后）

## 1. Design Health Score

| # | 启发式 | 分 | 关键发现 |
|---|---|---|---|
| 1 | 系统状态可见性 | 3 | 首行状态区一石三鸟；但预算耗尽时绿点照常亮（budgetExhausted 只到 readout 不进首行），托盘图标四态仍只映射 2 态 |
| 2 | 贴近真实世界 | 3 | 标签全平实、¥ 本币；「手动模式」命名的仍是扫描方式而非读者关心的事 |
| 3 | 用户控制与自由 | 3 ↑ | Esc 已通：应用级 + enabled 跟随 visible，不偷全局键（TrayMenu.qml:309-314）；点外关闭照旧；唯一残口是语言行激活无响应 |
| 4 | 一致性与标准 | 3 | 令牌/圆角/danger/等宽纪律保持；漂移仍在且本轮再核实两处（第二条分隔线零余量、死属性 anchors.margins） |
| 5 | 错误预防 | 3 | danger 退出、语言原地可逆、无确认符合托盘惯例 |
| 6 | 识别而非回忆 | 4 | 四项全可见带当前值；Accessible.name 现在连右侧数值一起念（MenuRow.qml:31） |
| 7 | 灵活与效率 | 3 ↑ | Tab/Enter/Space/Esc 控件层全通（qtest 证）；但入口激活链未验证、语言行 Enter 死路、子菜单不可聚焦 |
| 8 | 美学与极简 | 4 | 焦点环只在持焦时出现不加常驻噪音；分隔线与 ≡ 图标两处小噪音照旧 |
| 9 | 错误恢复 | 3 | 本表面无错误路径；最该报告的「预算已停」仍沉默 |
| 10 | 帮助与文档 | 1 | 行无 tooltip、模式名无释义、无帮助入口 |
| **合计** | | **30/40** | **Good 档中段（28–35）** |

认知负荷八项 0 失败，负荷低——「少」依旧干净；欠的仍是「诚实」与「通」的收尾。

## 2. 设计专属性判定

**半专属（与上轮同判）**：行堆叠骨架是通用形状；Lens 的三处不变——产品标当状态灯、行内今日数字、恒宽窗口左条留白。本轮新增（焦点环、Esc、读屏命名）是规格正确性而非身份表达，不改变判定。

## 3. 整体印象

修复流①把上轮「门没开」修成了「门开了、有一扇还焊着」：Esc 与三行的键盘/读屏链一次做对（Enter/Space 含 isAutoRepeat 防抖、name 连值念、四语零未完成），焦点环严守 DESIGN 的 2 px ink 偏移 2 px。剩下的不是品味问题：语言功能对键盘与读屏是死区（行宣布为按钮、激活却无响应），整条键盘链的生产入口（窗口激活）也没人验证过。**最大机会不变：首行第三态**——修复流②落地后这张卡才开始说真话。

## 4. 做得好

1. **MenuRow 四件套一次到位**：role Button、name 连右侧数值（统计行念「Statistics, 0 words · ¥0.00」）、onPressAction、Enter/Space 且 isAutoRepeat 不重触发（MenuRow.qml:29-39）；FocusRing 2 px ink 外描边偏移 2 px（FocusRing.qml:18-23，MenuRow.qml:92），行高 32 + 卡内距 6 容得下 4 px 外扩，顶行上扩恰好被 4 px spacer 接住，几何不裁切（组件渲染快照亦显示暗色下清晰可见）。
2. **Esc 的工程判断对**：ApplicationShortcut + enabled: menu.visible（TrayMenu.qml:309-314）——菜单不在屏时不占用全局 Esc；注释写明为何窗口级快捷键不可用，与 UI-SPEC §4.0 新规则及取词蒙版同款。
3. **上轮三好保持**：首行一石三鸟、今日统计行内联、恒宽几何与 hitRect 收边。

## 5. 优先问题

**[P1] 预算耗尽时，首行状态说谎（未动，属修复流②）** — 首行只读 selectionCapture（TrayMenu.qml:49、143、149）；budgetExhausted 已在 readout（app_controller.cpp:774，上轮核实 100/150 产出），托盘图标转琥珀、取词暂停，菜单照常绿点+模式名。纯托盘产品的唯一正门把故障说成正常。修法不变：首行加第三态（warn 琥珀 +「今日预算已用完」），顺带补全四态映射。→ /impeccable clarify

**[P1] 语言功能是键盘与读屏的唯一死区** — 修复流①让其余三行通了，语言行反而成了最扎眼的断点：MenuRow 宣布为 Button，Enter/Space/onPressAction 三条激活路都触发 picked()（MenuRow.qml:32-39），而 TrayMenu 的语言行没接 onPicked（TrayMenu.qml:168-184，只挂 hover）——键盘、读屏、鼠标点击在同一个信号上一起落空；子菜单四个 option 也没有 activeFocusOnTab/Keys（TrayMenu.qml:254-294），键盘无法换语言。无主窗口产品四分之一的正门功能对整类用户关闭。修法：语言行 onPicked 切换 listVisible 并移焦入列表；option 行补焦点链。→ /impeccable harden

**[P2] 键盘链的生产入口未验证** — placePanel 只 raise() 不激活（SurfaceHost.qml:231-235），Esc 注释自认「窗口从不成为 active」（TrayMenu.qml:306-308）；Tab 在真机上能否从托盘打开后到达行，qtest 证明不了（它直接 forceActiveFocus）。这是本轮新增修复的生效前提。修法：一次真机键盘实测（打开即按 Tab）；不通则 showTrayMenu 里补 requestActivate。→ /impeccable audit

**[P2] 状态行文案语义摇摆（未动）** — 开时显示模式名（设置常量）、关时才显示状态（人话），顺序反了（TrayMenu.qml:149）。状态优先写法（「自动 · 取词中」）待落。→ /impeccable clarify

**[P3] 规格↔实现漂移，本轮再核实两处** — 行字号 13 vs 规格 12.5（MenuRow.qml:77、TrayMenu.qml:279）；第二条分隔线零垂直余量、直接贴住上下两行（规格要求上下 5 px，TrayMenu.qml:194-200），其上 anchors.margins: 5 是死属性——无 anchors 可挂，像写了余量从未生效；两条分隔线宽仍不一（全宽 vs 内缩 2）；设置图标 15 px 下读作「≡」。→ /impeccable polish

**[P3] 子菜单选中态只靠颜色** — 当前语言行墨底白字（TrayMenu.qml:268-278），读屏只念「中文，button」无选中述明。需非视觉的当前态（name 后缀「(current)」或 Accessible 状态）。→ /impeccable harden

## 6. 画像红旗

- **Alex**：Tab/Esc 有了（大改善）；但 Tab 序走到 Language 按 Enter 无反应——一条中途死路；换语言全程无键盘路径。
- **Sam**：行级读屏命名是质变（值随名念、四语全译）；但语言功能三路皆断、子菜单选中无述明、窗口能否收键存疑。
- **预算敏感者**：¥0.00 首屏原则兑现好；预算耗尽那天菜单仍沉默（P1）。
- **沉浸读者**：Esc 应用级但 enabled 跟随 visible，阅读时不偷按键；菜单可接受焦点与兄弟表面不同（TrayMenu.qml:24 无 WindowDoesNotAcceptFocus），点击激活属菜单常态，抢焦点疑虑可放。

## 7. 小观察

选语言后瞬时消失与面板 cross-fade 不一致（未动）；「0 词 · ¥0.00」零态语言可商榷（未动）；玻璃下 11 px faint 不达 AA 是既有边界（未动）；MenuRow 点击不取焦点，鼠标点完续用键盘需再 Tab 一次；Esc 直接收整菜单含子菜单，合惯例；死属性 anchors.margins: 5 见 P3。

## 8. 开放问题

1. 窗口激活链（=P2）：真机从托盘打开即按 Tab，一次实测闭合——qtest 证明控件层，证不了窗口层。
2. 语言的键盘形态：Language 行 Enter 展开子菜单并移焦（合惯例），还是去掉子菜单循环切换？
3. 修复流②落地 budgetExhausted 时，托盘图标四态采样是否同批交 visual-qa 专项。

## 9. Quick wins

1. 语言行 onPicked: menu.listVisible = !menu.listVisible（一行，P1-2 鼠标/键盘/读屏死路同修）。
2. option 行加 activeFocusOnTab（四行，语言可键盘切换）。
3. 删死属性 anchors.margins: 5，给第二条分隔线上下各 5 px spacer，对齐规格。
4. 行字号 13 → 12.5，或改规格认 13——二选一写死。
5. 子菜单当前项 name 加「(current)」。

---

*一句话收束：锁芯装对了（Esc、Tab、读屏名全合规），但语言那扇还焊死着，且钥匙在真机上转不转得动没人试过；下一锤仍是首行那句真话（修复流②）。*

Questions skipped: 子代理按主线 brief 执行 re-critique 轮，开放问题以 §8 交回主线排期，不在子代理内交互发问。
