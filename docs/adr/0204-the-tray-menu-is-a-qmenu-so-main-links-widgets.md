# 0204 托盘菜单是 QMenu，main 因此链接 Widgets

- **状态**：accepted
- **背景**：应用界面全部是 QML，唯一常驻可见物是托盘图标及其菜单。Qt 6 给 QML 用的平台菜单类型（`Qt.labs.platform` 一系）仍标着 experimental，托盘是产品永远在线的入口，不能踩实验品。
- **决定**：`main.cpp` 链接 QtWidgets，用 `QApplication` 启动；托盘菜单用 `QMenu` 构建，不用 QML 的 Menu 类型。
- **后果**：一个 QML 应用背上了 QtWidgets 依赖；换来托盘菜单走在成熟实现上。将来若换成 QML 菜单，托盘交互要整个重写。
