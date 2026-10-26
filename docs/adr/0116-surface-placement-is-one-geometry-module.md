# 0116 表面落点只有一个几何 module

- **状态**：accepted
- **背景**：落点规则是纯算术，却和窗口副作用混在四个 QML 文件里：`SurfaceHost.qml` 的 `placeBeside` / `placeBesidePet` / `placeTransient` / `placeReviewCard` / `keepOnScreen` 自己算位置再写窗口；`ScreenPoints.qml` 按屏原点把原生点换成 DIP；`WordsListRow.qml` 与 `ReviewSignWindow.qml` 又把 DIP 乘回原生像素，且从 0 起乘；`Bubble.qml` 的高度另有一份 `max(预撑高, 内容高) + 两侧阴影`。每条规则只有搭起整个 `Main.qml` 才测得到。三处换算里的两处从 0 起乘，`docs/QML.md` §4 记着它在竖屏上偏 512 DIP，复习卡在那块屏上会落偏。
- **决定**：新增 `src/app/geometry/surface_geometry.{h,cpp}`，一个无状态的 `SurfaceGeometry` QML singleton，收下跨表面的落点算术与按屏 DIP ↔ native 换算：`boundedSize` / `besideAnchor` / `besidePet` / `transient` / `reviewCard` / `keptOnScreen` / `windowHeight` / `heldFloor` / `dipFromNativePoint` / `nativeFromDipPoint` / `dipFromNativeLength` / `nativeFromDipLength`。
  - 输入是事实：锚点矩形、卡尺寸、间距、阴影、工作区、屏幕原点与缩放；输出是四舍五入后的窗口左上角。工作区的远边按 QRectF 的 `right()` / `bottom()`（不含）读，与 QML 原来的 `left` / `top` / `right` / `bottom` 逐像素一致。
  - `SurfaceHost.qml` 退为 adapter：读窗口与屏幕、取工作区、写 `x` / `y` 并 `raise()`；`ScreenPoints.qml` 只找屏与复制 payload；`WordsListRow.qml` 与 `ReviewSignWindow.qml` 的原生点改由 `nativeFromDipPoint()` 从屏幕原点换算；`Bubble.qml` 与 `HeightHold.qml` 的高度规则改读 `windowHeight()` / `heldFloor()`。
  - 模块落在 `app/geometry`，不是 `app/qml/util` 的 JS 库：它跨表面、要进程内值测试，而 `.pragma library` 的 JS 到不了 gtest。它只依赖 Qt Core 的值类型，不读窗口、不问屏幕、不持状态。
- **后果**：
  - `surface_geometry_test` 的 18 个值用例取代了原来要开窗口的落点断言：`besideAnchor` / `besidePet` / `transient` / `reviewCard` / `keptOnScreen` / `boundedSize` / 高度保持 / 原点换算各钉住边界；`tst_main` 保留交接用的 adapter 用例（`placeBeside` 的交接、`ScreenPoints` 把原点交出去、气泡把读者的偏好侧交出去）以及原有的面板增长与路由行为。
  - `WordsListRow.qml` 的复习卡锚点缺陷一并修复：旧代码 `Math.round(at.x * ratio)` 把 DIP 从 0 起乘，屏幕原点不为 0 时偏 `origin * (1 - ratio)`；现在与 `ScreenPoints.dipOn()` 互逆，`surface_geometry_test` 的 `APointConvertsFromItsScreensOwnOrigin` 钉住竖屏那组实测值。
  - `SelectionBar.qml` 的五种形态（`placeAt`）与 `ReviewSignLayout.qml` 的签/卡选边仍是表面自己的规则，不进这个 module：它们各有自己的边与对齐，跨表面共用的只是上面的落点与换算。`glass_math` 的 `toNativeRect` 服务于抓屏，用的原生原点是 Win32 的显示器原点，不是 `QScreen` 的虚拟原点，因此不合并。
  - 被排除的方案：把算术留在 `SurfaceHost.qml` 只加测试，仍要开 `Main.qml` 窗口；放进 `util` 的 JS 库，语法上更近 QML，但 gtest 读不到；把工作区与屏幕查询也搬进 module，会把它从值函数变成需要 `QGuiApplication` 的对象，测试重新需要平台。
- **ADR 关系**：0084 把 “每表面协议” 列为下一步，这是其中落点这一半；0055 的气泡预撑高成为 `windowHeight()` / `heldFloor()` 两条规则；0085 不变——module 只依赖 Qt Core 值类型。
