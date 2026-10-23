# 0094 宠物的脾气进数据：animations.json 的 behavior 块

- **状态**：accepted（2026-10-24）
- **背景**：待机间隔、打盹与场景概率、生气阈值、闲逛等待与步数、躲藏概率、注视距离，一直是散在代码里的常数（ADR 0048、0049、0076、0092 各画了一个）。换一只宠物只是换皮：七只随包宠物的作息、胆量与耐心完全相同。宠物包生态最有杠杆的一步，是让包作者能写出性格。
- **决定**：
  - **`animations.json` 增加可选的 `behavior` 块**：`idleBaseMs`、`idleSpreadMs`、`sceneChance`、`napChance`、`angryClicks`、`angryWindowMs`、`wanderWaitMinMs`、`wanderWaitMaxMs`、`strollSteps`、`hideChance`、`gazeRadius`。**每个字段单独可选**，缺哪个用哪个的缺省；整个块缺省时行为与今天逐字节一致。
  - **缺省值即原常数**。机器侧常数（待机、两掷、生气）上移为 `lens::core::pet` 的命名空间常数，`BehaviorSpec` 的成员缺省引用它们；`kHideChance` 与闲逛等待的正源一并移入 core，`PetWander` 与 `HoverSense` 改为注入参数。一个名字只有一处定义。
  - **校验从包格式惯例**：越上限 `TooLarge`，类型错或越下限 `BadAnimation`。时间字段 0–600 000 ms（`idleBaseMs` 下限 1 000），概率字段 0–1，`angryClicks` 1–10，`strollSteps` 1–4，`gazeRadius` 严格大于近圈与出圈余量之和（20）且不超过最大画布 128，`wanderWaitMinMs` 不得大于 `wanderWaitMaxMs`。
  - **七只随包宠物各一套**，写在 `scripts/pet/recipes/<id>.json` 里，渲染器原样透传到 `animations.json`：狗热切少睡、猫嗜睡常躲、幽灵胆小几乎必躲、猫头鹰沉稳少动、果冻一点就炸、麻薯绵软、云飘忽但积了雨就翻脸。
  - **`pounce` 步态仍只走一步**。一步一跃是步态的语义，不是脾气；`strollSteps` 只放宽其余步态的上限。
- **后果**：
  - 包格式 v1 只增不改：旧包照常装载，不 bump 版本、不做迁移（§10 的边界依旧）。
  - 状态机构造器加了 `BehaviorSpec` 与 `MoodSource` 两个带缺省的参数，旧调用点与旧测试一字未动。
  - 情绪（ADR 0093）缩放的是 `behavior` 里的基础概率，二者是乘法关系：白天段与性格各管一头，互不覆盖。
  - 概率字段允许 0：一只从不打盹的包是合法性格，不是配置错误。
