---
target: 批次① 设置浮层（4.4）第 2 轮（修复流①后）
total_score: 31
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
target_fingerprint: "sha256:b7806aea9eb3836281cce35cf80f716243ace6b510b497fc43af43324c51515d"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
timestamp: 2026-10-24T11-28-58Z
slug: src-app-qml-settings-settingspopup-qml
---
**Method: design review only (detector not applicable, native QML platform)** — Assessment A（native 单人法：无 B 评估、无降级）。子代理重跑轮：Ask-the-User 问题流程由主线程批次计划持有，本报告以清单收尾、不设提问。证据：14 张真机截图沿用（像素未变的态沿用作视觉证据，`%TEMP%\lens-critique\01–14`）+ 焦点环新增快照四张（focus-ring-switch / focus-ring-icon-button 及 zoom-*：放大图核对得 2px ink 描边、外偏 2px 空隙、圆角随控件外形，与 DESIGN.md:262 及 FocusRing.qml:18–23 一致）+ 工作树全量源码（SettingsPopup、目录/链接/分类行、pages 11 页、controls 12 件）+ UI-SPEC.md 4.4/4.0 现行版 + DESIGN.md + PRODUCT.md。未运行应用、未驱动 dev surface、未注入鼠标；修复流①自述的验证证据（qml-lint 0 警告、15 个新 qtest 用例全绿、四语 490/490）经源码逐条对名核实（tst_switch.qml:63/80/93/108、tst_segment.qml:70/93、tst_dropdownfield.qml:479/506/518、tst_iconbutton.qml:47/63/70/82、tst_menurow.qml:63/81、tst_statrow.qml:81/101 全部在册），未重跑。

---

# Lens 设置浮层 · 设计评审报告（第 2 轮 · 修复流①后）

## 1. Design Health Score

| # | 启发式 | 分 | 关键发现 |
|---|---|---|---|
| 1 | 系统状态可见性 | 3 | OCR 三态、更新五态、同步七种错误完整；「Saved / Defaults restored」仍走最弱通道（faint 11px，SettingsPopup.qml:483–494），禁用行仍不说明原因 |
| 2 | 贴近真实世界 | 3 | 档位、主题名是读者的语言；「Set 0 to pause the cap.」「每 1000000 个 token」原样未动 |
| 3 | 用户控制与自由 | 3 | Esc 分层退出不变、且升格为 4.0 全局规则（UI-SPEC.md:40）；「恢复默认」仍一击即中、无确认无撤销 |
| 4 | 一致性与标准 | 3 | 四件套共享控件行为完全统一（一个 FocusRing、一套键处理、一套命名语法）；滑块/路径/四枚裸按钮掉出同一模式，DESIGN.md:292 仍未全兑现 |
| 5 | 错误预防 | 3 | 预算 validator、热键冲突拒绝、AA 门禁不变；全局 Defaults 仍裸奔 |
| 6 | 识别而非回忆 | 4 | 悬停预览体系原样成立；读屏名称层新增（开关取行标签、下拉取设置名）把「识别」延伸到无鼠标读者——预览本体仍 hover-only |
| 7 | 灵活与效率 | 3（上轮 2） | 全面板 Tab 路径成立：目录/链接/开关/分段/下拉字段/输入框全可达，Enter/Space 防自动重复、分段方向键不回绕；纯选择下拉可开不能选、滑块/路径/热键仍指针-only，压住第 4 分 |
| 8 | 美学与极简 | 3 | 焦点环 2px ink 干净融入灰阶语言，未添色相与字重；滚动内容仍被页脚硬切 |
| 9 | 错误恢复 | 3 | 同步错误与宠物包拒绝文案范本不变；API key 填错仍要绕经取词失败才发现 |
| 10 | 帮助与文档 | 3 | 就地说明覆盖高危点不变；「打字速度」等术语仍无注释，字体页帮助句只写了 Hover 一条路（SettingsFontPage.qml:107） |
| **合计** | | **31/40** | **Good（28–35：地基扎实，补弱区即可）** |

上一轮 30/40 → 本轮 **31/40**（+1，全部来自启发式 7：键盘路径从无到有）。未修复处的分数如实保留。

