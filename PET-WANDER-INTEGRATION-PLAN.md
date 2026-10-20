# 宠物闲逛、边缘躲藏与躲藏举牌：应用层接入计划

- **状态**：原型已确认（2026-10-20），应用层未接入。设计决定见 `docs/adr/0075-pet-wanders-hides-and-keeps-asking.md`。
- **原型**：`ui-prototypes/pet-prototype/walk.html`。七只宠物的 `walk` 循环、70% 边缘躲藏、`edge_peek`、拖出、躲藏举牌都在这里跑过。
- **新美术**：`data/pet/<id>/sprites/body/{walk,edge_peek}.png` 与部分 `sprites/effects/{walk,edge_peek}.png`；生成器和动作数据暂时只在原型里（`ui-prototypes/pet-prototype/tools/build-walk-assets.py`、`ui-prototypes/pet-prototype/walk/walk.json`）。
- **不在本计划内**：新的桌面宠物设置项、新的一级界面、自由漫游、把宠物包拆到别的进程。

## 1 已确认的行为

### 1.1 闲逛

- 只在桌面宠物窗口可见、状态机真正空闲时计时。每次进入 idle 重新抽 120–300 秒；任何动作、阅读、睡眠、举牌、拖动、指针停在宠物窗口上都会作废当前这一次。设置预览、宠物关闭时不走。
- 一次闲逛 1–2 步（猫 1 步），每一步播放一次 `walk` 循环，窗口沿步态路径移动；走完停 200–1200 ms（随步态），再决定下一步或结束。
- home 是读者最后放下宠物的位置；活动范围是窗口宽度的 10–60%，随包宠物 24–46%，形状按步态：`float` 纵向更大，`hop` 以横向为主，`trot`、`pounce` 更低。每 tick 的窗口位置都夹进 home 椭圆和当前屏工作区。
- 每步时长等于 `walk` 动作的自然时长：`frames × 1000 / fps`。窗口路径只负责平移和很小的纵向补充；身体的主要上下起伏由 `walk` 动画承担。
- 不跨屏。home 在哪块屏，就在哪块屏的工作区里闲逛；工作区取 `QScreen::availableGeometry()`。
- 闲逛位置不写回设置；`PET/position` 仍是读者放下的 home。重启回到 home。

### 1.2 边缘躲藏

- 一步结束时，窗口正好贴住当前屏工作区的左边缘或右边缘（1 px 容差），掷一次 70% 的签。中签就进入躲藏；没中签就照常停顿、继续下一次闲逛。
- 躲藏时窗口按宠物的**墨迹边界**滑出屏幕，保留约 34% 的身体可见；滑出 260 ms。循环播放 `edge_peek`，每 3.6 秒向内探约 30 px 再缩回。纵向位置留在原处。
- 没有超时、没有自动醒来、没有复习结束躲藏。单击不结束；只有读者拖动并把宠物拉回工作区内一段（窗口至少 80% 在工作区内）才结束。拖动不够就缩回边缘。
- 拖出后以落点为新的 home，按现有拖拽规则保存位置；躲藏位置从不保存。
- 系统事件是例外：屏幕被移除或工作区改变导致宠物无处可藏时，取消躲藏并把窗口夹回安全位置。关闭宠物、切换宠物也结束躲藏；这些是读者或环境的显式变化，不是宠物自行离开。改大小不结束躲藏，按新窗口重算隐藏偏移。
- 躲藏期间暂停悬浮感知与注视；宠物只偷看、举牌或被拖出。复习牌自己的悬浮小卡仍然工作。

### 1.3 躲藏举牌

- 复习提问的可用条件从 “状态机 idle” 改成 “状态机 idle 或正在躲藏”。躲藏不阻塞提问，也不被提问结束。
- `ReviewAsked` 让状态机进入 `ask`；窗口不离开边缘。牌子继续用现有的左右手与内外侧选择：左边缘用内侧的右手，右边缘用内侧的左手；`choose()`、`nudgeFor()` 不改。
- 回答、90 秒超时或关闭后，牌子收起；宠物演 celebrate、encourage、angry（如果该宠物有），然后回到 `edge_peek`，躲藏继续。
- 读者在问题未答时把宠物拖出来：躲藏结束，问题保留，牌子跟到新位置继续。
- 躲藏时手可能被屏幕边缘裁掉。接入时先按现有摆位实现，用真机确认牌杆是否脱手；如果脱手，优先让窗口在 `ask` 期间临时向内让一点，仍不结束躲藏；不新增一套 edge-ask 美术，除非视觉验收证明必须。

