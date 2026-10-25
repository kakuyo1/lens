---
target: 批次③ 托盘菜单 · 最终版
total_score: 32
max_score: 40
p0_count: 0
p1_count: 1
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
target_fingerprint: "sha256:1eecd9c375963b56fe5f2066cc503940d6aa1242fd7896d01b2e1b43e06efa40"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
timestamp: 2026-10-25T00-23-11Z
slug: src-app-qml-panels-traymenu-qml
final: true
final_commit: 6d85341
code_commit: 5dbe839
---

# 托盘菜单 · 最终 critique

**方法**：最终源码逐条对照 + 战役内真机快照（`%TEMP%\lens-critique\tray-light.png`、`tray-dark.png`、`%TEMP%\lens-budget-tray-*.png`）+ `tst_traymenu.qml` 的末次全量。最终版没有重新拍摄菜单展开与语言子菜单的运行态；这两项按源码与 qtest 记，并在末尾列为未采样。

**范围**：`TrayMenu.qml`、`MenuRow`、`tray.cpp` 的状态线 / tooltip、UI-SPEC §4.0 / 4.1 / 4.2 / 4.6、`tray_state.h`、ADR 0104。评分只对菜单表面，不把托盘图标四态的绘制质量重复计入。

## 1. 最终读数

**32 / 40（Good）**。趋势：28 → 30 → 32 → **32**。

最后一轮 +0 是诚实的：语言行的按压出口、状态行省略、分隔线都是真修，但菜单的键盘可达性仍没有被证明，Escape 仍整张收起而不是先折列表。地板补了，结构没变，所以分数不动。

| # | 启发式 | 分 | 最终依据 |
|---|---|---|---|
| 1 | 系统状态可见 | 4 | 首行四态与 `tray_state.h` 同优先级；忙碌与空闲用文字分开；预算耗尽压过模式名，因为取词会随之暂停 |
| 2 | 贴近真实世界 | 4 | 次 / 词口径（ADR 0104）在菜单与 tooltip 一致；MixedText 让数字留在 Cascadia，中文单位用应用字体 |
| 3 | 用户控制与自由 | 2 | 语言行现在有 `onPicked`，列表行有 Enter / Space；但 Escape 仍是整张菜单收起，不会先把列表折回去，且菜单是否真的激活未被证明 |
| 4 | 一致与标准 | 3 | 分隔线改成真结构，不再有死的 `anchors.margins`；仍欠的是左键也开菜单，而 §4.0 只写右键 |
| 5 | 错误预防 | 3 | Quit 立即执行；危险色与分隔线是唯一防护，符合托盘惯例，仅此而已 |
| 6 | 识别而非回忆 | 4 | 每行带自己的实时值：统计行是今日次数与花费，语言行是当前语言 |
| 7 | 灵活与效率 | 2 | 行内没有键盘路径的实证；窗口由托盘手势打开，没有 `requestActivate`；规格却写着语言行是 Tab 站 |
| 8 | 美学与极简 | 4 | 五行、一列、全令牌；状态标与功能图标同列，安静成立 |
| 9 | 错误恢复 | 3 | 预算耗尽说得很直，但仍是死胡同；设置就在两行下，却没有链接 |
| 10 | 帮助与文档 | 3 | tooltip 4.6 承担模式与次数；240px 卡内不该再塞更多 |

**认知负荷**：只有一项失败——一个产品标，两种含义（`ok` 同时表示「空闲就绪」和「解释进行中」），读者每次都要读文字才能分辨；托盘图标自己的忙碌小点在这里没有孪生。其余五项通过：五行平铺、分组清楚、一个语义色一个意思、状态无需交互可读、语言列表是诚实的渐进披露。

## 1.1 本面优先项

按全局 C 顺序（见 `FINAL-00-overview.md` §4）；「加取词开关」不在本轮，属于产品 IA。

