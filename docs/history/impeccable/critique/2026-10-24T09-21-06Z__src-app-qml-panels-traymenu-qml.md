---
target: 批次① 托盘菜单（4.2）第 3 轮
total_score: 28
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
target_fingerprint: "sha256:925ba8e530b7716f65454624c3ba81be110e38001b56d6c54132e78e53f8e873"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
timestamp: 2026-10-24T09-21-06Z
slug: src-app-qml-panels-traymenu-qml
---
**Method: design review only (detector not applicable, native platform)** — Assessment A 子代理独立评审，主线核实两条载重结论后交付（均成立：TrayMenu.qml 无 budget/exhausted 读取，grep 证实；语言行无 onPicked，stats/settings/quit 三行都有唯独它没有）。证据：tray-light.png / tray-dark.png 真机截图 + TrayMenu.qml/MenuRow.qml 源码 + SurfaceHost 相关段 + UI-SPEC §4.1/4.2 + DESIGN.md + PRODUCT.md。边界：语言子菜单、托盘图标四态、hover 态未采样（转 qtest/visual-qa）；菜单挂 scratch 空数据（0 词 · ¥0.00）。

# Lens 托盘菜单 · 设计评审（批次① 第 3 轮）

## 1. Design Health Score

| # | 启发式 | 分 | 关键发现 |
|---|---|---|---|
| 1 | 系统状态可见性 | 3 | 首行状态区承担产品态+今日数字，好设计；但预算耗尽时绿点照常亮，托盘图标四态只映射进 2 态 |
| 2 | 贴近真实世界 | 3 | 标签全平实、¥ 本币；「手动模式」命名的是扫描方式，不是读者关心的事 |
| 3 | 用户控制与自由 | 2 | 点外关闭已实现且 hitRect 算得准；但无 Esc（兄弟面板都有），「语言」行点击无任何响应 |
| 4 | 一致性与标准 | 3 | 令牌/圆角/danger/等宽数字守纪律；漂移：行字号 13 vs 规格 12.5、两条分隔线宽不一 |
| 5 | 错误预防 | 3 | 退出按规格用 danger；语言即时生效原地可逆；无确认符合托盘惯例 |
| 6 | 识别而非回忆 | 4 | 四项全可见、右侧带当前值与今日数字，零记忆负担 |
| 7 | 灵活与效率 | 2 | 行内数字省一步、hover 子菜单快；全菜单零键盘路径 |
| 8 | 美学与极简 | 4 | 每个元素都有岗位，层级清晰；仅分隔线与 ≡ 图标两处小噪音 |
| 9 | 错误恢复 | 3 | 本表面无错误路径，误操作均可逆；最该报告的「预算已停」反而沉默 |
| 10 | 帮助与文档 | 1 | 行无 tooltip、模式名无释义、无帮助入口；只有托盘 tooltip 在门外帮忙 |
| **合计** | | **28/40** | **Good 档下沿** |

认知负荷八项 0 项失败，负荷低——这张卡在「少」上干净，问题全在「通」上。

## 2. 设计专属性判定

**半专属：骨架是模板，三处是 Lens。** 统计/语言/设置/退出的行堆叠是通用形状；但：① 产品标当状态灯（lens.svg 同几何按 ok/faint 染色，品牌记号与在岗状态合并，规格只要求匿名圆点，实现越得对）；② 行内今日数字（「成本由读者掌控」翻译成一行像素）；③ 窗口恒宽的左条留白（为子菜单预留、贴边收拢一次算对）。首行承载「常驻伙伴在岗」目前勉强——文案是功能设置的语言，不是伙伴的语言。

## 3. 整体印象

照着自家规格一丝不苟做出来的卡——令牌、圆角、字重、danger、等宽混排全对，浅暗两套都立得住；不做的动效也做得对。**最大机会：把首行从「两态装饰」升格为「真状态机」**——托盘图标在 C++ 侧知道四件事（TrayReadout），菜单首行只知道两件，于是最需要被告诉「为什么没弹词」的那一刻，这扇门是绿的。

## 4. 做得好

