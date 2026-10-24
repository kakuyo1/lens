---
target: 设置浮层
total_score: 31
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
target_fingerprint: "sha256:4a76b3d02451f729ef5125b8ee21def13fcff6d55ac8e097ca7e04fb1a1acda8"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
timestamp: 2026-10-24T15-28-38Z
slug: src-app-qml-settings-settingspopup-qml
---
Method: design review only (detector not applicable, native platform)
# Assessment A — design-director review, `SettingsPopup.qml`（re-critique round 4）

运行方式：原生平台（Windows QML 桌面），Assessment B（detector / browser overlay）不适用，仅设计评审。证据为四张 2x 真机截图（目录、取词与弹窗、关于、通用滚动位）与源码逐条对照；浮层压在实时深色桌面上，评审只针对表面本身。本轮是 flow-③「令牌对表 (polish)」落地后的第 4 轮。

**1. Specificity verdict（特异性判定）**：为这个产品而写，不是类目通用件。悬浮设置标题即唤出读者自己的解释气泡（同一通道、同一套令牌、按当前字体 / 解释语言 / 打字速度重画），全部设置即时生效、无 Save 按钮，右注的 muted / faint 两档按「状态 vs 值」分义，OCR 与宠物门控各自带原因行——任何一块放进通用设置模板都活不下来。本轮对表（目录行与页面行共一条左缘、胶囊统一 27px、三个组标签统一 12px）在源码里是改在行组件与字段标签本身，不是散点补丁，方向和落法都对。

Deterministic scan: not applicable (native platform)。

**2. Design Health Score — 31/40（Good）**

| # | 启发式 | 分 | 关键问题 |
|---|---|---|---|
| 1 | 系统状态可见 | 3 | OCR 灰化的原因行在折叠线下，首屏只见灰不见因；正文滚动无指示（已排期） |
| 2 | 贴近真实世界 | 3 | 「token」「API key」裸奔无 inline 注解；热键框内「触发键」与节标签互相重复 |
| 3 | 用户控制 | 3 | Esc 阶梯（问题→子页→分类→关）完整；Defaults 即时且不可逆（flow ④ 排期） |
| 4 | 一致性 | 3 | flow-③ 三项对表已落地（左缘、27px 胶囊、12px 组标签）；返回键 muted / 关闭键 faint 两档、通用页双标签语法仍在 |
| 5 | 错误预防 | 3 | 热键冲突行、可用性门控、AA 校验不过即禁用 Apply；Defaults 无守卫（flow ④ 排期） |
| 6 | 识别而非回忆 | 4 | 悬浮预览渲染真实气泡；行右注即值 / 状态 |
| 7 | 灵活与效率 | 3 | 键盘语法齐全但死区（flow ① 排期）；目录无输入跳转 |
| 8 | 美学与极简 | 3 | 三阶文字梯度安静；学习页平铺、关于页中段大留白（后者规格如此） |
| 9 | 错误恢复 | 3 | OCR 状态逐因命名（缺英文数据 / 运行时不可用）；但原因与灰化控件不在同一屏 |
| 10 | 帮助与文档 | 3 | 预览卡是最好的上下文帮助；无人回答「API key 从哪来」 |
| **合计** | | **31/40** | Good |

持平（31）是诚实的读数：flow-③ 修掉的是 1–3px 级噪音，修得很干净，但剩余的分数几乎全部住在已排期的流里（滚动条、Defaults 守卫、键盘死区、可发现性）——polish 流本来就不该挣这些分。本轮唯一的实质性新债是第 1 / 9 条的「原因与灰化控件不同屏」。

**3. 认知负荷** — 3 项未过（moderate）：chunking（阅读学习页 11 个控件一列平铺）；grouping（塑造词卡的 7 个开关无名无界，取词页却为 3 个开关立了组）；working memory（灰化的 OCR 控件在上屏、解释它的那行在下屏，读者要自己把两屏连起来）。

**4. 总体印象**

这轮对表之后，设置浮层在「同一把尺子」这件事上达到了它四轮以来的最好状态：左缘一线贯穿页眉、目录、页面控件与页脚，胶囊家族一个高度，组标签一个字号，像素层面没有再能挑的散点。真正决定下一分涨不涨的已经不是对齐，而是两件事：一是取词页首屏那三个灰掉的控件至今不解释自己（原因行物理上存在，只是被排到了折叠线下）；二是已排期的四块大件（滚动指示、键盘死区、Defaults 守卫、可发现性）一天不落地，31 分就是这块浮层的天花板。最大的机会：把「为什么灰」搬到灰的地方去，这是一行代码的位置问题，却是这台 OCR 不可用的机器上整个首屏的观感问题。

**5. What's Working**

- **预览通道仍是这块表面的灵魂**：指针停在标题上，面板旁浮出的是读者自己的解释气泡——按他当前的主题、字体、解释语言、打字速度实时渲染，只靠 Preview 胶囊与卡外 6px 虚线框认领「这是演示」。它不是帮助文案，是把产品输出提前画给你看。
- **flow-③ 对表改在了组件里，不是补丁里**：目录行与链接行的 0 内缩写进 `SettingsCategoryRow` / `SettingsLinkRow` 的锚点注释，胶囊 27px 落在页脚 Defaults、「检查更新」、Apply custom colors 与 Chip 四处同高，取词页三个组标签统一 12px——四张截图里左缘、高度、字号肉眼已无法区分出任何一个例外。
- **行语法的默认正确**：`SettingsCategoryRow` / `SettingsLinkRow` / `Chip` 出厂自带 Tab 站、Enter / Space、`Accessible.role` + name、FocusRing、按压底色；note 的「muted 是状态、faint 是值」一格两义，诚实地复用了同一个 11px 槽位。

