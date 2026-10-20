# 0075 原生句柄只在窗口上屏之后取

- **状态**：accepted
- **背景**：宠物的方形窗口本该只画出精灵、其余透出桌面，实测整个 128 × 128 DIP 的区域合成成纯黑，评审牌与答案卡是同一族写法。
  排查留下的事实：
  - 贴图与场景图的透明通道完好；同一 Qt 6.9 用最小 QML 复现同样的 flags 和 `color: "transparent"`，透明正常；再套上
    1 × 1、`opacity: 0` 的宿主窗口层级，仍然正常。
  - 换软件渲染、禁用 QML 与 RHI 磁盘缓存、去掉 `setDefaultAlphaBuffer(true)`、注释掉 1.6.5 加的
    `releaseHiddenSurfaces()`、从外部重下一次黄黑键 `SetLayeredWindowAttributes`，全都仍然发黑。
  - 探针给出关键一条：`Component.onCompleted` 走到 `Pet.attachWindow()` 时窗口 `visible=false`，而
    `excludeFromCapture()` 与 `setPassthrough()` 都经 `handleOf()` 调 `QWindow::winId()`。句柄一取，平台窗口就被创建
    出来；在首次上屏之前创建的 QQuickWindow 拿不到 Qt 到显示时才配好的透明呈现，透明像素就成了黑。
- **决定**：`PetController` 加 `onShown()`：窗口已在屏上就立刻执行规则，否则等 `visibleChanged` 第一次为真再执行。宠物的
  禁止截屏与点击穿透、评审牌和答案卡的 `keepOutOfCapture()` 都走这条路；`setEnabled()` 与 `setPassthrough()` 只在窗口
  可见时改样式，其余留给下一次上屏。
- **后果**：
  - 禁止截屏（ADR 0038、0041）从 “窗口一存在就生效” 退成 “窗口第一次上屏才生效”，中间那一帧宠物可能被截到。黑方块与
    一帧可见之间选了后者。
  - 规则每次上屏重跑一遍：`SetWindowDisplayAffinity` 与样式位都幂等，重跑不为修正，只为窗口被重建时不丢。
  - 未解的是 “什么时候开始坏的”：这条路径自宠物诞生（`118cabc`）就是这样，1.7.0 前后两个沾边的改动都单独排除过，翻转点
    在仓库之外——系统、驱动或 Qt 运行库对 “显示前创建的原生窗口” 的处理变了。所以这里记的是 “不要再在上屏前取句柄”，
    不是 “哪一次改动弄坏的”。