1. **首行状态区一石三鸟**：产品标即状态灯、状态即品牌、图标列对齐；取词关闭时降灰标+明确文案，状态有文字冗余不靠颜色单独传达。
2. **今日统计行内联**：不开面板就回答「今天学了没、花了没」；MixedText 等宽混排；¥0.00 本身就是安抚。
3. **最容易做错的几何做对了**：恒宽窗口、子菜单展开贴边收拢一次算对、hitRect 计入子菜单排除阴影边距。

## 5. 优先问题（主线核实后维持）

**[P1] 预算耗尽时，首行状态说谎** — 首行只读 selectionCapture 与 modeLabel；预算到顶时 CaptureDuty 暂停全部取词、托盘图标转琥珀，菜单照常绿点+「手动模式」。纯托盘产品里这是唯一正门：故障被说成正常。修法：Controller 已产出 budgetExhausted（app_controller.cpp:100/150/774），首行加第三态（warn 琥珀 +「今日预算已用完」），顺带把四态语义映射完整。→ /impeccable clarify

**[P1] 键盘与读屏用户进不了这扇门** — TrayMenu/MenuRow 无 activeFocus/Keys/Accessible（grep 证实）、无 Esc；项目明明会做（PictureStrip 有完整三件套、三个兄弟面板都有 Escape）。无主窗口产品的全部入口，键盘用户 100% 断路。修法：MenuRow 焦点环 + Tab/方向键 + Enter/Space + Accessible.role；窗口级 Escape。→ /impeccable harden

**[P2] 「语言」行点击无响应** — 子菜单只挂 hover（TrayMenu.qml:176-183），MenuRow 的 picked 没接；同一条菜单三行点了有反应、一行点了没有。修法：语言行 onPicked → 切换 listVisible，点击即开再点即收。→ /impeccable harden

**[P2] 状态行文案语义摇摆** — 开时显示模式名（设置常量），关时才显示状态（人话）——顺序反了。修法：状态优先写法（「自动 · 取词中」「取词已关」「预算已用完」），模式名并入 tooltip。→ /impeccable clarify

**[P3] 规格↔实现细碎漂移** — 行字号 13 vs 规格 12.5；两条分隔线收边不一；设置图标 15px 下读作「≡」。→ /impeccable polish

## 6. 画像红旗

- **Alex**：步数合格（设置 2 步、语言 3 步）；零键盘加速器；点「语言」零反馈；无 Esc。每天开十次的前门没有一个快键。
- **Sam**：窗口存在但整条交互链键盘不可达；读屏大概率读成散落文本（源码无角色声明，未实测）。
- **沉浸读者**：出入安静；未验证隐患：Qt.Tool 弹出是否抢焦点——若抢，每次开菜单都在打断阅读流。
- **预算敏感者**：¥0.00 首屏、花费 3 步，原则兑现好；红旗：预算耗尽那天菜单保持沉默。

## 7. 小观察

选语言后瞬时消失与面板 cross-fade 不一致；「0 词 · ¥0.00」零态可商榷（账目语言 vs「今天还没开始」，取其一写进规格）；子菜单墨底白字 vs Windows radio 惯例（保留成立，双标多余）；四语言目录与界面语言同表（加语言不改两处，工程好决定）；玻璃下 11px faint 注记不达 AA 是既有边界（ADR 0035）但 ¥ 恰是最不该糊的一行；contentActive 首开才加载，常驻成本压住了。

## 8. 开放问题

1. 键盘链从哪一环断的：C++ 侧托盘图标键盘激活是否映射到 showTrayMenu？（转 qtest 专项）
2. 菜单会不会偷焦点（Qt.Tool 未声明 WindowDoesNotAcceptFocus）？沉浸优先产品值得一次实测；若抢，比任何视觉问题都优先。
3. 状态行刷新时机：预算中途到顶的瞬间，菜单数字与托盘图标会不会各刷各的、留下绿色窗口期？（未验证）

---

*一句话收束：把 DESIGN.md 的纪律执行到了行级；欠的不是品味，是诚实（预算耗尽绿点亮着）和开门（键盘与读屏用户在门外）。*
