# 0196 主题切换是值表的交叉渐变

- **状态**：accepted
- **背景**：主题一直是整表替换：设置一变，每个窗口的 `TokenTable` 立刻换成新表，全部表面同帧跳变；进出玻璃主题时，整块材质瞬间出现或消失。读者在设置里点一下，视觉上像被换了一张图。
- **决定**：渐变发生在值表层。`TokenTable` 记住离开的表（`fadeFrom`）与进度（`mix`），一条 `NumberAnimation` 把 `mix` 在 `Tokens.motion.theme`（240 ms，减少动效时为 0）内从 0 走到 1，`tokenColor()` 逐通道插值。令牌名与语义不变，组件依旧不感知主题（DESIGN.md 的 Same Names Rule）。卡片材质同步：`ShadowCard` 与 `BubbleCard` 的底色各带一条 `Behavior on color`；`GlassSurface` 用 `drawnMode` 把上一份材质留到淡出结束，并把玻璃整体淡入淡出。皮肤预览窗（`ThemePreview`）把 `themeFade` 置 0，悬浮比较仍然瞬时（DESIGN.md：hover 无过渡）。
- **后果**：
  - 否决了 “整窗截图交叉淡入”：它要在主题落表前抓旧帧（异步，还得推迟主题生效），玻璃的读屏与排除抓屏时序会被搅在一起，测试也不稳定。
  - 令牌表不能直接给每个颜色挂 `Behavior`：QML 不允许 `Behavior` 挂在只读属性上（探针用例报 “Invalid property assignment ... is a read-only property”）；去掉只读换动画会削弱值表的纪律，最省事的写法因此出局。
  - 依赖条件而非令牌的角色仍会瞬替：`body`（玻璃取第一档文字）与 `radiusCard`（18 ↔ 22 px）没有中间值可插。
  - 选择动作条与复制抽屉自绘卡片、未加底色行为：点开设置前，点外那一下已把它们收起，实际不会在切换时在场；将来要在场，照 `BubbleCard` 补一条。
  - 玻璃的幕布仍由 `GlassHost` 在换主题时立刻重取或清掉，淡入淡出的是材质本身（底色、描边与着色器输出）；模糊的退场落在卡片底色升起的那段渐变里。
  - 已经带 `Behavior on color` 的控件（chip、分段、开关、下拉）会在渐变期间追着令牌走，落定比表慢一条 `press` 的长尾。
