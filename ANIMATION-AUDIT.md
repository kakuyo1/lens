# Lens 动画系统调查

> 调查日期：2026-10-17
> 范围：QML 动效 token、各 surface 的实现、RevealText / 宠物 / 玻璃 / 原生窗口路径
> 状态：临时文档，待后续执行完成后删除
> 本次调查未修改代码，也未运行测试。

## 结论

动效系统本身是健康的：token 集中、减少动效彻底、面板的出现动画只有一处实现。真正的问题有两类：

- **覆盖不完整**：最常出现的 Bubble 和 SelectionBar 没有进出场；Notice / UpdateCard / RemovalQuestion 只淡入不淡出；UI.md 已经规定的按压反馈只覆盖了少数控件。
- **几条高频路径把动画做在了昂贵的对象上**：RevealText 每个流式 chunk 全量重建字符布局；Bubble 的反馈行、YearWords 调色层、Words 筛选行都在逐帧改原生窗口尺寸；玻璃主题在桌面静止时仍以 15 Hz 采样。

现有 profiler 还没有可靠的逐帧百分位数据，因此下面的性能结论是代码级判断，最后一节给出了验证方式。

## 一、现状盘点

### 1.1 骨架

动效只有三个时长和一个缓动，全部在 [src/app/qml/theme/Motion.qml:19](src/app/qml/theme/Motion.qml#L19)：

| token | 时长 | 用途 |
| --- | ---: | --- |
| `press` | 150 ms | 按压、开关、颜色状态 |
| `pop` | 180 ms | 表面出现、列表行进出 |
| `bubble` | 220 ms | 气泡反馈行展开 |

减少动效时，每个时长都解析为 0（[Motion.qml:22-31](src/app/qml/theme/Motion.qml#L22)）；Windows 的辅助功能设置优先于应用开关。

标准的“卡片出现”只有一处，在 [src/app/qml/components/ShadowCard.qml:46](src/app/qml/components/ShadowCard.qml#L46)：卡片不透明度跟随所在窗口的 `visible`，配一条 `pop` 的 `Behavior`，并用位移 `y: (1 - opacity) * 8` 让卡片上浮 8 px。窗口的 `leaving` 为真时，卡片淡出结束后由同文件的 Timer 再隐藏窗口（[ShadowCard.qml:61-70](src/app/qml/components/ShadowCard.qml#L61)）。面板之间的交接由 [src/app/qml/Main.qml:375](src/app/qml/Main.qml#L375) 的 `hidePanels()` 设置 `leaving` 完成交叉淡化。

### 1.2 覆盖表

| 表面 / 路径 | 现在有什么 | 缺口 |
| --- | --- | --- |
| 六张面板、托盘菜单、Notice、UpdateCard、RemovalQuestion | ShadowCard 淡入 + 上浮 8 px；面板之间交叉淡化；`leaving` 后才隐藏 | Notice / UpdateCard / RemovalQuestion 的关闭是硬切 |
| **Bubble** | RevealText 逐字写出；反馈行 220 ms 高度 + 透明度；按钮 150 ms 按压 | **没有进出场**；倒计时结束或点击反馈后直接隐藏 |
| **SelectionBar** | 仅按钮 150 ms 按压 | **没有进出场**；动作后直接隐藏 |
| 评审牌 / ReviewCard / 答题卡 | 宠物自己的 `ask` 动作；`expanded` 和 `visible` 绑定瞬间切换 | 卡片本身没有过渡；ReviewCard 按钮没有按压 |
| Mask | `shown` 直接切换 | 关闭必须保持瞬时（见下） |
| 单词列表筛选 | 行高 + 透明度 180 ms（[WordsPopup.qml:356-371](src/app/qml/WordsPopup.qml#L356)） | 删除单词时行直接消失 |
| YearWords 调色层 | 高度展开、色环旋入、色相旋转、点缩放 1.12 | 展开期间逐帧修改原生窗口尺寸 |
| 设置页 | 不转场 | 这是 UI.md 有意的“不加转场”，不是缺口 |
| 宠物 | 单一 50 Hz 时钟推进 1–12 fps 精灵 | 内容帧率远低于时钟 |
| RevealText | 每 16 ms 推进一次光标，尾字模糊 | 每收到一个流式 chunk 就全量重建布局 |
| 玻璃主题 | 可见时约 15 Hz 截屏 + 模糊 | 桌面静止时也持续采样 |

两个例外需要明确写进文档：

- 宠物不响应“动画效果”开关和 Windows 减少动画设置，这是 [docs/adr/0023-pet-single-frame-clock.md:8](docs/adr/0023-pet-single-frame-clock.md#L8) 的明确取舍，不是遗漏。
- Bubble 与 SelectionBar 自带卡片，不走 ShadowCard，所以它们只有按压和反馈展开（[docs/QML.md:252-255](docs/QML.md#L252)）。要给它们加进出场，等于改这条规范。

## 二、值得加入的动画

### P0-1 气泡与选区条的进出场

这是收益最大的一处。气泡是软件里出现频率最高的 surface，但它的显示路径是先设 `visible = false`，再由 0 ms 的 Timer 定位、显示并 `raise()`（[Bubble.qml:165-186](src/app/qml/Bubble.qml#L165)），倒计时结束和点击反馈都直接 `visible = false`（[Bubble.qml:212-223](src/app/qml/Bubble.qml#L212)、[Bubble.qml:356-371](src/app/qml/Bubble.qml#L356)）。选区条同样是 `visible = false` 后由 `Qt.callLater` 定位再显示（[Main.qml:619-625](src/app/qml/Main.qml#L619)），动作后立即隐藏（[SelectionBar.qml:169-174](src/app/qml/SelectionBar.qml#L169)）。

建议：

- 气泡卡片内容做 `opacity` + 8 px 上浮，时长 `pop`；退场 150–180 ms 后再隐藏窗口。只在 `!wasVisible` 时入场；已经可见的气泡刷新内容时保持不动，否则换词会闪。
- 选区条入场用 `pop`、退场用 `press`。它出现在指针旁边，退场不宜超过 150 ms；动作本身仍然立即执行，只是窗口晚一点收。
- 不要直接动画原生窗口的 `opacity`，而是动画卡片内容；窗口在淡出期间还要禁止点击，否则透明但可交互，会挡住下面的文字。
- 一个关键前提：`show()` 先隐藏再定位，是为了避免窗口用旧高度画出第一帧（[Bubble.qml:161-166](src/app/qml/Bubble.qml#L161)）。加淡入不能破坏这次“先量高、再显示”的步骤；淡入应发生在 `visible = true` 之后，而不是提前。
- 最小改法是各自加一段 `Behavior`；如果评审卡也采用同一套逻辑，再抽成一个只负责“淡入、上浮、延迟隐藏”的组件，并把决定记进 ADR。

规范影响：需要给 [UI.md:412-430](UI.md#L412) 的动效表补上“气泡/选区条出现与退场”，并改掉 docs/QML.md 里“它们只有按压与反馈展开”这句话。

### P0-2 通知、更新卡、删除确认的对称退场

这三者都用了 ShadowCard，所以入场已经有淡入 + 上浮；但关闭时直接 `visible = false`，没有 `leaving`：

- [Notice.qml:58-61](src/app/qml/Notice.qml#L58)
- [UpdateCard.qml:102-105](src/app/qml/UpdateCard.qml#L102)
- [RemovalQuestion.qml:99](src/app/qml/RemovalQuestion.qml#L99)（另有 186、219 两处）

ShadowCard 已经写好了“`leaving` 为真 → 淡出 → Timer 隐藏窗口”的路径，这三个窗口只需要声明 `leaving`，关闭时先置 `true`。这是成本最低、观感提升最直接的一项：现在它们像被拔掉电源一样消失。

注意两点：

- 控制器回调（关闭通知、提交删除决定）应该在点击时立即发生，不要等淡出结束。
- 删除确认是模态窗口。淡出的 180 ms 内窗口仍在，会继续挡住下面的列表。可以接受，也可以在 `answered` 之后临时解除模态；后者更干净。

规范影响：UI.md 现在只写了“出现”，建议补一行“浮层退场”，并明确“语义回调立即、视觉延迟”。

### P0-3 补齐按压反馈

UI.md 已经规定按钮按压是 `scale(.97)`、`press` 150 ms（[UI.md:417](UI.md#L417)），所以这不是新设计，而是实现覆盖率问题。

全仓库 25 个 QML 文件共有 50 个 `TapHandler`，只有 7 个文件出现 `0.97`：Notice、UpdateCard、RemovalQuestion、SelectionBar、WordsPopup、BubbleVerdictRow、SettingsAboutPage。缺得比较明显的是：

- 设置 / 费用 / 统计 / 年度 / 托盘各面板标题行右侧的图标按钮；
- ReviewCard 的 Remembered / Forgot；
- TrayMenu 的普通菜单行、SettingsCategoryRow、SettingsLinkRow；
- Segment、Chip、PathField、HotkeyField、Switch。

建议按“读者是否把它当按钮”来补，而不是所有 `TapHandler` 一律缩放：图标按钮和主按钮用 `scale(.97)`；整行点击的菜单项用按压底色变化更合适。行 hover 保持瞬时，不要顺手给 hover 加过渡——[UI.md:426](UI.md#L426) 明确禁止，真机上指针扫过列表会像在“拖影”。

规范影响：无，属于把已有规范补齐。

### P1-1 删除单词时的行退场

筛选变化已经有很完整的行折叠：行高 + 透明度 180 ms，`visible` 跟随动画而不是筛选（[WordsPopup.qml:349-371](src/app/qml/WordsPopup.qml#L349)）。但删除单词是直接调 `Controller.removeWord()`（[WordsPopup.qml:100-107](src/app/qml/WordsPopup.qml#L100)），模型一变整行直接消失。

建议在确认删除后，先把该行标记为 `removing`，复用同一套高度 / 透明度折叠，180 ms 后再真正从模型移除。这样删除有“收起来”的结束感，也给列表一个短暂的状态说明。中等成本，主要难点是删除后列表高度和窗口高度的联动（见性能一节）；如果窗口尺寸问题先不解决，这一项可以先不做。

### P1-2 Segment / Chip 的选中态

这两者的选中都是瞬时换底色、换文字色和字重：Segment 在 [Segment.qml:40-56](src/app/qml/components/Segment.qml#L40)，Chip 在 [Chip.qml:14-35](src/app/qml/components/Chip.qml#L14)。开关已经有 150 ms 的底色 + 滑块过渡（[Switch.qml](src/app/qml/components/Switch.qml)），分段控件却没有，读起来会有一点“硬”。

建议只做 150 ms 的填充色 / 边框色过渡，不做滑块位移，也不做文字缩放。字重从 Normal 到 Bold 的跳变无法平滑插值，强行做会带来宽度抖动；保留瞬时字重反而更稳。这一项属于状态反馈，不是转场，但 UI.md 表里没有它，采用前需要补一行。

### P2 评审流程、遮罩、语言列表、宠物包弹窗

- **评审流程**：`ReviewSignWindow` 的窗口可见性是绑定 `asking && placed && Pet.action === "ask"`（[ReviewSignWindow.qml:42](src/app/qml/ReviewSignWindow.qml#L42)），答题卡靠 `sign.expanded && sign.visible`（[ReviewSignWindow.qml:190](src/app/qml/ReviewSignWindow.qml#L190)），都是瞬时的。宠物的 `ask` 动作已经承担了大部分注意力引导，所以优先级不高；如果加，建议只给 ReviewCard 做 fade，不要做位移动画，避免和手部 / 牌子的摆放打架。ReviewCard 的按钮至少应补按压反馈（[ReviewCard.qml:77](src/app/qml/ReviewCard.qml#L77)、[ReviewCard.qml:108](src/app/qml/ReviewCard.qml#L108)）。
- **Mask**：`open()` 直接 `shown = true`，`close()` 直接 `false`（[Mask.qml:47-48](src/app/qml/Mask.qml#L47)）。入场可以给那层 `rgba(0,0,0,.38)` 做 100–120 ms 淡入，输入在淡入期间就保持可用；**退场必须保持瞬时**，因为蒙版要在截屏之前消失，淡出会把蒙版留在截图里。这一项是“可以做，但不是必须”。
- **托盘语言列表**：列表卡片靠 `Loader.Ready` 触发 ShadowCard 的淡入，窗口高度是跳变的。要让它平滑展开就得逐帧改原生窗口高度，正好落进性能一节的坑里；建议等窗口尺寸策略明确后再做。
- **PetPackageDialog**：Qt Quick Controls 的 `Popup`，没有自定义 enter / exit。加一个 150 ms 的淡入 / 缩放很容易，但它是低频模态框，优先级最低。

### 不建议加入

- 设置页 / 面板内容的转场。UI.md 的“不加转场”是有意的；这类阅读工具里，页面滑动会打断阅读节奏。
- hover 过渡、弹簧、回弹、视差。现有动效的克制是产品性格的一部分，而且 hover 加过渡在长列表里会明显拖影。
- 给宠物加“更丰富”的待机动画。宠物已经有独立的状态机；真正该优化的是它的时钟成本，而不是再加动作。

## 三、值得优化的性能与体验

再次说明：以下是代码级嫌疑排序，不是实测结论。仓库现有的自动化基线还没有可靠的逐帧百分位，只测到了 Creating / JavaScript 等捕获耗时（[qml-automated-scenario-baseline-2026-10-03.md](docs/metrics/runtime-performance/qml-automated-scenario-baseline-2026-10-03.md)）。

### P0-1 RevealText 的全量重建（最值得先测）

这是我认为最可能的动画卡顿来源。

- `onTextChanged` 每次都调用 `rebuild()`（[RevealText.qml:190-200](src/app/qml/components/RevealText.qml#L190)）。
- `rebuild()` 会把整段文字重新走一遍，生成全新的 `pieces` / `positions` 数组并整体赋值（[RevealText.qml:137-188](src/app/qml/components/RevealText.qml#L137)）。`pieces` 是 Repeater 的 model，换数组等于销毁并重建全部字符 delegate。
- 每个字符是一个独立的 `Text`（[RevealText.qml:287-310](src/app/qml/components/RevealText.qml#L287)）；光标每 16 ms 变化一次，每个已落定的字符都要重新求值 `softness(index)`（[RevealText.qml:296](src/app/qml/components/RevealText.qml#L296)），所以揭示期间是每帧 O(n) 的绑定求值。
- 好的部分：模糊尾巴被限制在 `softSpan = 6` 个字符（[RevealText.qml:64-69](src/app/qml/components/RevealText.qml#L64)、[RevealText.qml:317-357](src/app/qml/components/RevealText.qml#L317)），不会随整段长度线性增长。但一个气泡最多有 7 个 RevealText 字段同时揭示（[Bubble.qml:454-597](src/app/qml/Bubble.qml#L454)），尾字模糊的 shader 总数仍然可能是“字段数 × 6”。
- 还有一个小的浪费：揭示 Timer 的运行条件只看文本和光标，不看窗口是否可见（[RevealText.qml:215-223](src/app/qml/components/RevealText.qml#L215)）。气泡被关掉后，剩余字数的 Timer 还会跑几秒。

建议按顺序做：

1. **先测长答案**：用现有 `ProfileScenario.qml` 的气泡步骤换一段长释义，或者加一个流式长文本步骤，看逐帧时间。
2. **增量追加，而不是整体重建**：文本只会在尾部增长，保留已有的 `positions` 和 delegate，只追加新字符；宽度、字体、换行模式变化时才全量重建。Repeater 对普通 JS 数组的 `push` 不敏感，需要换成会通知增量的模型（ListModel 或 C++ model），或者改成“已落定部分按词 / 行渲染”的结构。
3. **落定字符合并**：只有尾巴需要逐字符，已经清晰的部分可以按词或按行合并成一个 `Text`。这样 item 数从 O(字符) 降到 O(词 / 行)，同时还能恢复词内 kerning，排版更好。这是结构性改动，但收益最大。
4. **共享时钟**：让 Bubble 用一个 16 ms 定时器驱动所有字段，或至少把“窗口不可见就停止”加进运行条件。这减少的是定时器数量，不减少 O(n) 绑定，属于第二步之后的优化。

### P0-2 动画期间逐帧修改原生窗口尺寸

这是体验上最可能在低端机出现掉帧的地方，也是本次调查里最容易被忽略的一处。

- **Bubble 反馈行**：反馈行高度 0 → `actions.height + 25` 用 220 ms 动画（[BubbleVerdictRow.qml:29-40](src/app/qml/components/BubbleVerdictRow.qml#L29)），而 Bubble 的窗口高度是内容高度（[Bubble.qml:43](src/app/qml/Bubble.qml#L43)）。也就是说，鼠标在气泡上悬停一次，就会在 220 ms 内逐帧改大约 13 次原生窗口尺寸；窗口接近屏幕边缘时，`onHeightChanged` 还会逐帧调用 `keepOnScreen()` 移动窗口（[Main.qml:483](src/app/qml/Main.qml#L483)）。这是全应用最高频的动画。
- **YearWords 调色层**：`paletteProgress` 180 ms 动画（[YearWordsPopup.qml:59-61](src/app/qml/YearWordsPopup.qml#L59)），Loader 高度跟着进度走（[YearWordsPopup.qml:346-347](src/app/qml/YearWordsPopup.qml#L346)），窗口根高度又由内容决定（[YearWordsPopup.qml:67-68](src/app/qml/YearWordsPopup.qml#L67)）；`holdCorner` 在每次高度变化时改 x/y（[Main.qml:315-324](src/app/qml/Main.qml#L315)、[Main.qml:584-585](src/app/qml/Main.qml#L584)）。
- **WordsPopup 筛选**：行高动画（[WordsPopup.qml:356-371](src/app/qml/WordsPopup.qml#L356)）会改变列表高度，窗口高度绑定在内容上（[WordsPopup.qml:72](src/app/qml/WordsPopup.qml#L72)），同样逐帧改窗口并 `holdCorner`（[Main.qml:577-578](src/app/qml/Main.qml#L577)）。

这些窗口都是无边框、透明、always-on-top。逐帧 `SetWindowPos` / resize 会让合成器重新分配窗口表面，QML 场景也要重排；在 125% 缩放、多显示器或集成显卡上容易出现帧节奏问题。视觉上还会出现“内容在动、窗口边缘也在动”的双重运动。

建议：

1. **先量**：用 `qml-metrics.ps1` 分别跑“悬停气泡反馈行”“打开 / 关闭年度调色层”“切换单词筛选”，看帧时间和窗口几何事件，不要凭感觉改。
2. 如果确认是瓶颈，再考虑“窗口一次到位、内容在窗口内裁切动画”的方案：打开时先把原生窗口设到最终高度，卡片 / 列表只在内部用 clip + translate 长出来。但这会让透明区域参与命中测试，可能吞掉下面的点击，需要用输入 mask 或调整窗口的可交互区域；如果 mask 也要逐帧改，又回到原点。这是一次真正的架构取舍，适合记 ADR。
3. Bubble 的反馈行还有一个更保守的选项：保留高度动画，但把 `keepOnScreen` 的逐帧移动合并到动画结束或每两帧一次，先解决“窗口位移 + 尺寸同时变”的叠加。不要简单地取消高度动画，UI.md 明确要求高度 + 透明度。
4. 在测量结果出来之前，不建议直接重构；这类改动很容易把“看起来更顺”换成“点不到下面的东西”。

### P1-1 玻璃主题的 15 Hz 常驻采样

[glass_backdrop.cpp:60](src/app/glass_backdrop.cpp#L60) 的 `kLiveIntervalMs = 67`，在 [glass_backdrop.cpp:330](src/app/glass_backdrop.cpp#L330) 设成定时器。只要有任何玻璃表面可见，它就以约 15 Hz 读取桌面、4 倍降采样、送图、重算模糊。桌面完全静止时，这 15 次采样得到的是同一张图。

建议做自适应：

- 对降采样后的图做一个便宜的采样 / 哈希；连续 2–3 帧没变化就把频率降到 4–5 Hz，或者暂停；检测到变化、窗口移动、拖拽时立刻回到 15 Hz。
- 这样玻璃对桌面变化的响应最多晚 200–250 ms；在重度模糊下基本看不出来，但能省掉大量空闲 CPU 和电池。
- 窗口隐藏时已经停止，这一点不用改。

这是我认为性价比第二高的性能项：实现不复杂，收益稳定，风险主要是“玻璃更新变慢”的观感，需要真机看一眼。

### P1-2 宠物的 50 Hz 时钟

[pet_controller.cpp:40](src/app/pet/pet_controller.cpp#L40) 的 `kTick = 20 ms`，[pet_controller.cpp:140](src/app/pet/pet_controller.cpp#L140) 启动，[pet_controller.cpp:735-736](src/app/pet/pet_controller.cpp#L735) 每 tick 推进状态机并同步。宠物数据是 1–12 fps，50 Hz 意味着大量 tick 没有任何帧变化。ADR 0023 明确接受这个设计，并说“若性能不够，先调整 tick 长度”：

> 窗口可见时，计时器约每 20 ms 唤醒一次，即每秒 50 次。若性能不够，先调整 tick 长度，不换方案。

所以不建议把它当成 bug。正确做法是先测宠物可见时的空闲 CPU：

- 如果占比可忽略，保持现状；
- 如果有明显成本，按动作的 `fps` 调度下一帧（例如 idle 5 fps 就 200 ms 一次），只在拖拽或需要连续锚点更新时回到 50 Hz。状态机的 `advance(kTick)` 时长语义需要一起改，属于 ADR 级改动。
- 不要为了省电直接把它降到 10 Hz，动作切换和眨眼会变得不均匀。

### P2 其余轮询与效果

- **DropdownField 的 16 ms 轮询**：[DropdownField.qml:124-131](src/app/qml/components/DropdownField.qml#L124) 在列表打开期间每秒调用约 60 次 `placeList()`，代码里已标了 `ponytail`。改成滚动位置 / 窗口移动信号驱动即可，影响面小，但打开下拉时是实打实的每秒 60 次 JS + 几何计算。
- **屏幕布局 250 ms 轮询**：[Main.qml:431-436](src/app/qml/Main.qml#L431) 永远在跑，每秒 4 次唤醒。可以换成 QScreen 的 geometry / devicePixelRatio / 虚拟桌面变化信号（需要在 C++ 侧转发）。成本很小，但属于“永远不该有的空闲唤醒”。
- **气泡倒计时 100 ms**：[Bubble.qml:212-223](src/app/qml/Bubble.qml#L212) 每 100 ms 减一次，但显示只用整秒（[Bubble.qml:625](src/app/qml/Bubble.qml#L625)）。可以拆成“1 秒显示定时器 + 到期的一次性定时器”，悬停暂停时保留剩余毫秒。省 9 次 / 秒的唤醒，属于小优化。
- **拖拽定时器**：ShadowCard、SelectionBar、Bubble 的 16 ms 拖拽轮询是必要的，文件里已经记录了为什么不能用 `startSystemMove()` 或 handler 的 translation（[ShadowCard.qml:82-131](src/app/qml/components/ShadowCard.qml#L82)）。不要动。
- **MultiEffect 阴影 / 图标**：ShadowCard 用纯形状做阴影源、避免文字被重采样，这个设计是对的。Icon 的 tint 每个图标一个离屏效果，设置页图标多时值得用 `qml-metrics.ps1` 看一眼 overdraw，但优先级低于上面几项。

## 四、怎么验证

不改代码也能先把“该不该改”测清楚：

1. **动效观感**：`powershell -File scripts/qa/ui-run.ps1` 启动，按 `docs/QML.md` 的方式做 visual QA。动画不能靠快照验证，仓库现有测试也只断言 token 用量和减少动效为 0（[docs/QML.md:259-260](docs/QML.md#L259)）。
2. **逐帧性能**：用 `scripts/profiling/qml-profile.ps1 -Scenario` + [ProfileScenario.qml](src/app/qml/ProfileScenario.qml) 跑现有场景；再补三个针对性步骤：长答案流式揭示、悬停气泡反馈行、年度调色层开合。`qml-metrics.ps1` 看帧时间 / overdraw，`parse-qml-trace.py` 看热点。
3. **已有基线的启示**：现有自动化基线里最大的 Creating 热点是托盘语言列表的首次创建（约 344 ms 的捕获耗时），而不是逐帧动画；这说明“打开时卡一下”和“动画不顺”是两类问题，不能用一个淡入动画去掩盖创建成本。另外这份基线还没有可靠的逐帧百分位，所以本文的性能结论都要先用上面的场景补测（[基线文档](docs/metrics/runtime-performance/qml-automated-scenario-baseline-2026-10-03.md)）。
4. **改动的测试范围**：按 AGENTS.md，纯动效改动只需要最窄的 QML 测试（`test/qtest/components/tst_motion.qml`、`tst_shadowcard.qml`）加一次 visual QA，不需要全量跑。本次是调查，没有跑测试。

## 五、如果只做三件事

1. **给 Bubble 和 SelectionBar 补进出场，并让 Notice / UpdateCard / RemovalQuestion 对称淡出。** 这是“软件是否丝滑”感知最强、风险最低的一步。
2. **把 UI.md 已经规定的按压反馈补齐到所有真正的按钮上。** 不需要新设计，只需要覆盖率。
3. **先测 RevealText 长答案和气泡反馈行的原生窗口 resize，再决定是否做增量布局 / 窗口尺寸重构。** 这两处是最可能真正影响帧率的，而不是看起来最花哨的。

最后，玻璃的 15 Hz 空闲降频和宠物 50 Hz 的测量可以并行做，都是低风险的资源优化；设置页转场、hover 过渡和更多装饰动画则不建议加入，它们会和这份产品克制的性格相冲。

---

本次调查没有修改任何文件，也没有运行测试。
