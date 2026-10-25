---
target: 批次② 解释气泡（4.3）第 2 轮（修复流①后）
total_score: 27
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 1
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\bubble\\Bubble.qml"
target_fingerprint: "sha256:3df03703c13532b5934ee5a8ef4eb9cab1441384ccc03618603fae5526c35403"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\bubble\\Bubble.qml"
timestamp: 2026-10-24T11-14-43Z
slug: src-app-qml-bubble-bubble-qml
---
**Method: design review only（原生 QML 表面，检测器/浏览器证据不适用）。** Assessment A 子代理独立评审（re-critique，同表面第 3 份报告）。证据：沿用 %TEMP%\lens-critique\bubble-snap\ 的 13 张真平台快照（本轮未新摄）+ 修复流①未提交变更集的 git diff + src/app/qml/bubble/ 当前源码（Bubble.qml 未动，指纹与上轮一致——本轮改动全在卫星件）+ ChainText / SenseRows / PictureStrip / IconButton / Segment / FocusRing + UI-SPEC.md §4.3（新增「无焦点表面」条目，:74）+ DESIGN.md。边界：探针用例 tst_bubble.qml:200-219 以代码审读核证、未运行（本轮约束禁运行应用），「通过」为修复流①自述；句/实体/译文三型未采样；玻璃主题未渲染；浅色 muted/faint 精确对比度未实测。

# Lens 解释气泡 · 设计评审（批次② 第 2 轮 · 修复流①后）

## 1. Design Health Score

| # | 启发式 | 分 | 上轮 | 关键发现 |
|---|---|---|---|---|
| 1 | 系统状态可见性 | 3 | 3 | 倒计时、在途行、失败行齐备；hover 停表仍无视觉暗示 |
| 2 | 贴近真实世界 | 3 | 3 | 义项无编号无词性，未动 |
| 3 | 用户控制与自由 | 3 | 3 | 读屏出口已建（具名+按压）；限时无「钉住」，且停表只认指针在场——读屏浏览模式无人停表（P2-G） |
| 4 | 一致性与标准 | 3 | 3 | 「New」一词三现未动；新增：共享控件在无焦点表面内声明 Tab 站/焦点环，与 4.3 字面相抵（惰性，P3-H） |
| 5 | 错误预防 | 3 | 3 | 防环、在途防重、译文卡藏语言切换，未动 |
| 6 | 识别而非回忆 | 3 | 3 | 判定行指针面仍 hover 才在场；读屏树内此门不存在——探针在夹具未设 hovering 下仍找到 Known/New |
| 7 | 灵活与效率 | 3 | **2↑** | 读屏浏览模式成为第二条完整路径并被探针锁定；视觉键盘路径以无焦点契约显式放弃（不再是结构缺席） |
| 8 | 美学与极简 | 3 | 3 | 默认态仍是边注；全开关高度失控未动 |
| 9 | 错误恢复 | 2 | 2 | 图片失败态两行灰字同权重、无原因，未动 |
| 10 | 帮助与文档 | 1 | 1 | 卡上零教学，未动 |
| **合计** | | **27/40** | 26 | **Acceptable（20–27 档顶格）** |

认知负荷八项：七项过；视觉层级半过（正文落 muted 档，同 P1-C）。低负荷，与上轮同。情绪旅程弹出/阅读/消失三段安静成立，未变。

## 2. 设计专属性判定

（承上轮）专属性集中在第二层与元信息：义项路标、下钻链双栈边、hover 判定行、等宽 ~ 脚注、「Disappears in Ns」元信息行；默认卡的脸仍近通用词典弹窗，bubble.png 单看认不出 Lens。

本轮新增的一笔属于交互语法而非视觉：**「无焦点表面」本身成了产品专属契约**——为沉浸取词放弃焦点，用读屏命名换回可达（UI-SPEC.md:74 把取舍写成了字），零视觉噪音。判定行的 hover 门控在读屏树里不解锁也不设墙，是这套「边注语法」的自洽延伸。

## 3. 整体印象

上轮两大 P1 其一（键盘与读屏路径）以最克制的方式落地：不加 Tab 站、不加焦点环、不加 Esc，代价为零视觉噪音，换来读屏浏览模式全具名可按，且契约与窗口 flag 都进了规格与探针。另一 P1（玻璃正文 2.33:1）按计划留给流⑥。修复本身几乎无可挑剔；它真正暴露的是下一层问题——**时钟不认识读屏的在场**（P2-G）。

**最大机会不变**：正文提档（安静与可读的显式交易，流⑥）。**次级新机会**：让停表认识读屏在场。

## 4. 做得好的

1. **读屏路径以「契约而非补丁」落地**：五类可按件全部具名可按——判定胶囊（BubbleVerdictRow.qml:77-79/109-111）、关闭（BubbleHeaderActions.qml:126）、语言切换（:143-145，名「Explanation language」）、链中可下钻的词（ChainText.qml:99-103，链词本身为名）、链内返回（BubbleChainPanel.qml:140）；名称全部走 qsTr 随界面语言本地化；契约写进 UI-SPEC.md:74，窗口 flag 被探针断言锁住（tst_bubble.qml:212-213）。判定行不因 hover 门控从读屏树消失——读屏用户无需指针即可判定。
2. **（承上轮）状态可见性是系统工程**：停表条件五条、tick 粒 100ms、下钻防环（maxSteps=3 + trail 去重）、失败一行灰字不弹通知。
3. **（承上轮）下钻链的卡内运动与双主题真同构**：面包屑+返回+双栈边；暗夜是按 155° 重调的阶，不是调暗。