## 2 现有代码地基

- [单帧时钟](src/app/pet/pet_controller.cpp)：`PetController::tick()` 每 20 ms 推状态机、悬浮和画面。闲逛与躲藏也必须走这一个时钟，不新增计时器。
- [状态机](src/core/pet/pet_state.cpp)：角色是产品闭集；idle 计时只在 `currentRole == Role::Idle` 时累计。`walk`、`edge_peek` 不映射角色，所以需要一个由控制器播放的 ambient 循环入口。
- [窗口放置](src/app/pet/pet_placement.cpp)：已有 `fitsWithin`、`clampedWithin`、`indexOfNearestArea`，闲逛和隐藏的边界计算复用它们。
- [复习调度](src/app/review_duty.cpp)：提问前检查 `petIdle_()` 并重掷下一次 look；`ReviewAsked` 由[状态机](src/core/pet/pet_state.cpp)进入 `Ask` held。
- [牌子摆位](src/app/qml/ReviewSignLayout.qml)：已经按左右手、内外侧、nudge 选位置；宠物窗口不需要为牌子移动。
- [宠物数据](src/app/pet/pet_assets.cpp)：已有按动作量墨迹和手部握点的工具；躲藏偏移与牌子握点可复用。

## 3 数据模型

### 3.1 新动作

每只随包宠物新增两个循环动作，名字固定为 `walk` 和 `edge_peek`：

| 宠物 | `walk` | `edge_peek` | 步态 |
|---|---|---|---|
| 小狗 | 6 帧 @ 8 fps | 6 帧 @ 5 fps | `trot` |
| 麻薯 | 6 帧 @ 9 fps | 6 帧 @ 5 fps | `hop` |
| 猫头鹰 | 6 帧 @ 8 fps | 6 帧 @ 5 fps | `hop` |
| 猫 | 6 帧 @ 7 fps | 6 帧 @ 5 fps | `pounce` |
| 幽灵 | 6 帧 @ 6 fps | 6 帧 @ 5 fps | `float` |
| 水母 | 6 帧 @ 5 fps | 6 帧 @ 5 fps | `float` |
| 云 | 6 帧 @ 5 fps | 6 帧 @ 5 fps | `float` |

- 两者都是 `loop: true`，都不是角色，也不进 `idlePool`（自动扩充只收单次动作）。
- 动作的帧、锚点、效果和配饰支持规则与普通动作相同；配饰的 `supportedActions` 必须包含这两个名字，否则移动/偷看时配饰会消失。
- 第三方包可以不带这两个动作；不带 `walk` 就不闲逛，带 `walk` 但没有 `edge_peek` 就只闲逛、不躲藏。

### 3.2 `animations.json` 的 `movement` 块

`movement` 可选；存在时：

```json
"movement": {
  "style": "trot",
  "walk": "walk",
  "peek": "edge_peek",
  "range": 32
}
```

| 字段 | 类型 | 规则 |
|---|---|---|
| `style` | 字符串 | 必填，闭集 `glide`、`float`、`hop`、`trot`、`pounce` |
| `walk` | 字符串 | 必填，本包已有的循环动作 |
| `peek` | 字符串 | 可选，本包已有的循环动作；缺省表示不躲藏 |
| `range` | 整数 | 可选，窗口宽度的百分比，10–60，默认 30 |

- 走步时长取 `walk` 的 `frames`、`fps`；节奏、70% 概率和可视比例是产品常数，不写进宠物数据。`style` 只决定路径形状、步数与步间停顿。
- 没有 `movement` 的宠物保持现在的被动待机；不滑行、不躲藏。这是为了不让缺少步态美术的包出现 “整只宠物在冰上滑” 的观感。
- 未知 `style`、循环动作缺失、动作不是循环、`range` 越界都按现有整只拒绝处理；`range` 过大是 `TooLarge`，其余是 `BadAnimation`。
- 格式版本仍是 1；这是可选的新块，旧版本会忽略它。宠物包格式文档在实施阶段补字段、上限与校验规则。

### 3.3 配方与美术源

