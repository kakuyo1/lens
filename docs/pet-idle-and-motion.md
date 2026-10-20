# 宠物待机节奏：分析与实施记录

- **状态**：已实施（2026-10-16），决定成为 `docs/adr/0048`。文中的 “四只” 是当时的随包名册；现在随包七只，规则不变。
- **范围**：需求一——宠物待机时动作随机一点，不要固定 3 分钟直接睡觉
- **曾经的第二个需求**：同一轮里还做过 “整个软件的动画更丰富一点，重点在交互反馈”，**已整轮回退**（判定：引入多处缺陷与严重性能下降）。那次的分析、决定与记录一并删除，`UI.md`、`docs/QML.md`、`Motion.qml` 与全部表面都回到改动前的样子。本文件只谈需求一。
- **证据基线**：`main` 上的 `0288975`

## 1 现状（读码所得，未实机计时）

状态机在 `src/core/pet/pet_state.h`，推进者是 `PetController` 里一个 **20 ms** 的 `QTimer`（`src/app/pet/pet_controller.cpp:39`、`127-128`、`579-583`），每跳调一次 `advance(20ms)`。随机源是注入的 `std::mt19937`（`pet_controller.cpp:164-167`），可测。

改动前的实际时间线：

1. 停在 `idle` 循环里，等 `idleWait`。`nextIdleWait()` = 20 s + `random()` × 20 s，即**均匀分布 20–40 s**（`pet_state.cpp:172-176`，常数 `kIdleCooldownBase` 在 `pet_state.h:94`）。
2. 到点从 `idlePool` 里随机挑一个单次动作播放，**只排除刚播过的那个**（`pet_state.cpp:157-170`）。
3. 单次动作放完回到 `idle`，重新抽一次 20–40 s 的等待。
4. 与上面并行，`quiet` 从最后一次真实事件起一直累加；到 **180 s 整**（`kQuietBeforeYawn`，`pet_state.h:92`，`pet_state.cpp:74-79`）请求 `yawn`，哈欠的 `returnTo` 是 `sleep`（`data/pet/*/animations.json`），于是进 `sleep`。
5. `sleep` 的 `loop` 是真（同上），所以 `advance()` 里那句 `not loop and elapsed >= durationOf(...)` 永不成立——**睡着以后没有任何自发的醒来路径**，只有事件能叫醒它（`pet_state.cpp:92`）。

所以读者的体感正是 “随机动几下，然后 3 分钟一到就睡死”：随机的部分够随机，但**睡眠是一个固定常数触发的、且不可逆的悬崖**。

## 2 真正的问题：默认宠物没有动作可随机

待机池是每只宠物手工写在 `animations.json` 的 `idlePool` 里的。四只随包宠物的实际情况：

| 宠物 | 总动作 | 被角色占用 | 自由动作（只能随机播） | 手写 `idlePool` |
|---|---|---|---|---|
| **dog（`pet_store.h:47` 的默认值）** | 12 | 11 + `blink` | **0** | `look_around`, `stretch` |
| cat | 17 | 11 | 5：groom / knead / pounce / scratch / tail_flick | 7 |
| mochi | 17 | 11 | 5：bounce / hiccup / shiver / spin / wobble | 7 |
| owl | 17 | 11 | 5：flap / hoot / hop / nod / preen | 7 |

小狗的池子是 2 个，加上 “不重复上一个” 的规则，**实际是 look_around ↔ stretch 严格交替**，没有任何随机可言。它总共 12 个动作，11 个被角色占用，第 12 个是 `blink`——**它压根没有第 13 个动作可以随机播**。

三只猫科的池子本来就是 “5 个自由动作 + look_around + stretch”，所以池子的自动扩充（§3 D）对它们零影响，是为小狗与未来的新动作准备的。

## 3 另外两个既有缺陷

- **P1 哈欠会被随机动作吞掉**。`advance()` 在请求哈欠**之前**就把 `quiet` 清零（`pet_state.cpp:75-77`），而随机动作是用 `Priority::Situational` 播的（`pet_state.cpp:83`），与哈欠同级。若 180 s 那一刻正在播一个随机动作，`play()` 里的 `requested <= priority` 直接丢弃，那次哈欠**整次丢失**，要再等 180 s。窗口约 1 s / 40 s ≈ 2.5%。这条是读码推理，未实机复现。
- **P2 优先级与规格不符**。`docs/history/PHASE3.md:88` 写的顺序是 “拖动 > 单击 > 学习事件 > 情境动作 > 随机待机”，随机待机应当在**最低**一档；代码却给了它 `Situational`，与哈欠、张望平级。P1 是这条不符的直接后果。

删掉安静计时器之后，发生冲突的那段代码整个消失，所以 **P1 不是被修好，而是不存在了**；P2 也不需要一个新档位（见 §4 E）。

## 4 方案

