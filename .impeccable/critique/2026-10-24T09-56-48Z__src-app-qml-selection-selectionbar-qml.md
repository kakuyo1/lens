---
target: 批次③ 取词链路+杂项（4.10/更新卡/4.11/4.1/4.6）第 5 轮
total_score: 28
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 3
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\selection\\SelectionBar.qml"
target_fingerprint: "sha256:5c5a98b717084da71b8b4802db7e7423da1ad8ec10a3c02a3b195dc511e42537"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\selection\\SelectionBar.qml"
timestamp: 2026-10-24T09-56-48Z
slug: src-app-qml-selection-selectionbar-qml
---
**Method: design review only (detector not applicable, native platform)** — Assessment A 子代理独立评审，主线核实主要结论后交付（View/Download 同用 ink 实心 UpdateCard.qml:298/336、Cancel 0.58 透明度 :299、扇面标签未设字重默认 400、页边签位移不随 sideLeft 翻向 TabBody:152、tooltip 实为 QSystemTrayIcon::setToolTip tray.cpp:127——均成立）。证据：动作条五身体真机截图（真实 selection 流程驱动）+ 更新卡双主题 QTest 真平台快照 + SelectionBar/bodies 五件 + UpdateCard + Mask + tray.h/tray_state.h 源码 + UI-SPEC §4.0/4.1/4.5/4.6/4.10/4.11 + DESIGN.md + PRODUCT.md + ADR 0056。边界：蒙版运行态、托盘四态、tooltip 运行态、hover/按压态、句/实体/译文选区、玻璃主题、下载进行态未采样（转 qtest/visual-qa）。

# Lens 取词链路杂项组 · 设计评审（批次③ 第 5 轮）

## 1. Design Health Score

| # | 启发式 | 分 | 关键发现 |
|---|---|---|---|
| 1 | 系统状态可见性 | 3 | 更新卡状态机完整（进度+字节+ETA+失败三分）；但下载中 Cancel 以 0.58 透明度呈现读作禁用实则可按；就绪态不说「校验通过」 |
| 2 | 贴近真实世界 | 3 | 页签/手牌/吊牌/扇面纸品隐喻物理全部成立；「View」含义含糊；未签名安装缺一句安心话 |
| 3 | 用户控制与自由 | 3 | 点外关、蒙版 Esc+误按取消、close=暂不/skip=此版本语义分明；动作条与更新卡无 Esc |
| 4 | 一致性与标准 | 2 | 五身体共享令牌/玻璃/图标，但圆角离梯（8/10 vs 阶梯 9/11）、扇面标签 400 字重 vs 其余 700、图标 16 vs 15、页边签左侧变体位移方向不翻 |
| 5 | 错误预防 | 3 | 蒙版 <2px 按取消、抓屏前先隐藏自己；校验失败即删文件；未签名安装无提示语义 |
| 6 | 识别而非回忆 | 3 | 三动作永远图标+文字、五形态同一套标签；通道判定刻意隐藏（规格决定） |
| 7 | 灵活与效率 | 3 | 拖选→单击→气泡已是最短鼠标路径；形态可选、拖动跟随；零键盘加速器、误拖后无自动消失 |
| 8 | 美学与极简 | 3 | 五身体同一材质语言；但扇面 720ms 入场是全应用最长动效且发生在阅读焦点旁 |
| 9 | 错误恢复 | 3 | 下载失败按原因三分各给修法、坏文件即删；OCR 无结果出通知（运行态未验证） |
| 10 | 帮助与文档 | 2 | 蒙版提示行「拖动框选 · Esc 取消」开拖即隐是全组范本；五形态/成本语义/SHA256 语义无就地说明 |
| **合计** | | **28/40** | **Good（基础扎实，修补弱项）** |

## 2. 设计专属性判定

**authored——同一套产品语言的五种变奏，不是五个杂牌。** 五身体共用同一动作清单、同一组图标、同一公式「muted 15px 线稿图标 + 700 字重标签 + panel 底 + line 描边 + 卡片阴影」；隐喻家族自洽于「纸品」世界（书签书脊折角/手牌/吊牌孔位细线/扇面/长条），与北极星「安静的书页边注」同源；落点各自服从物理（吊牌孔位贴松手点、扇与手牌以松手点为轴摊开）。蒙版刻意不进主题令牌是有理由的例外（注释钉在源头）。**出戏点**：扇面标签 400 字重独自偏离字体系统；更新卡「View」实心墨底是全组最通用 SaaS 的一笔。

## 3. 整体印象

Lens 里完成度最高的一组表面。五种身体不是噱头——骨架真的一份，形态切换读者不会觉得换了应用；吊牌签「线端那颗点就是你的松手点」是对「这条为什么出现在这」最诚实的回答。代价在纪律的最后一公里：字重、圆角、图标尺寸、位移方向各身体各说各话，DESIGN.md 自己立的 Ladder 和 Two-Weights 规则在这组文件里没有被执行。**最大机会**：一次对表 DESIGN.md 的全组 polish（字重归 700、圆角归梯、图标归 15）就能把「五个好形体」收拢成「一件产品的五张纸」；外加更新卡把「SHA256 校验通过」写上去——那是全链路唯一读者把手交给可执行文件的时刻。