## 5. 优先问题

**[P1-C] 玻璃主题下气泡正文仍是全应用唯一「天生不达标」的内容**（承上轮，未动，属修复流⑥）— 实测雾面玻璃次要字 2.33:1（达标像素 3.7%）、弱字 1.65:1（0%）；气泡释义/词源/例句按设计全部落在 muted/faint 档（BubbleCard.qml:259/335/374），其他表面正文是 text 档。修法：玻璃模式下正文提一档、元信息保持 faint，走一次 ADR。→ /impeccable colorize

**[P2-G]（修复流①新暴露）停表只认指针在场，读屏用户的卡会在阅读中消失** — updateCountdown 只认 hovering/dragging/revealing/countsDown/surfaceShown（Bubble.qml:273-278）；读屏浏览模式不产生 hover，默认显示时间到点即收（Bubble.qml:280-295），刚建好的读屏路径被时钟削短。修法：读屏按压（onPressAction）发生时重灌 remainingMs 或短停一程；彻底版是认「读屏活跃」为停表条件第六条。→ /impeccable harden

**[P2-D] 图片失败态的恢复动作没有按钮相，且无原因**（承上轮）— 失败行与恢复动作同字号同色同权重（快照 bubble-pictures-failed-light.png 实证：两行纯灰说明字），不区分网络失败/无结果/限流。修法：恢复动作提为 muted 并保留胶囊底；文案区分原因。→ /impeccable clarify

**[P2-E] 学习闭环（Known/New）是隐藏功能，零在场教学**（承上轮；读屏面已缓解）— 判定行 hover 才展开（BubbleVerdictRow.qml:26），指针用户发现率仍堪忧。修法：首次 N 张卡加一次性提示，或设置预览卡演示展开态。→ /impeccable onboard

**[P2-F] qtest 气泡夹具双语言同显，违反生产单语言不变量**（承上轮）— opened() 仍同时设 en 与 zh（tst_bubble.qml:44-45），生产恒单语言（explanation_duty 过滤）。修法：夹具镜像生产载荷形状，并加不变量断言。→ /impeccable harden（测试侧）

**[P3-H] 共享控件把 Tab 站与焦点环声明带进了无焦点表面，与 4.3 字面相抵** — IconButton 无条件 activeFocusOnTab: enabled 并挂 FocusRing（IconButton.qml:29/41；气泡的关闭与链内返回都用它），Segment activeFocusOnTab（Segment.qml:27，语言条），Pictures 条与义项图入口同样（PictureStrip.qml:112/263、SenseRows.qml:171）。窗口 flag（Bubble.qml:28）挡住焦点遍历，当前全部惰性、无视觉后果；但「不带 Tab 站」的契约单点依赖窗口 flag，探针未断言 Tab 站缺席。修法：气泡内实例显式 activeFocusOnTab: false（或共享控件加 focusable 参数），探针补零 Tab 站断言。→ /impeccable harden

**[P3-I] 探针的两个覆盖盲区** — 夹具无 spans/englishSpans（tst_bubble.qml:40-49），ChainText 的具名路径（ChainText.qml:99-103）未被断言覆盖；「无一无名可按件」只扫 Button 角色（tst_bubble.qml:205-210），语言条收起时的 RadioButton 单元与点按收条用的无名 scrim（BubbleHeaderActions.qml:63-67）不在口径内——均有具名或替代出口、非实际障碍，但断言口径窄于其名。修法：补一条带 spans 的载荷用例。→ /impeccable harden（测试侧）

## 6. 画像红旗

- **Alex**：视觉键盘路径仍为零——但已从「结构缺席」变为「写进规格的取舍」（UI-SPEC.md:74）；ZH 切换 2 击未变。
- **Sam**：大幅改善——浏览模式全具名可按、判定无需 hover、名称随界面语言本地化；仍在：停表不认他的在场（P2-G，卡可能读到一半消失）、玻璃正文 2.33:1（P1-C）；字号可配置、状态非仅颜色、暗夜全过 AA 是既有加分。
- **沉浸读者**：三段安静未动；全开关信息密度越线（pictures 态 8 个内容块）未采样未验证。
- **预算敏感者**：下钻无成本语境的显式取舍未动，建议在 ADR 里认下来。

## 7. 小观察

语言条收起时其 RadioButton 单元不在读屏树（visible:false 即出树），开启靠「Explanation language」按压、闭环成立，但开启瞬间无任何读屏通告；「ZH」视觉上仍不点名自己是语言切换（具名只救读屏不救指针，快照复核仍在）；pictures-failed 两主题截断点不同（上轮判快照时机，仍待 qtest 复核）；倒计时 hairline 替代方案（上轮）仍立。

## 8. 开放问题

1. 停表要不要认识「读屏在场」？P2-G 的最小版（按压即重灌一程）与完整版（读屏活跃即停，停表条件第六条）之间，哪档配得上「沉浸优先」？
2. （承上轮，流⑥）玻璃正文提档的代价怎么裁？muted→text 损失多少「安静」vs 2.33:1 的不可读。
3. （承上轮）卡上要不要成本语境？下钻计费零提示是取舍还是疏漏，建议 ADR 认下。

---

*未验证项汇总：句/实体/译文三型、RevealText/hover 动态、玻璃主题渲染、浅色 muted/faint 精确对比度、18px 字号态高度、真实屏幕阅读器（NVDA/UIA）会话。*
