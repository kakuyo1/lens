---
target: 批次① 设置浮层 · 最终版
total_score: 33
max_score: 40
p0_count: 0
p1_count: 0
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
target_fingerprint: "sha256:bc35ea84b87cbfb5f37b4741802a23d3ee4244d7ae37a2d1a7b51606954422f6"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
timestamp: 2026-10-25T00-23-11Z
slug: src-app-qml-settings-settingspopup-qml
final: true
final_commit: 6d85341
code_commit: 5dbe839
---

# 设置浮层 · 最终 critique

**方法**：最终源码逐条对照 + 战役内 2x 真机快照（目录 / 通用 / 阅读学习 / 取词 / 模型 / 预算 / 扩展 / 关于 / 字体 / 连接 / 同步，含暗夜与滚动位）+ `tst_settings_wave3.qml`、`tst_dropdownfield.qml`、`tst_settings_preview.qml` 的末次全量。没有为本文件重新拍摄悬停预览和自定义主题；这两类按源码与 qtest 记，不冒充实拍。

**范围**：`SettingsPopup.qml`、11 个页面、共享控件（`Switch` / `DropdownField` / `StepSlider` / `SliderRow` / `PathField` / `HotkeyField` / `Chip` / `SettingsCategoryRow` / `SettingsLinkRow` 等），以及 UI-SPEC §4.4、DESIGN §3.1 / 4.4。评分只对这个表面，不把共享控件在其他表面的收益重复计入。

## 1. 最终读数

**33 / 40（Good）**。趋势：30 → 31 → 31 → 31 → **33**。

最后 +2 来自两件高危动作被真正关闭：恢复默认有了二态确认，键盘与读屏死区被清完。上一轮停在 31 是诚实的平台期——对表修的是 1–3px 噪音，挣不到滚动、防护、键盘这些大项。

| # | 启发式 | 分 | 最终依据 |
|---|---|---|---|
| 1 | 系统状态可见 | 3 | OCR 原因行已移到灰化开关正下方，滚动有细条并跟随焦点；但 Saved / Defaults restored 仍只占一个 11px muted 槽，长页面没有页内进度 |
| 2 | 贴近真实世界 | 3 | 主题名、EE 档位、预览都是读者的语言；预算页的「设为 0 可暂停上限」仍绕，token / API key 仍无就地注解 |
| 3 | 用户控制与自由 | 4 | 恢复默认翻成 Confirm / Cancel 二态，开问时焦点进 Confirm，取消与完成后回 Defaults；预算页没有可恢复项，整行隐藏；Esc 仍按问题 → 子页 → 分类 → 关面板逐层退 |
| 4 | 一致与标准 | 3 | 左缘、12px 组标签、27px 胶囊已落在组件里；残余是热键节标签与框内「Trigger key」重复、通用页 Theme 标签 13px 粗体与其余字段 12px muted 两档、滑块读数仍用应用字体 |
| 5 | 错误预防 | 4 | 恢复默认守卫、AA 校验不过禁用 Apply、热键冲突拒绝、预算 validator 全在；保护句写明 API key 与开机自启不变 |
| 6 | 识别而非回忆 | 4 | 悬停预览直接画读者自己的解释气泡；下拉选项有预览；图片不预览并给了产品理由 |
| 7 | 灵活与效率 | 3 | 滑块、下拉、路径、热键、四枚按钮、六处输入框都进了键盘；但目录仍无搜索 / 直达，每次打开回目录 |
| 8 | 美学与极简 | 3 | 灰阶与留白节奏成立；阅读学习页 11 个控件一列平铺，没有组标签，扩展页仍长 |
| 9 | 错误恢复 | 3 | OCR 原因与控件同屏；API key 填错仍要到取词失败才发现，页面没有「测试连接」 |
| 10 | 帮助与文档 | 3 | 预览是最好的上下文帮助；token 是什么、key 从哪来仍要出门找 |

**认知负荷**：三项未过——分块（阅读学习页 11 个控件一列）、分组（七个词卡开关无名无界）、工作记忆（预算和模型页的因果链被拆在三个页面）。比第一轮的「中等负荷」好，但还没有到低负荷。

## 1.1 本面优先项

按全局 C 顺序（见 `FINAL-00-overview.md` §4）；未列出的项仍在「仍然开着」表里，但暂不排期。

| 序 | 候选 | 项 | 级别 |
|---|---|---|---|
| 1 | C7 | 预算页反馈今日已花与到顶行为 | P2 |
| 2 | C11 | 说明与分组标签 | P2 / P3 |
| 3 | C12 | 测试连接 | P2（独立功能） |
| 4 | C13 | 排版与目录 | P3 |

## 2. 专属性判定