**认知负荷八项**（未动，同上轮）：过 5 项——分组、视觉层级、一次一事、工作记忆、渐进披露（四处条件显隐仍全部有据）；败 3 项——单一焦点与分块 ≤4（扩展页全展开时九块并存、40 动作 chip + 60+ 配饰 chip 平铺，SettingsExtensionsPage.qml:406–499）、最少选择 ≤4（目录 7 项，SettingsCatalog.qml:17–51；阅读学习页 10 行连排）。**3 项失败 = 中等负荷**，仍集中在扩展页与长开关列，主流程页（通用/模型/预算）干净。

## 2. 设计专属性判定

**结论不变：这套设置只可能是 Lens——肌肉专属、骨架中性。本轮新增第三层证据：键盘模型也是写出来的，不是套出来的。**

- 修复流①不是给控件撒 `activeFocusOnTab`，而是立了一套语法并写进规格（UI-SPEC.md:107）：开关取行标签为名、下拉取左侧设置名、图标按钮由调用点命名、分段是单 Tab 站的 RadioButton 组、首尾不回绕。SwitchRow.qml:57 一行 `accessibleName: root.label` 让 15 处调用点全部自动得名；12 个下拉调用点（General 2、Learning 3、Font 2、Capture 2、Model 2、Extensions 1）逐一核对全部传名。命名跟着「行 = 标签 + 控件」的产品结构走，不跟控件走——这是把行文法直接编译进了无障碍层。
- FocusRing.qml 是小而准的工艺：外扩 4px（2px 偏移 + 2px 描边），调用方按「控件自身圆角 + 4」传半径保持曲线平行（FocusRing.qml:8–11）；放大快照里胶囊（开关）与圆角矩形（图标按钮）的环都贴合。焦点态未引入新色相、新字重——修复本身遵守 One Green Rule 与字重两档。
- 悬停预览体系、主题命名、宠物人格渗进表单、「页脚没有 Save」——上轮全部专属证据原样成立（相关源码未动）。

通用化的一面也不变：目录层、预算页、API 配置页仍是灰白卡骨架，「钱与连接」依旧没有 Lens 指纹，产品指纹最重的资源（预览）与最需要它的页面（预算）依旧断开。

## 3. 整体印象

上一轮这块面板是「鼠标独享的有想法」；这一轮它对键盘读者开门了：Tab 能从目录一路走到任何一页的开关、分段与输入框，Enter/Space 触发，焦点环干净得像本来就在设计语言里。四件套一次立齐、一套语法、一批测试托底——工程上漂亮的修复流。

但门开到七成：滑块整类、两枚路径字段、热键行、四枚裸按钮、纯选择下拉的「选」这个动作，仍只认指针。Tab 走到打字速度就掉进死区；键盘能打开主题列表，却挑不了主题。**最大机会：走完最后一程（尤其下拉的键盘移选），再把「预览思维」从解释气泡延伸到钱——预算页的今日已花。**

## 4. 做得好的

1. **键盘层按语法一次立起**（Switch.qml:72–86、IconButton.qml:29–41、Chip.qml:39–49、Segment.qml:27–41 与 65–71、DropdownField.qml:83–95）：role + name + onPressAction，Enter/Space/Return 防自动重复，分段单站方向键不回绕，可输入下拉 input 为唯一 Tab 站且描边转 ink（DropdownField.qml:370）；15 个新 qtest 用例与像素级焦点环核对托底，未动的设计语言毫发无损。
2. **悬停预览体系（启发式 6 的 4 分保持者）**：七个 SwitchRow 预览 + 四类选项预览原样；克制例外（图片不预览，SettingsLearningPage.qml:144–145）与理由原样。
3. **渐进披露与错误预防的暗线（未动，依然成立）**：玻璃参数随主题条件出现（SettingsGeneralPage.qml:143–293）、宠物区随主开关整体收放（SettingsExtensionsPage.qml:305–310）；AA 门禁、热键冲突拒绝、宠物包 14 种拒绝原因人话化。

## 5. 画像红旗