| 序 | 候选 | 项 | 级别 |
|---|---|---|---|
| 1 | C1 | 菜单键盘路径 | P1 |
| 2 | C10 | 预算行、忙碌标、左键规格 | P3 |

## 2. 专属性判定

**Authored。** 轮廓是标准托盘菜单，但首行是 Lens 自己的发明：把托盘图标四态的优先级写成一句话，标就是产品标。`stateLineFor()` 是纯函数，可按四种输入离线驱动，不花一次 API 的钱；状态行的 11px 文字与 15px 图标共用功能行的列，不是另起一列。这些不是通用托盘菜单会有的东西。

## 3. 最终切片里真正改掉的

1. **语言行不再 hover-only**：`TrayMenu.qml:207-220` 的 `onPicked` 切换 `listVisible`，展开时 `Qt.callLater` 把焦点交给当前语言；列表项各有 `activeFocusOnTab`、`Accessible.role` / `name` / `onPressAction`、Enter / Space 和 FocusRing（`:305-325`）。`tst_traymenu.qml:228` 用键盘打开并选中，`:205` 覆盖指针选择。
2. **长本地化串不再画出卡缘**：状态文字右锚到卡右缘并 `ElideRight`（`:169-180`）；西班牙语的长句与「1,280 consultas · ¥12.34」一类的重日不会压到行标。
3. **分隔线符合规格**：`x: 2`、`width: parent.width - 4` 的 1px 线（`:236-241`），不再用无锚定的 `anchors.margins: 5` 冒充上下留白。
4. **四态映射有了测试**：`test_theStateLineMapsTheFourTrayStates` 与 `test_theStateLineTakesTheControllerReadout` 把优先级和实时接线钉住；预算耗尽态有真机快照 `lens-budget-tray-exhausted.png` 核对琥珀标。

## 4. 问题账（已关闭）

| 来源 | 问题 | 结局 |
|---|---|---|
| 第 3 轮 P1 | 语言子菜单 hover-only，读屏按压动作落空 | `onPicked` 接线；列表行有 Tab / Enter / Space / Accessible；qtest 键盘与指针各一例 |
| 第 3 轮 P2 | 长本地化串无右锚、无省略 | 状态文字右锚 + `ElideRight`；统计 note 也有宽度约束 |
| 第 3 轮 P2 | Quit 分隔线的 `anchors.margins: 5` 是死代码 | 改成有真实几何的 1px 线 |
| 第 2 轮 P1 | 语言子菜单 hover-only 且无可访问动作 | 同上 |
| 第 2 轮 P2 | 状态行无右锚无省略 | 同上 |
| 第 2 轮 P2 | 分隔线不守 5px 呼吸 | 同上 |

## 5. 仍然开着的问题

| 优先级 | 问题 | 证据 | 为什么还没关 |
|---|---|---|---|
| P1（按影响） | 菜单的键盘路径没有被证明；规格、实现、激活模型三方不一致 | UI-SPEC 4.2 写「语言行是 Tab 站、展开时当前语言行接管焦点」，`TrayMenu.qml:305-325` 也给了 Tab / Enter；但窗口没有 `WindowDoesNotAcceptFocus`，由 `SurfaceHost.qml:416` 的 `placePanel` 显示，没有 `requestActivate`；同文件的 Escape 注释（`:363-365`）明确假设「a window that never becomes active」 | 已实现（C1，ADR 0108）：打开时请求激活，首行接管焦点，Esc 先折列表；qtest 焦点链通过，真托盘 Tab 仍待一次手动确认 |
| P3 | 预算耗尽行是陈述，不是可点的出路 | `TrayMenu.qml:144-181` 的状态行没有 TapHandler；Settings 就在下面两行 | 第 3 轮 P3；读者看到「今日预算已用完」后仍要自己找设置 |
| P3 | 左键也开菜单，§4.0 只写右键 | `tray.cpp:55-59` 对 `Trigger` 与 `Context` 一视同仁；UI-SPEC §4.0 只列右键 | 第 3 轮 P3 的规格漂移仍在；对托盘常驻产品，左键开菜单可能是对的，但规格得承认 |
| P3 | 忙碌态与就绪态共用同一个绿色产品标 | `stateLineFor()` 的 `busyLabel` 分支返回 `ok` 绿，和「空闲且取词开启」同色；文字区分，图形不分 | 第 3 轮认知负荷项；托盘图标有忙碌小点，菜单首行没有孪生 |
| P3 | 菜单里没有取词开关 | 五行固定，取词要绕到设置 | 第 3 轮画像项；240px 卡是否该承担这个最常用开关，仍未裁 |
| P3 | 预算耗尽行的文案与设置预算页的口径仍各说各的 | 这里说「Today's budget is used up」，预算页说「Set 0 to pause the cap.」 | 文案层旧账；读者要在两处自己拼出因果 |

