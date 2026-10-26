# 0117 一次抛掷由一个抛掷 module 拥有

- **状态**：accepted（2026-10-26）
- **背景**：飞行几何（`PetFlight`）与落地连击（`PetThrowMood`）都已有纯逻辑与测试，但把两者串起来的规则仍散在 2612 行的 `PetController` 里：`resetThrowMood()` 在 14 处被调、`angryRepeatsLeft_ = 0` 另写 6 处，`updateFlight` / `landFlight` / `updateLandingAngry` 三个函数共享 `flight_`、`throwMood_`、`angryRepeatsLeft_` 三个成员，必须彼此对得上。ADR 0092 的中断规则只存在于这些调用点的排列里，生气连播没有进程内测试，唯一端到端用例要开真窗口并最长等 6 秒。读码还发现两处与 0092 相悖：飞行中来一次外部学习事件会走 `handle()` → `endFlight()`，在 `endFlight` 里把整段连击清掉，而 0092 写明外部学习事件不抹掉已完成的落地；`setMotionReduced(false)`（把减少动效关掉）也清空连击，而 0092 的中断列表里没有这一项。
- **决定**：新增 `src/app/pet/pet_throw.{h,cpp}`，一个无窗口、无时钟、无 QObject 的 `PetThrow`。它拥有一次抛掷从松手到空闲的全部状态：飞行、落地连击与生气连播；`PetFlight` 与 `PetThrowMood` 仍是它内部的纯逻辑部件，各自保留自己的几何与计数。
  - 骰子从外部注入（`core::pet::RandomSource`），每次完整落地抽一次；生气判定、连击窗口与 3 次连播都由 module 自己结算。
  - `begin()` 收下松手位置、速度、窗口与工作区，速度不够时返回 false（放下）；该调用只丢弃尚未播完的生气连播，不重置连击。
  - 中断按种类进 `stop(ThrowStop)`：接住、放下、点击、改大小、换宠、开关宠物、预览/场景、屏幕变化、窗口消失、开启减少动效都从新一次连击开始；`Held`（在平地上重新拿起）与 `Learning`（外部学习事件）保留已完成的落地，只丢掉还没开始的生气连播。0092 的那张中断表只在这里写一次。
  - `tick(step, canPlay)` 吃时间与 “此刻能否起播一个角色”，吐 `ThrowStep`：窗口下一步的左上角、是否刚落地、该播哪个角色。落地那一步点名 `Dizzy`；适配层可以换成 `ClickReact`，并调用 `landingReacted(dizzy)` 告诉 module 这只宠物有没有真的晕眩——只有晕过的那次落地可以把生气排进连播。没有 `angry` 动作时适配层调用 `dropRepeats()`，与 0092 的 “命中后什么也不播” 一致。
  - `PetController` 退为 adapter：`endThrow()` 负责把窗口停在原地、给状态机补 `DragEnd`、按需要把落点记为 home，再按种类停掉 module；`updateThrow()` 只做三件事——把 module 说的位置写给窗口、落地时收尾、把 module 点的角色交给状态机。
- **后果**：
  - `resetThrowMood()` 与 6 处直接清零消失，14 处调用点各写一个中断种类；0092 的规则集中在 `pet_throw.cpp` 一处，生气连播第一次有进程内用例。
  - 两处疑似缺陷随之修好：飞行中的外部学习事件不再抹掉已完成的落地（`ALearningEventKeepsTheCompletedLandings` 钉住）；关闭减少动效不再清空连击。顺带对齐 0092 的 “改设置打断”：打开设置预览若正有一次抛掷在飞，现在会把宠物放在当前位置，而不是让它继续飞。
  - `pet_throw_mood_test` 由 `pet_throw_test` 取代：13 个直写用例加 10 个按中断种类参数化的用例，共 23 例，全部是注入骰子的值测试；`Main::test_aThrowLandsAndStaysThere` 的端到端用例保留，仍断言落点不动与 `dizzy` 的播放。
  - 被排除的方案：把编排留在控制器、只给三个函数补测试，中断规则仍散在调用点；把 `PetThrowMood` 扩成抛掷 module，会让它同时拥有几何与角色决定，名字不再诚实；把 module 放进 `core`，则要为核心搬一份 Qt 值类型，而 0090 的几何与 0092 的计数本就在 `app/pet`。
- **ADR 关系**：0090 的飞行几何与 0091 的晕眩回退不变；0092 的中断、连播顺序与 20 秒窗口成为 module 的行为，不再是调用点的约定；0114 / 0116 的 “纯逻辑 module + adapter” 在宠物这里再落一处。