**Sam（键盘/读屏）**：主体可达后，剩余死区一览——打字速度、玻璃六参、宠物大小共 8 个滑块键盘不可调（StepSlider.qml:64–77 无焦点无角色）；OCR 两枚路径字段不可达（PathField.qml:77–81）；热键行只能点击进入录制（HotkeyField.qml:108–112）；纯选择下拉开了列表挑不了行（DropdownField.qml:483 窗口不接受焦点）；页脚 Defaults、Apply custom colors、GitHub repository、Check for updates 四枚裸按钮不可达；六个 TextInput 无读屏名——API key 密码框读到的是无名编辑框（对照：扫描白名单有 Accessible.name，SettingsCapturePage.qml:227）。预览体系仍 hover-only（SwitchRow.qml:40–44；字体页帮助句明写 Hover，SettingsFontPage.qml:107）。

**Alex（效率型）**：每次打开仍回目录；纯选择下拉键盘挑不了；「恢复默认」仍无确认。

**沉浸阅读者 / 预算敏感者**：同上轮——Defaults 裸奔、预算页无今日已花、预算货币与模型价目货币双轨无解释。

## 6. 问题清单

### P0

无。

### P1

**[P1] 「恢复默认」无确认、作用域不可见；本轮新增反讽——键盘上也够不着它**（未动，上轮 P1 原样）
- 页脚 Defaults 仍是一击 `TapHandler → restoreDefaults()`（SettingsPopup.qml:461–481、:268–273），重置 UI-SPEC.md:160 清单所列 15 项设置，即时生效、无撤销。规格明说「不修改 API key，也不修改自启」，按钮上一个字不说；预算页点它什么都不变却照样报「已恢复默认」。同面板「删除云端数据」是两步确认（SettingsSyncPage.qml:267–297）——同一块卡上两种标准。
- 本轮它成了四枚未修裸按钮之一（SettingsPopup.qml:480）：面板内破坏性最大的动作，恰好是键盘读者的盲区。
- 修法不变：复用面板已有 question 通道做行内二态确认（照同步页 confirmingDelete 模式），一句话写明「API key 与开机自启不变」；无默认可恢复的页不显示。命令：**/impeccable harden**

**[P1] 键盘层七成建成，剩余死区清单明确**（上轮 P1 大幅收窄）
- 已建成：目录行、链接行、开关、分段、下拉字段、图标按钮、Chip 全部 Tab 站 + Enter/Space + 角色/命名 + FocusRing；六个 TextInput 补 Tab（SettingsBudgetPage.qml:70、SettingsConnectionPage.qml:52 与 :109、SettingsSyncPage.qml:215、SettingsClipboardPage.qml:44、DropdownField.qml:412）；Esc 分层退出不变。
- 死区一，**滑块整类**：StepSlider.qml:64–77 纯 MouseArea；打字速度（SettingsGeneralPage.qml:439–450）、玻璃六参（:198–258）、宠物大小（SettingsExtensionsPage.qml:284–292）共 8 处键盘不可调。
- 死区二，**纯选择下拉可开不可选**：列表窗口 `WindowDoesNotAcceptFocus`（DropdownField.qml:483），行 pointer-driven（:553–560 注释自认）；Enter/Space/Down 只会开合（:87–95）。主题、背景刷新、词汇档位、解释语言、复习频率、宠物类型、动作条形态、最小词长——8 个纯选择字段，键盘读者改不了值。
- 死区三，**四枚裸按钮**：Defaults（SettingsPopup.qml:480）、Apply custom colors（SettingsGeneralPage.qml:412–416）、GitHub repository 行（SettingsAboutPage.qml:95–97）、Check for updates（:171–175）。DESIGN.md:292「所有可点击元素有焦点态」仍未兑现。
- 死区四，**路径与热键**：PathField.qml:77–81 无焦点无 AT，且 :33–34 注释已宣称「its name to assistive tools」而实现无名——注释跑在了实现前面；HotkeyField.qml:108–112 只能点击进入录制态。
- 死区五，**读屏细项**：六个 TextInput 无 Accessible.name（名单见画像红旗）；Chip 选中态不达读屏（Chip.qml:40–42，Button 角色无 checked——宠物页百枚 chip 听不出哪枚在选中）；Segment 的 Grouping 本身无名（单元格有名，组无名，Segment.qml:28）。
- 修法：滑块补 activeFocusOnTab + 左右方向键 ±1 档 + Slider 角色 + FocusRing；下拉把移选做到字段的 Keys 上（Up/Down 移动、Enter 确认、Esc 关列表），高亮复用 hoveredIndex；裸按钮并入共享控件；PathField 顺手兑现注释的承诺。命令：**/impeccable harden**