## 6. 边界（不是待办）

- **Quit 没有二次确认**：托盘菜单的行业惯例，危险色与分隔线是全部防护；不为它单独立确认，是本轮接受的取舍。
- **tooltip 外观归 Windows**：UI-SPEC 4.6 只定内容与次数口径，外观、圆角、字体、出现时机不由 Lens 控制；因此不把 tooltip 的观感算进这个表面的分数。
- **菜单不抢焦点**如果是产品选择，那么 Shift+Tab / 方向键都不该承诺；麻烦的是当前规格承诺了 Tab。这不是纯文档问题，是契约裁决。
- **真实屏幕阅读器会话**没有跑；即使读屏按压动作在树里，菜单是否会在朗读时被外部点击收起，未验证。

## 7. 画像

- **Alex（效率型）**：每天开菜单看状态，只能看不能切；设置是来回一趟，左键也没有承担取词开关。预算耗尽时，他看到一句话，却没有就地解决的门。
- **Sam（读屏）**：语言列表的选项现在有名称与按压动作，比上一轮好；C1 已让菜单在打开时接管焦点，qtest 焦点链通过，真托盘 Tab 仍待一次手动确认。预算耗尽行的文字读得到，行动点没有。
- **Jordan（初访者）**：五行很容易懂；「今日统计」的 `128 次 · ¥0.42` 自释；语言行右侧当前值也清楚。他不知道的是左键为什么也开菜单，以及预算耗尽后该去哪。
- **预算敏感者**：预算耗尽被诚实说出，但没有「去调预算」的下一步。

## 8. 开放问题

1. 这张菜单收不收焦点？已裁 D1：收——打开时请求激活，保留 Tab / Enter，Esc 先折列表再收菜单；剩下的是验证 Windows 下 `requestActivate` 是否会被托盘点击吞掉。如果被吞掉，才退回「无焦点 + 读屏地板」并改规格。
2. 首行是状态条，还是产品头条？对一个没有主窗口的产品，这一行是全部门面；把「今天做了什么」放上来、把状态降给托盘图标，值不值得打破五行的平衡？
3. 预算耗尽时，状态行能不能直接点进设置预算页？这会把一个死胡同变成一条最短修复路径，成本是一行 TapHandler。
4. 取词开关该不该有一席之地？如果托盘是唯一常驻入口，最常用的开关藏在设置里的距离，可能比多一行更贵。

## 9. 证据与未采样

**证据**：`%TEMP%\lens-critique\tray-light.png`、`tray-dark.png`（菜单两主题）；`%TEMP%\lens-budget-tray.png`、`lens-budget-tray-full.png`、`lens-budget-tray-crop.png`、`lens-budget-tray-exhausted.png`（预算态与琥珀标）；`tst_traymenu.qml` 的四态映射、实时读数、语言键盘与指针选择、长串省略、Escape、Quit 危险样式、菜单宽度。

**未采样**：语言列表展开后的真实焦点落点、两主题下的 hover / 按压运行态、左键打开的实机路径、菜单在真实读屏器中的朗读顺序、屏幕边缘翻边后的语言列表位置。