- 六只渲染宠物：把原型生成器里的 `WALKS`、`PEEKS` 帧搬进 `scripts/pet/recipes/<id>.json`，加 `movement`；`render_pet.py` 原样透传 `movement`，并重渲 `animations.json`、`anchors.json`、`accessories.json` 与 sheet。
- 小狗：在 `scripts/pet/dog/build-assets.py` 增加 `walk`、`edge_peek` 两个动作的绘制分支，更新 `dog/manifest.json`；`data/pet/dog/animations.json`、`anchors.json`、`accessories.json` 是手写源，直接补条目。
- 幽灵的风痕、麻薯的落地痕、云、水母的雾泡都沿用生成器里已验证的帧；重渲后先看 `git diff`，只接受与这两个动作相关的 PNG、JSON 变化。
- 旧的原型生成器在数据迁移后可以删除，或缩成只生成预览的工具；应用与原型都以 `data/pet` 为源，原型的 `walk.json` 不再保留第二份动作数据。

## 4 核心与状态机

### 4.1 解析

- `src/core/pet/pet_config.h/.cpp`：新增 `Style` 枚举、`MovementSpec`，挂在 `Animations` 上；在 `parseAnimations()` 的动作与角色校验之后解析 `movement`。
- 校验必须在动作表建好之后进行，才能确认 `walk`、`peek` 存在且是循环；配饰的 `supportedActions` 仍由既有解析负责。
- `pet_data_test` 增加七只随包宠物的 profile 断言，并断言 “支持 idle 的配饰也支持 walk 与 peek”。

### 4.2 Ambient 循环

状态机新增（命名可调整）：

```cpp
bool playAmbient(ActionName const& name); // 只允许在 idle 时进入，且 name 必须是循环动作
void stopAmbient();                      // 当前是 ambient 时回 idle；已被事件抢占时无动作
```

- 走 `Priority::Idle`，没有角色，`currentRole` 为空；idle 计时因此暂停。悬停要求 `currentRole == Idle`，不会在行走或偷看时插进来。
- 点击、拖动、阅读、举牌、场景等事件按现有优先级抢占；控制器观察到当前动作不再是 ambient，就结束这一步或退出躲藏。
- `walk` 与 `edge_peek` 都通过这个入口播放；躲藏是控制器模式，不是状态机角色。
- 单测覆盖：只允许 idle、只接受循环动作、事件打断、`stopAmbient()` 回 idle、ambient 期间 idle 计时不走。

## 5 应用层

### 5.1 闲逛

新增 `src/app/pet/pet_wander.h/.cpp`：

- 持有 home、范围、步态、随机源、当前窗口位置/尺寸/工作区；`advance()` 返回本 tick 的窗口位置与 “这一步是否结束、是否贴边”。
- 路径与原型一致：`float` 正弦、`hop` 抛物线、`trot` 小颠、`pounce` 冲出、`glide` 直线；纵向只做小补充，主要身体动作由 `walk` 承担。
- 目标点先夹进 home 椭圆，再 `clampedWithin()` 进当前屏工作区；贴边检测只看左右边缘，1 px 容差。
- 控制器在 `tick()` 中按 `machine_->idle()`、桌面窗口可见、非预览、非拖动来决定是否推进；每步开始时 `playAmbient(walk)`，步结束时 `stopAmbient()`。步长与动作时长对齐。

### 5.2 躲藏

`PetController` 持有躲藏状态（建议一个 `std::optional<Hiding>`）：

- `side`（左/右）、隐藏基准 x、当前 peek 相位、滑出进度、是否正在拖动。
- 进入前取 `edge_peek` 第一帧的墨迹边界，按 34% 可见计算基准 x；滑出用 260 ms 缓出；每 3.6 秒余弦探入 30 px。
- 躲藏时 `playAmbient(edge_peek)`；窗口 `setPosition()` 仍走同一 20 ms tick。
- `fitOnScreen()` 在躲藏时跳过；`keepOnScreen()` 遇到屏幕变化时取消躲藏并夹回安全位置；`savePosition()` 在躲藏时不写。
- `dragStart()` 停掉闲逛/偷看；`dragEnd()` 判断落点是否已把窗口至少 80% 拉回工作区：是则结束躲藏并保存落点，否则把窗口放回隐藏基准并继续躲藏。
- 关闭宠物、切换宠物、屏幕移除结束躲藏；改大小保留躲藏并重算偏移。
- 躲藏时跳过 `updateHover()`，并把 gaze 归零；牌子自己的小卡不在宠物窗口里，不受影响。

### 5.3 复习

- `PetController` 增加：

```cpp
bool availableForReview() const; // enabled && (machine_->idle() || hiding_)
```