**Authored。** 预览体系仍然是这个产品的招牌：指针停在标题上，面板旁浮出的是读者自己的解释气泡，按当前主题、字体、解释语言、打字速度渲染，只用 Preview 胶囊与 6px 虚线框认领「这是演示」。开关行在键盘焦点落上去时也升同一张卡——鼠标和键盘问的是同一个问题。全部设置即时生效、没有 Save；右注的 muted / faint 两档按「状态 vs 值」分义。这些放进通用设置模板都活不下来。

同时要承认，预算页与连接页仍是最通用的一对：一个输入框、两根输入线，没有 Lens 的指纹。产品指纹最重的预览和最需要它的钱之间仍是断开的。

## 3. 最终切片里真正改掉的

1. **恢复默认不再一击全局重置**：`SettingsPopup.qml:43-46` 的 `confirmingDefaults` / `defaultsAvailable`，页脚从单粒 Defaults 变成 Defaults → Confirm + Cancel，`restoreDefaults()` 只在 Confirm 路径调用（`:306-311`）。开问时 `Qt.callLater` 把焦点交给 Confirm（`:540-541`），取消与完成后回到 Defaults（`:555-560`）；预算页 `defaultsAvailable: false`，整行不画。状态行同时换成保护句「API key and launch at login stay as they are.」（`:574-575`），不再让上一笔「Defaults restored」挂在新问题旁。
2. **键盘死区清到底**：`StepSlider` 有 `activeFocusOnTab`、`Accessible.Slider`、名称与 FocusRing（`:32-38`、`:75`），方向键 / Page / Home / End 走 `nextValue()`（`:47`）；`DropdownField` 的列表有高亮索引、上下移动、Enter 选择、Esc 收起（`:51`、`:218-247`），可输入框不再把 Space 当选择；`PathField` 与 `HotkeyField` 都有 Tab 站、名称、Enter / Space 与焦点环；关于页的 GitHub 行和「检查更新」、通用页的 Apply custom colors、六处 TextInput 全部补了名称与 Tab。
3. **滚动可见且焦点被带回**：正文右侧有 `objectName: "scrollThumb"` 的细条（`:463`），`revealFocus()` 沿父链确认焦点在正文内，再把 `contentY` 钳到可视区（`:201-225`）。这是设置页第一次能在键盘上从头走到尾而不把焦点丢在视口外。
4. **OCR 原因搬到灰化控件下面**：`SettingsCapturePage.qml:84-99` 的原因行现在紧跟触发通道组，在首屏能同时看到灰掉的 OCR、自动扫描、触发热键与「OCR runtime is unavailable / English OCR data is missing」。组件契约没有改，改的是空间关系。
5. **状态与禁用不再同权重**：Saved 与状态类 LinkRow note 升 muted；依赖 OCR / 宠物的禁用行补原因；禁用态透明度统一到 0.45 的按钮规格（检查更新、Apply custom colors 两处）。

## 4. 问题账（已关闭）

| 来源 | 问题 | 结局 |
|---|---|---|
| 第 1 轮 P1 | 恢复默认一击重置、作用域不可见、预算页也显示 | Confirm / Cancel 二态 + 保护句 + 预算页隐藏；qtest 三例 |
| 第 1 轮 P1 | 键盘与读屏整层空缺，违反 DESIGN 焦点态 Do | 控件层与页面接线全部补齐；设置全页可 Tab，读屏有名称与按压 |
| 第 1 轮 P2 | 滚动内容被页脚硬切、无指示 | 细滚动条 + 焦点跟随；目录与长页不再靠截断提示滚动 |
| 第 1 轮 P2 | 关键状态搭 faint 11px | Saved / 状态 note 升 muted；禁用行补原因 |
| 第 2 轮 P1 | OCR 灰化原因在折叠线下 | 原因行移到触发组下；首屏可读 |
| 第 2 轮 P1 | 正文 Flickable 无 ScrollBar、焦点落视口外 | 同上，滚动条与 `revealFocus()` 落地 |
| 第 2 轮 P1 | 页脚 Defaults 无 Tab / 无读屏 | 换成共享 `Chip`，Tab、名称、按压、焦点环齐备 |
| 第 3 轮 P2 | 阅读学习页七个开关无组标签 | **未做**，仍在账上 |
| 第 3 轮 P3 | 模型页价格行未走 MixedText、无千分位 | **未做**，仍在账上 |
| 第 4 轮 P3 | 禁用态 0.58 硬编码 | 统一到 0.45 |

## 5. 仍然开着的问题