**6. Priority Issues**

- **[P1] OCR 灰化的「为什么」在折叠线下（新发现）**。取词页首屏：OCR、自动扫描（faint 灰化）与 OCR 触发热键（同样灰化）全部可见，而唯一解释它们的「OCR 运行时不可用 / 英文数据缺失」状态行排在「弹窗与剪贴板」链接行之后，首屏视口外。组件契约（SwitchRow / HotkeyField 头注「不说为什么」）与页面契约（UI-SPEC 4.4：探测中或缺失时开关不可操作**并显示对应状态**）各自成立，空间关系断了——在 OCR 不可用的机器上（截图即如此），首屏读作「这软件坏了」。**修复**：把状态行上移到触发通道组正下方（组容器与「动作条样式」之间）；或灰化时在组容器底部并一行原因，恢复可用时随组隐藏。**命令**：/impeccable layout。
- **[P1·已排期 flow ①] 键盘死区**：正文 Flickable 没有 UI-SPEC 4.4 点名的细滚动条（faint 滑块、透明轨道），Tab 焦点滚出视口不回带；页脚 Defaults 仍是 Rectangle + TapHandler——无 Tab 站、无焦点环、无 Accessible，违反 DESIGN「所有可点击元素有焦点态」。修复方案 round 3 已给，照做即可。**命令**：/impeccable harden。
- **[P2·已排期 flow ④] Defaults 一击全局重置、无确认**：~16 项设置即时改写，页脚状态行只确认既成事实；词汇删除已有两步确认先例。修复照 round 3：两步胶囊（按下翻成「确认」停留一拍）或快照 + 状态行「撤销」。**命令**：/impeccable clarify。
- **[P2] 阅读学习页 7 个开关无名分组（round-3 遗留、未排期）**：多义解释、词源、例句、音标、义项路标、搭配、图片共享同一个概念——词卡画什么——没有任何一行字说出来；姊妹页（取词）为 3 个开关立了组标签 + 容器，对照之下这页是噪音。**修复**：一排 11px muted 组标签（如「词卡内容」），只加标签不建容器，保住「触发通道是唯一分组容器」的规格。**命令**：/impeccable distill。
- **[P3] 标签语法小噪音（新发现，两处）**：其一，热键节标签「OCR 触发热键」与框内常驻左标签「触发键」（HotkeyField 的 qsTr("Trigger key")）互相重复——框内标签为与开关行同形而存在，但在本页有节标签，读作两遍；录制态「按下组合键」反而是必要信息。其二，通用页 Theme 标签 muted 12px 与 Backdrop refresh 粗体 13px 是同一种结构（标签 + 下拉）的两档写法，页面上没有可见理由解释为什么一个安静一个响。**修复**：热键框内左标签在有节标签时留空、仅录制态显示提示；通用页字段标签统一 muted 12px，粗体 13px 只留给真正的参数组标题。**命令**：/impeccable polish。

**7. Persona Red Flags**

- **Alex（急躁的高手）**：全浮层最富的信息——每张预览卡——绑定 hover，他的键盘永远够不到（flow ⑤ 已排期）；他最快的点击（恢复默认）是最破坏性的一个（flow ④）；OCR 不可用时，页面下半真正的解药——两个「指向你自己的 Tesseract」的路径框——在折叠线下，他得先滚过一整排改不了的热键和灰开关才能找到。
- **Sam（无障碍依赖）**：页脚 Defaults 无 Tab 站、对读屏不可见（flow ①）；滚动无指示、焦点出视口不回带；OCR 原因行是一个兄弟 Text，与三个灰化控件无任何程序关联——读屏用户听到的是三个可切换的开关和一段远处的解释，而不是「这三项因为 X 被禁用」；玻璃主题下 muted / faint 低于 AA（有记录的边界，仍是他的日常）。
- **Jordan（初访者）**：目录行摘要写得好（「限制单个本地日的模型花费」这样的句子让分类不点自明）；但一旦走到模型服务，「token」「API key」没有任何 inline 解释，他不知道 key 从哪来、token 是什么单位——这是全浮层唯一一处必须出门求助的地方。

**8. Minor Observations**

- 禁用态透明度硬编码 0.58（检查更新 busy、Apply custom colors 禁用两处），DESIGN 按钮规格写的是 45%——两处都不在令牌表里。
- SliderRow 值读数（「3 / 5」、玻璃参数「10 %」）是纯数字却用应用字体，DESIGN 要求纯数字走 Cascadia 等宽；拖动时读数宽度会抖。
- 目录末行在折叠处被拦腰截断（「桌面宠物」半行悬在页脚线上）——有了滚动条之后这不再是问题，此刻它是无指示滚动的唯一线索，也是最吵的一条。
- 「检查更新」27px 之后与页脚「恢复默认」、Chip「清除」完全同族，按压 0.97 + `Tokens.motion` 全家一致，像素层面无可挑剔。
- 热键 kbd 胶囊（Ctrl / Alt / S 三枚、19px、等宽 11px）与规格逐项吻合。

**9. Questions to Consider**

- 如果「OCR 不可用」是这台机器的常态，为什么解释它的那一行要等读者滚动才见面？——把「为什么灰」放进灰的三行里，成本是几像素的锚点，收益是整个首屏不再读作故障。
- 预览卡是产品最好的自我解释，却只回应指针；flow ① 落地键盘死区之后，Tab 走到「打字速度」的那一拍，预览卡该不该也升起来？
- Defaults 在 flow ④ 拿到两步确认之后，页脚那行 11px 状态字的职责是否应该从「确认」升级为「撤销」？——即时生效的设置哲学里，后悔药比确认书更符合它的气质。