- [AppController](src/app/app_controller.cpp) 的 `ReviewDuty` 回调从 `pet_->idle()` 改为 `pet_->availableForReview()`；`ReviewDuty` 的构造参数改名并改注释，逻辑不变。
- 提问到来时，状态机从 ambient 进入 `Ask`；窗口保持隐藏位置；`ReviewSignWindow.relayout()` 照常选内侧手。回答/超时后，控制器在反应动作结束、状态机回 idle 时重新 `playAmbient(edge_peek)`，躲藏继续。
- 读者拖出时躲藏结束；如果问题还在，牌子跟到新位置，状态机仍是 `Ask`。
- 不新增 `ReviewDuty` 的 “待复习” 重排：躲藏本身已经允许提问，不需要把 look 留到拖出后。

### 5.4 QML 与交互

- `PetWindow.qml`：按下时新增 `Pet.stopWandering()`；拖拽/松手仍走现有 `dragStart`、`dragEnd`、`savePosition`；窗口在边缘外时可见部分仍能接收鼠标。
- `PetWindow.qml` 的 `onWidthChanged`、`onHeightChanged` 调 `fitOnScreen()`；C++ 在躲藏时跳过。
- `ReviewSignWindow.qml`、`ReviewSignLayout.qml`：不改摆位算法；躲藏时 `layout.figure` 是隐藏窗口的位置，`choose()` 会选内侧手。视觉验收重点看杆是否还握在手里。
- 设置页动作药丸：`walk`、`edge_peek` 要出现在动作列表里，需要在 `SettingsExtensionsPage.qml` 的 `actionLabel()` 加两项，并在四份 `.ts` 补翻译；`edge_peek` 预览只在设置舞台上原地播，不进入躲藏。
- 不做新的开关：闲逛、躲藏与躲藏举牌都是宠物行为的一部分；读者关宠物即可全部停止。

## 6 交互矩阵

| 事件 | 闲逛中 | 躲藏中 |
|---|---|---|
| 点击 | 进入 click_react；闲逛作废 | 不结束躲藏；不播 click_react |
| 拖动 | 进入 pickup；闲逛作废 | 进入 pickup；松手够远就结束躲藏，否则缩回 |
| 阅读/思考/庆祝等学习事件 | Event 抢占；闲逛作废 | 动作播放，躲藏保留 |
| 复习提问 | Event 抢占；停下脚步 | 进入 ask；躲藏保留 |
| 回答/超时 | 照常 | 反应动作后回 edge_peek，躲藏保留 |
| 指针靠近/停在宠物上 | 不开始新的闲逛；已开始的步停止 | 悬浮关闭；不响应 |
| 设置预览打开 | 闲逛暂停 | 桌面宠物继续躲；设置舞台单独演 |
| 关闭宠物 | 停止 | 结束躲藏；再开启从 home 开始 |
| 切换宠物 | 停止 | 结束躲藏；新宠物从当前落点开始 |
| 改大小 | 停止当前步 | 保留躲藏，按新窗口重算偏移 |
| 屏幕移除/工作区变化 | 夹回安全位置 | 取消躲藏并夹回安全位置 |
| 重启应用 | 从 home 开始 | 从 home 开始，不恢复躲藏 |

## 7 测试与验收

### 7.1 单元与集成

| 目标 | 用例 |
|---|---|
| `lens_gtest_unit` / `PetConfig` | `movement` 缺省、合法块、未知 style、缺失/循环动作、range 边界、旧包不受影响 |
| `lens_gtest_unit` / `PetStateMachine` | `playAmbient` 只允许 idle、只接受循环动作、事件抢占、`stopAmbient`、ambient 期间 idle 计时不走 |
| `lens_gtest_unit` / `PetWander` | 五种路径、home 椭圆、工作区夹取、贴边检测、70% 注入随机、拖出阈值、屏幕移除 |
| `lens_gtest_unit` / `PetData` | 七只随包 profile、`walk`、`edge_peek` 存在且循环、配饰支持两者 |
| `lens_gtest_unit` / `ReviewDuty` | 新的可用回调在躲藏时为真、提问不结束躲藏、回答/超时后恢复 peek |
| `lens_qtest_surfaces` 或真机 | 窗口在边缘外的部分可拖、拖出/缩回、牌子内侧手、卡片不被裁 |

### 7.2 真机与视觉