**A。删掉安静计时器。**待机不再通向睡眠。`quiet`、`kQuietBeforeYawn` 与 `advance()` 里那一段整段删除。

**B。打哈欠改成抽签的结果。**每次待机等待到点时先掷一次骰子，`random() < kNapChance`（**1/12**）就 `request(Role::Yawn, ...)`，否则从池子里挑一个动作播。哈欠的 `returnTo` 已经是 `sleep`，这段数据不用改。

**C。睡眠可自醒。**进 `sleep` 时抽一个 `napFor`（**90–300 s**），到点 `settle()` 回 `idle`。要点：

- `settle()` 已经会走 “回 pendingReturn → 回 held → 回 idle” 这条链，醒来天然走第三条。
- `held == Role::Sleep` 时**不**自动醒——每日预算暂停用的也是 `sleep`（`pet_state.cpp:63-68`），那个睡眠是读者明确要求的。
- `sleep` 的 `loop: true` 保持不变；`advance()` 里 Sleep 的自醒判断放在那句 `not loop` 之前并直接返回。

**D。池子自动扩充。**装载时把 `Playbook.idlePool` 补全为 `idlePool ∪ {没被任何 roles 占用的单次动作}`（去掉保留名 `blink`，且只收单次动作，否则会得到一个永远结束不了的动作）。**格式与校验都不变**，`idlePool` 仍是必填、仍只能列单次动作（`pet_config.cpp:207-217`），宠物包不受影响。以后往 `animations.json` 里加动作，它自动进池子，不必手改 `idlePool`。

**E。P2 不新增优先级档。**计划里曾写 “给 `Priority` 补一档低于 `Idle` 的 `Ambient`”，落地时发现它没有用处：请求只在 `currentRole == Role::Idle` 时发出，那一刻不可能有别的动作在播，所以随机动作由 `enter()` 直接进入——空闲计时器本来就占着 `Idle` 这个状态，它不需要在阶梯上排位。少一档枚举比多一档更接近规格。

**F。节奏旋钮。**等待区间从均匀 20–40 s 放宽到 **20–90 s**；不重复从 “排除最近 1 个” 改成 “排除最近 2 个”。实现上要小心一处：排除的数量取 `min(记忆长度, 池子大小 - 1)`，否则池子不比记忆长时会把候选清空、宠物从此再也不动。配合 1/12 的哈欠概率，平均约每 11 分钟打一次盹。

**G。小狗新增 3 个动作。**`wag`（摇尾）、`sniff`（低头嗅）、`scratch`（挠）。三者都沿用小狗既有的表达方式——同一个 `standing` 姿势 + 每帧表情索引 + 逐帧锚点位移 + 一层小标记，不新画轮廓：

| 要改的文件 | 改什么 |
|---|---|
| `scripts/pet/dog/manifest.json` | 加 3 条：sheet 路径、帧数、表情索引、锚点、可选 effect；并把这 3 个名字加进每个配饰的 `supportedActions` |
| `scripts/pet/dog/build-assets.py` | `BODY` 表加 3 项指向 `standing`，`EFFECTS` 表加 3 个画标记的函数 |
| `data/pet/dog/animations.json` | `actions` 加 3 条（`loop: false`，否则装载会被拒） |
| `data/pet/dog/anchors.json` | 加 3 条，逐帧三层的位移；由 `manifest.json` 的 `anchors` 镜像而来 |
| `data/pet/dog/accessories.json` | 同上，三个名字进每个配饰的 `supportedActions`；不加，戴着的配饰在新动作里会消失 |
| `data/pet/dog/sprites/` | 跑 `build-assets.py` 重渲，`sync_sprites.py` 核尺寸 |

原先还打算加第 4 个 `nod`（打瞌睡点头），实施时发现它与 `sniff` 在小狗身上读起来几乎一样（都是低头 + 闭眼），去掉。`wag` / `sniff` / `scratch` 加上原有的 `look_around` 与 `stretch`，小狗的待机池正好五个自由动作，与其余三只相同。

按 D，这 3 个动作**不需要**写进 `idlePool`——它们没被任何角色占用，会自动进池。

## 5 不做

- 不做 “心情 / 精力” 模型、不做动作权重表。池子 2–7 个动作，加权只会让调参变复杂而不改变体感。
- 不让宠物对减少动效开关有反应。`docs/adr/0023` 已经定过：宠物始终播放全部动作，两者互不干涉。

## 6 影响面