| 优先级 | 问题 | 证据 | 为什么还没关 |
|---|---|---|---|
| P2 | 预算页不显示今日已花、剩余与到顶行为 | `SettingsBudgetPage.qml:90-95` 仍只有「Set 0 to pause the cap.」 | 已实现（C7）：输入框下回读今日花费与上限，到顶转 warn，并说明暂停到次日 |
| P2 | 阅读学习页七个开关没有组标签 | `SettingsLearningPage.qml` 七行平铺，无「词卡内容」一类的 11px muted 组标签 | 单表面遗留；姊妹页取词为三个开关立了组，对照之下这页是噪音 |
| P2 | 没有「测试连接」 | `SettingsConnectionPage.qml` 只有 key 与服务地址；错误要到取词失败才出现 | 单表面遗留；BYOK 产品里 key 是阻断全产品的状态，却没有一个验证动作 |
| P2 | token / API key 无就地解释，价格货币与预算货币的关系不可读 | `SettingsModelPage.qml:83-85` 价格行只有一串模型价；连接页无「从哪拿 key」 | 第 1 轮 P2 遗留；预览体系不覆盖钱与连接 |
| P3 | 目录 7 项、扩展页全展开的认知负荷 | 目录固定 7 行；扩展页 40 动作 chip + 配饰 chip 平铺 | 单表面遗留；与「安静书页」的气质有张力 |
| P3 | 每次打开回目录，连续改设置重复走目录 | `onVisibleChanged` 重置 category / route | 仍是效率项；没有目录搜索或直达 |
| P3 | 热键节标签与框内左标签重复 | `SettingsCapturePage.qml:137` 与 `HotkeyField.qml:82` 同时写「OCR trigger key / Trigger key」 | 第 4 轮 P3；只在录制态才需要框内提示 |
| P3 | Theme 标签 13px 粗体与字段标签 12px muted 两档 | `SettingsGeneralPage.qml:158-159` 对 `:114` | 第 4 轮 P3；页面上没有理由解释一响一静 |
| P3 | 滑块值读数仍用应用字体，不是等宽 | `SliderRow.qml:49-56` | 第 4 轮 P3；拖动时读数宽度会抖，纯数字应按 DESIGN 走 Cascadia |

## 6. 边界（不是待办）

- **没有 Save** 是产品选择：即时生效，唯一输入型设置失焦自动写。由此「恢复默认」的后悔药只能是确认书，不是快照撤销；这一轮选了确认书。
- **玻璃主题下 muted / faint 低于 AA** 是 ADR 0106 已认的材质边界；设置页的右注与组标签同样受影响，不在本表面单独解决。
- **自定义主题与玻璃参数组合**没有在本轮重新采样；Apply custom colors 的 27px 高度与禁用路径由代码和 qtest 钉住，不冒充真机像素结论。
- **真实 NVDA / UIA 会话**没有跑。测试只证明名称、角色、按压动作在树里；不证明某个读屏器会怎么念。

## 7. 画像

- **Alex（效率型）**：键盘现在真的能用了，Defaults 也不会一按毁全部；仍要为「改下一个设置」重走目录，且没有搜索。
- **Sam（读屏）**：开关、下拉、滑块、路径、热键、按钮都有名称与按压；滚动跟随焦点；OCR 原因和灰化控件同屏。仍在的是玻璃主题的对比度边界，以及没有真实读屏会话的验证。
- **Jordan（初访者）**：预览仍然是最好的第一课；token 和 API key 仍是黑话，预算页已经能回读今日花费与上限。
- **预算敏感者**：预算页现在回读今日已花、上限与到顶行为；模型价与预算币种的关系仍无处可读。

## 8. 开放问题

1. 预算页已经回读今日已花 / 上限 / 到顶行为；下一步是让模型价与预算币种的关系也在同一页说清。
2. 阅读学习页的七个开关能不能只加一排 11px muted 组标签，不建新容器，保住「触发通道是唯一分组容器」的规格？
3. API key 的「测试连接」是不是应该和失焦保存同一处发生，而不是等第一次取词失败？
4. 目录要不要允许输入跳转，或者至少记住上次离开的子页？即时生效模型下，连续改两个分类的设置仍要重走一遍目录，这是效率型读者每天付的税。

## 9. 证据与未采样

**证据**：`%TEMP%\lens-critique\` 的 01–14 号真机快照（目录、通用、阅读学习、取词、模型、预算、扩展、关于、字体、连接、同步、扩展宠物、暗夜两页）；`%TEMP%\lens-keyboard-qa\settings-catalog.png`、`settings-learning.png`；`%TEMP%\lens-crit4\settings-catalog.png`、`settings-learning.png`；`tst_settings_wave3.qml` 的 Defaults 三例、键盘与滚动两例、目录与页脚两例；`tst_dropdownfield.qml` 的键盘选择与单一 Tab 站；`tst_settings_preview.qml` 的开关聚焦升预览。

**未采样**：悬停预览的最终像素、下拉展开的运行态、自定义主题、玻璃参数全组合、真实屏幕阅读器会话、暗夜下的焦点环逐像素。