- `powershell -File scripts/qa/ui-run.ps1 -Restart`，观察：七只各走一轮；贴边躲藏；拖出；拖不够缩回；单击不结束；躲藏中触发一次复习（可以临时缩短频率或走开发面），确认牌子在屏幕内、杆不脱手、回答后继续躲。
- 多屏与 DPI：在主屏、副屏、任务栏在左/右/自动隐藏时各看一次；工作区边缘与贴边判断以 `availableGeometry()` 为准。
- 性能：窗口在行走时每 20 ms 移动一次；检查 CPU、闪烁、透明窗口拖动。若闪烁，位置更新降到每两三个 tick，不改帧时钟。
- 减少动效：宠物仍播放 walk、peek，按 ADR 0023 的既有规则；不新开关。

## 8 分阶段实施

1. **数据与美术**：迁移 `walk`、`edge_peek` 到 recipes、dog manifest，补 `movement`，重渲，补配饰支持；解析器与数据测试。验收：`lens_gtest_unit --gtest_filter=PetConfig.*:PetData.*`。
2. **核心 ambient 循环**：`playAmbient`、`stopAmbient` 与单测；控制器暂时不接窗口。验收：`PetStateMachine.*`。
3. **闲逛与躲藏**：`PetWander`、控制器 hiding 状态、窗口移动、边缘检测、70%、拖出/缩回；不含复习。验收：`PetWander.*`，一次 `ui-run` 视觉检查。
4. **复习与牌子**：`availableForReview`、AppController 回调、躲藏 ask、回答/超时恢复 peek、四份翻译与设置动作药丸。验收：ReviewDuty 用例 + 真机举牌。
5. **收尾**：屏幕变化、改大小、关闭/切换、多屏/DPI、性能、文档、ADR、完整套件。

每一阶段结束都可以单独回退；第 3 阶段没有复习接入时，躲藏只影响闲逛，风险最小。

## 9 风险与待确认

- **躲藏举牌的手**：现有牌子摆位不需要宠物窗口在屏内，但手画在宠物窗口里；窗口只露 34% 时手是否可见要在真机确认。若不理想，第一选择是 `ask` 期间窗口临时向内让一点，第二选择才是新 edge-ask 美术。
- **悬浮行为**：本计划选择躲藏时暂停悬浮与注视。若希望靠近边缘的宠物仍回应指针，需要额外定义它如何与 “只有拖出才结束” 共存。
- **第三方包**：本计划选择缺少 `walk`、`movement` 就不闲逛，缺少 `edge_peek` 就不躲藏；不给它们一条通用的滑行路径。若要兼容旧包，需要另定一条 fallback。
- **窗口闪烁**：透明置顶窗口每 20 ms 移动是否稳定，要在真机看；ADR 0053 对牌子窗口的 4 Hz 移动有过担心。闪烁时降低位置更新频率，而不是改帧时钟。
- **多屏**：home 所在屏的边缘、任务栏自动隐藏、DPI 变化；只在当前屏工作区躲藏，不跨屏。
- **数值**：120–300 秒、70%、34% 可见、30 px peek、3.6 秒周期、260 ms 滑出都是初值；视觉验收后可只改常数与数据，不动结构。
- **设置动作药丸**：`walk`、`edge_peek` 会出现在动作列表；若不想暴露，可另做过滤，但需要同时决定翻译与预览行为。

## 10 验收清单

- [ ] 七只随包宠物都有 `walk`、`edge_peek` 与合法 `movement`，配饰支持两者。
- [ ] 旧宠物包、没有 `movement` 的包仍能加载，行为与现在一致。
- [ ] 闲逛只在 idle 发生，动作/阅读/睡眠/举牌/拖动/指针在宠物上时停止。
- [ ] 贴边 70% 进入躲藏，单击不结束，拖出结束，拖不够缩回。
- [ ] 躲藏位置从不写回设置；拖出后的落点写回 home。
- [ ] 躲藏中复习牌可举，选内侧手，回答/超时后继续躲。
- [ ] 屏幕移除、工作区变化、关闭/切换宠物、改大小都有确定行为。
- [ ] 单测、qtest（如需要）、真机视觉与完整套件通过；文档、ADR、翻译同步。

## 附录 A：原型对照

- 步态与 `walk`、`edge_peek` 的逐帧美术：`ui-prototypes/pet-prototype/tools/build-walk-assets.py`。
- 动作数据（最终会并入 `animations.json`、`anchors.json`）：`ui-prototypes/pet-prototype/walk/walk.json`。
- 页面：`ui-prototypes/pet-prototype/walk.html`，`cd ui-prototypes/pet-prototype && npm run dev` 后打开 `/walk.html`。
- 原型里的等待被压缩为 8–28 秒，真实目标是 120–300 秒；页面上的 “边缘演示” 按钮绕过 70% 只为看美术，真实路径仍是掷签。