| 文件 | 改动 |
|---|---|
| `src/core/pet/pet_state.h/.cpp` | 删 `quiet` 与 `kQuietBeforeYawn`；加抽签、`napFor`、最近两个的不重复窗口、池子补全 |
| `src/core/pet/pet_config.h/.cpp` | 池子补全（`idlePool` ∪ 非角色单次动作）；格式校验不变 |
| `data/pet/dog/*` + `scripts/pet/dog/*` | 3 个新动作（§4 G） |
| `PRODUCT.md` 常驻伙伴 | 待机改成“抽签才睡 + 可自醒” |
| `GLOSSARY.md` 桌面宠物 | “动作池” 补上自动收录；新增“打盹” |
| `docs/pet-package-format.md` | `idlePool` 一条补上装载时的自动补入 |
| `docs/pet-art-pipeline.md` | §2 末尾记下“小狗的动作是什么构成的” |
| `docs/history/PHASE3.md` | §3.2 那一行与 §3.3 的随机待机一条标注修订 |
| `docs/adr/0048` | 本次取舍 |
| `test/googletest/unit/pet_state_test.cpp` | 三个字面量用例重写；新增“抽签才睡”“打盹会自己醒”“预算的睡眠不自醒”“池子不会把自己饿死” |
| `test/googletest/unit/pet_config_test.cpp` | 池子补全的三条（非角色单次动作收录、循环动作不收、被角色占用的不收） |
| `test/googletest/unit/pet_data_test.cpp` | 小狗的动作数与池子 |

## 7 实施期间可调的旋钮

等待区间 20–90 s、不重复窗口 2、打哈欠概率 1/12、打盹时长 90–300 s。四个都在 `src/core/pet/pet_state.h` 的顶层常数里，改它们要连带改 `pet_state_test.cpp` 里对应的字面量。

## 8 闲逛、边缘躲藏与躲藏举牌（原型已定，应用待接入）

2026-10-20 的后续需求：待机不再只站在原地，宠物要按自己的步态在放置点附近走动；走到屏幕左右边缘后可能躲进去偷看；躲着时复习牌仍能举起。原型在 `ui-prototypes/pet-prototype/walk.html`，应用层接入计划在 `PET-WANDER-INTEGRATION-PLAN.md`，决定见 `docs/adr/0075`。

### 8.1 七只步态

| 宠物 | 步态 | `walk` | 观感 |
|---|---|---|---|
| 小狗 | `trot` | 6 帧 @ 8 fps | 左右腿交替抬起的小跑，落脚扬尘 |
| 麻薯 | `hop` | 6 帧 @ 9 fps | 压扁／拉伸的连续小跳，粉色落地痕 |
| 猫头鹰 | `hop` | 6 帧 @ 8 fps | 收腿、展翅、落地影子 |
| 猫 | `pounce` | 6 帧 @ 7 fps | 低身倾斜、快速前扑，落脚扬尘 |
| 幽灵 | `float` | 6 帧 @ 6 fps | 慢速漂移，效果层带风痕 |
| 水母 | `float` | 6 帧 @ 5 fps | 伞体 pulse，tentacle 压缩，气泡 |
| 云 | `float` | 6 帧 @ 5 fps | 膨胀漂移，两侧雾痕 |

- `walk` 是循环动作，身体负责上下起伏；窗口只负责横向路径与很小的纵向补充。
- 两步之间 120–300 秒，只在真正 idle 时计时；动作、阅读、睡眠、举牌、拖动或指针停在宠物上都作废本次计时。
- home 是读者最后放下的位置；活动范围是窗口宽度的 10–60%，不跨屏，不把闲逛位置写回设置。

### 8.2 边缘躲藏

- 一步结束贴住当前屏工作区左右边缘时，70% 概率进入躲藏。窗口按墨迹边界滑出，只留约 34% 可见，循环 `edge_peek`，窗口每 3.6 秒向内探约 30 px。
- 没有超时，没有自动醒来；单击不结束，只有读者拖动并把宠物拉回工作区内一段才结束。拖不够会缩回。
- 屏幕被移除或工作区变化时取消躲藏并把窗口夹回安全位置；关闭或切换宠物也结束躲藏。改大小保留躲藏并重算偏移。
- 躲藏时暂停悬浮与注视；牌子自己的小卡不受影响。

### 8.3 躲藏举牌

- 复习的可用条件从 `idle` 扩展为 `idle` 或躲藏；躲藏不阻塞提问，也不被提问结束。
- `ReviewAsked` 让宠物演 `ask`，窗口留在边缘；牌子按现有的左右手与内外侧规则选内侧手。
- 回答或 90 秒超时后回到 `edge_peek`，躲藏继续；读者拖出时躲藏结束，问题跟到新位置。
- 真机验收重点：窗口只露一小部分时，握杆的手是否还在牌子末端；不在时优先让窗口在 `ask` 期间临时向内让一点，不新增 edge-ask 美术。

### 8.4 仍未应用

本节记录的是已确认的原型行为；`src/core`、`src/app` 与 `UI.md` 的接入尚未实施。接入顺序、数据模型与测试见 `PET-WANDER-INTEGRATION-PLAN.md`。原型的等待被压缩为 8–28 秒，真实目标仍是 2–5 分钟。
