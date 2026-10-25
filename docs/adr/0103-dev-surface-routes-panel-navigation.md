# 0103 开发面：surface route 走面板自己的导航进内页

- **状态**：accepted（2026-10-24）。`surface route <name> <target>` 进入 `LENS_ENABLE_DEV_SURFACE` 的命令表。
  其 “滚动注入仍然不做” 的后果由 ADR 0105（`surface scroll`）废止，余下决定不动。
- **背景**：UI 评审要遍历设置面板的七个分类与四个子页拍照（`docs/history/impeccable/critique/`）。0068 的命令表只能开到
  表面入口：`surface show settings` 永远落在目录页，分类与子页只能靠真实点击进去——而点击要么抢读者的指针，
  要么先截图定位坐标，评审的驱动反过来依赖了被评审物自己的像素，这条路不接受。
- **决定**：
  - **路由是状态层的一员，不是输入层**。`surface route settings learning`（分类）或 `general/font`
    （分类/子页）调窗口自己的 `routeTo`，它再调 `openCategory` 与 `openRoute`——目录行与链接行的点击调的
    就是这两个函数，与宠物右键 “先 `showSettings(true)` 再 `openCategory("extensions")`” 是同一条真实路径
    （`Main.qml`）。不移动指针，不注入任何事件。
  - **表驱动**：`surface_table.h` 的 Row 加一列 `route`（窗口方法名，`nullptr` 即无内部导航），dev surface
    据此生成路由表，与 opener 表同构——延续 0084 的 “一张表拥有全部表面” 的规矩。
  - **不新增动词**：`route` 是 `surface` 的子命令，语法动词仍是八个，0068 的十个上限不动。
  - **拒绝要说清，清单不复制**：语法层只校验表面名与 “该表面有没有 route 方法”；分类与子页的清单归 QML 的
    `routeTo`，core 不再抄一份。目标不认识、或面板收着（导航会在下次显示时被重置）→ `ok:false`。
- **后果**：
  - 以后长出内部页面的表面（面板替换、子页返回栈）在表上加一个方法名即可接入，命令表不用改。
  - `routeTo` 是产品 QML 里第一个只服务于驱动的函数；边界是 “只组合既有真实入口”，评审脚本到达的状态必须与
    读者点击到达的完全一致，越界就该删。
  - 悬停态（功能预览、下拉行预览）与滚动注入仍然不做：那是瞬时态或输入层，归 qtest 与 visual-qa，静态截图
    本来也装不下。
