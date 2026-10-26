# 0118 一次边缘躲藏由一个躲藏 module 拥有

- **状态**：accepted（2026-10-26）
- **背景**：ADR 0076 与 0095 的躲藏是一整段序列：滑出、偷看、指针靠近时多探入 12 px、提问时停在隐藏位、单击引出、拖出或回弹、窗口或屏幕变化后重算隐藏位。它由 `PetController` 里一个 14 字段的 `Hiding` 结构与 49 处 `hiding_` 引用拼出来；`PetWander` 只测到隐藏位、滑动与偷看偏移这些几何，序列本身没有进程内测试。`6c9748c` 给控制器加了 155 行，测试只加了 19 行几何。
- **决定**：新增 `src/app/pet/pet_hide.{h,cpp}`，一个无窗口、无时钟、无 QObject 的 `PetHide`，拥有整段躲藏。
  - 输入是事实：`begin()` 收边、起点、窗口尺寸、工作区、屏幕设备名与按屏缩放后的偷看墨迹；`tick(step, idle, pointerNear)` 收时间、状态机是否空闲与指针是否靠近露出的那一小条；`dragStart()` / `drop()` 收拖拽，`lureOut()` 收单击，`ask()` 收提问，`refit(area, …)` 收窗口或屏幕变化，`stop()` 收关闭或换宠。
  - 输出是值：`Step { position, loopPeek, left }`——窗口去哪、偷看循环是否该播、引出是否已到落点；`Ask { position, left }` 与 `Drop { pulledOut, snapBackTo }` 把提问和释放的两条分支各收成一次回答。
  - 指针距离不直接进 module：控制器用 `HoverSense` 量到可见 sliver 的观察结果（`watching()`）交进来，近 / 远的滞回仍只有 `HoverSense` 一个主人；module 只拥有 12 px 的探入斜坡。
  - `PetWander` 仍是它的几何 seam：`hiddenBase` / `slideAt` / `peekOffset` / `draggedOut` 不改，单测照旧。控制器只做窗口写入、状态机 ambient、屏幕查找与 home / 设置的写入。
- **后果**：
  - 14 字段的 `Hiding` 从控制器头文件消失，49 处 `hiding_` 引用变成对 `hide_` 的少量查询；`pet_hide_test` 的 13 个值用例覆盖滑出、左右偷看、指针探入与收回、提问停在原位、引出到起点、引出中提问、拖出结束、短拖回弹、改尺寸、换屏、忘记，以及偷看循环的时机。
  - 行为不变，两处边界照旧：引出落点按当前尺寸夹紧并写 home；屏幕消失时躲藏结束、宠物回到安全工作区。
  - 被排除的方案：把序列留在控制器、只给窗口加一条端到端用例，仍要真窗口且等 6 秒；把序列塞进 `PetWander`，会让闲逛几何同时拥有窗口状态与状态机时机；把窗口、状态机或 `QScreen` 放进 module，它就不再是值进值出的纯逻辑。
- **ADR 关系**：0076 的躲藏行为与 0095 的注视 / 引出 / 拖出规则成为 module 的行为；0066 的悬浮语义不变（`HoverSense` 仍决定近 / 远与反应）；0097 的提问侧眼仍由 `updateHover` 用 module 的 `area()` / `side()` 量出 sliver。