## 4. 做得好

1. **吊牌签把锚点讲成物理**（bar-tag.png）：孔位贴松手点、16px 细线、入场阻尼摆动、静止 1.4° 微倾、hover 收到 0.4°——形态本身回答「为什么在这」。
2. **经典长条是「安静边注」基准面**（bar-bar.png）：图标+文字、无装饰、不抢焦点（DoesNotAcceptFocus——点击动作后读者应用仍在前面）。
3. **更新卡的失败语义**（update-card-light/dark.png）：失败按原因三分各带修法、checksum 失败明说「已删除」、无 total 诚实省掉进度条、暗色主按钮反转严格执行。

## 5. 优先问题（主线核实后维持）

**[P1] 更新卡按钮层级：View 与 Download 同穿主色，Cancel 假装禁用** — View 与 Download/Install 都是 ink 实心（UpdateCard.qml:298/336），两个主按钮并排；下载中 Cancel 0.58 透明度（:299）既非禁用 45% 也非可用态。修法：View 改次级样式，ink 只留给 Download/Install/Cancel；下载中 Cancel 全透明度以文案区分。→ /impeccable layout

**[P1] 无障碍全组盲区** — 三个动作与更新卡三按钮全是 Rectangle+TapHandler，grep 全组零处 Accessible；窗口 DoesNotAcceptFocus 无焦点环；更新卡无 Esc 无 Tab。修法：更新卡三按钮 Accessible.role/name + Tab + Esc；动作条按契约保持指针优先（蒙版 Esc 已是先例）。→ /impeccable harden

**[P1] 五身体一致性漂移：规格自己的规则在自己家里失守** — 圆角离梯（页边签项 8 vs 规格 9、手牌卡 10 vs 阶梯 11、吊牌 12 随规格但也离梯）；扇面标签未设字重（默认 400）+ 图标 16px vs 其余 700+15px；页边签左侧变体 hover/按压位移恒 +x 不随 sideLeft 翻向（TabBody:152）。修法：扇面 700+15px；圆角归梯或修订阶梯记 ADR；位移乘 sideLeft 符号。→ /impeccable polish

**[P2] 安装时刻缺一句安心话** — 就绪态「The installer is ready to run.」吞掉了校验通过这一事实；未签名语义只活在文档。修法：「Checksum verified. The installer is ready to run.」。→ /impeccable clarify

**[P2] 手牌签按压与规格不符 + 入场动效超重** — 规格写「按下牌上抛淡出」，实现 opacity 直置 0 无动画（牌是消失不是抛出，转 qtest 确认观感）；扇面入场 pop×4=720ms 全应用最长且在阅读焦点旁。修法：手牌补上抛位移+淡出；扇面收到 pop×2 以内。→ /impeccable animate

## 6. 画像红旗

- **Alex**：拖选→单击→气泡最短路径、落点跟随重定位——效率合格。红旗：零键盘且无静置自动收，误拖税一天几十次。
- **Sam**：取词链路鼠标 only（拖选无法键盘触发，产品边界可接受）；更新卡 Install 键盘不可达是纯实现缺口；蒙版 Esc 是亮点。
- **沉浸读者**：出现近乎完美；红旗在消失与动效：无静置收、扇面 720ms 在视线旁展开（转 qtest 实测）。
- **预算敏感者**：三动作同权、花钱与否零传达（规格刻意）——标注为已知取舍；若要补，气泡比条更合适。

## 7. 小观察

**托盘 tooltip 规格与实现矛盾**：§4.6 描绘 #2a2a30 底 11px 圆角 7px 带箭头的定制卡，实现只是 QSystemTrayIcon::setToolTip（tray.cpp:127），外观由 Windows 决定——规格承诺着一个不存在的外观（要么改规格要么做真卡，见开放问题 3）。托盘图标状态机正确（Budget>Busy>Auto/Off、午夜轮询防琥珀过夜），taskbarIsDark 用系统 colorScheme 近似任务栏深浅可能选错变体（未验证）。§4.0 更新卡行滞后（未列 Download/Cancel/Install）。View 会顺手收卡（与「安装成功前不收起」承诺打对折）。扇面三片明暗差偷说「解释是主项」（三动作等权）。按钮高 26 vs 确认卡 27 两档。手牌 hover 转向与其他身体相反（无背板所需可接受，玻璃待验）。

## 8. 开放问题

1. 动作条要不要静置自动消失（6–10 秒）？误拖税 vs「它会不会自己跑掉」的不确定感——值得 ADR。
2. 扇面三片要不要同色、只靠 1px 扇骨线分界？现在的明暗差物理成立却偷说层级。
3. §4.6 定制 tooltip 是目标还是遗迹？做真卡是新表面；不做就改回「系统 tooltip」一句话——现在规格承诺着不存在的外观。

---

*未采样：蒙版运行态、tooltip 运行态、托盘四态、hover/按压、句/实体/译文选区、玻璃、下载进行态。*