### P2

**[P2] 滚动内容被页脚硬切**（未动）：Flickable clip 到页脚分隔线为止、无渐隐（SettingsPopup.qml:408–416）；01/04 号截图沿用——「扩展」副标题「桌面宠物」与 OCR 可用性状态行仍被腰斩，与「安静书页边注」的离开方式相反。命令：**/impeccable polish**

**[P2] 预算页不反馈花费，钱没讲清楚**（未动）：全页一个输入框加「Set 0 to pause the cap.」faint 11px（SettingsBudgetPage.qml:85–91）；无今日已花、无「到顶暂停、次日恢复」的界面表达；模型页价格行 muted 11px（SettingsModelPage.qml:80–86），预算货币与价目货币（USD/百万 token）的关系全产品无处可读。PRODUCT.md 第 4 条原则「成本由读者掌控」在此页承诺最重、兑现最轻。命令：**/impeccable clarify**

**[P2] 关键状态搭最弱视觉通道**（未动）：「Saved / Defaults restored」faint 11px 挤在页脚（SettingsPopup.qml:483–494）；LinkRow note 同款 faint 11px 承载「Not configured」这类阻断态值（SettingsLinkRow.qml:49–57、SettingsModelPage.qml:89–95）；依赖宠物/OCR 而禁用的行只变灰不说明原因（SettingsLearningPage.qml:154–173、SwitchRow.qml 头注释自认「nothing said about why」）。状态与装饰同权重。命令：**/impeccable clarify**

### P3

- 分组标签两种字号并存：取词页/扩展页 11px（SettingsCapturePage.qml:38–41、SettingsExtensionsPage.qml:316–320），通用/学习等页 12px（SettingsGeneralPage.qml:111–115）——同层级同语义，两个值。
- 取词页 `qsTr` 与 `qsTranslate` 上下文分裂未收敛（SettingsCapturePage.qml:149–151、166、181、197、217；HotkeyField.qml:67、124），i18n 工程债原样。
- 「Set 0 to pause the cap.」语义是「不设上限」，绕（SettingsBudgetPage.qml:87）；「每 1000000 个 token」应作「每百万 token」（05 号截图沿用）。
- API 配置页帮助句把不确定性推给读者、页内无「测试连接」小动作（SettingsConnectionPage.qml:113–119）；key 填错的发现路径仍绕经取词失败。
- 连接页两枚输入框描边不随焦点转 ink（SettingsConnectionPage.qml:39、:98 静态 line；对照 SettingsBudgetPage.qml:48、SettingsSyncPage.qml:202），DESIGN.md:274 输入框焦点态未对齐。
- 目录行左缘 9px、页面行 0px 的轻微跳动（SettingsCategoryRow.qml:42 与 SettingsLinkRow.qml:41）——本轮已由代码注释声明为有意（贴齐页内行，UI-SPEC.md 4.4 认可），倾向接受，仅存目。

## 7. Quick wins

1. 纯选择下拉补键盘移选（字段 Keys 上 Up/Down/Enter/Esc，高亮复用 hoveredIndex）——一个改动解锁 8 个字段，死区二整个消失。
2. StepSlider 三件套：activeFocusOnTab + 左右方向键 ±1 档 + FocusRing/Slider 角色——8 个滑块一次受益。
3. 四枚裸按钮并入共享控件或补三件套（Defaults、Apply custom colors、GitHub repository、Check for updates）。
4. 六个 TextInput 各补一行 Accessible.name。
5. 「Saved」与 LinkRow 状态 note 升 muted——两处字号档的事。
6. 预算页输入框下加「今日已花 ¥x / 上限 ¥y」一行，帮助句改「0 = 不设上限；到顶后暂停取词与请求，次日自动恢复」。

---

*未验证项汇总：faint on panel2 浅色对比度精确值（沿用上轮，实测文件在 docs/metrics/accessibility/ 未读）；下拉展开态与热键录制态的像素（瞬时态，行为已由本轮新增 qtest 覆盖）；zoom-focus-ring-switch.png 远端解析两次失败，结论以 FocusRing.qml 源码几何 + icon-button 放大图核对 + 修复流①自述的像素级核对为准。*
