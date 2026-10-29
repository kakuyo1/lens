# 宠物包格式 v1

面向第三方作者，也是应用校验实现的依据。应用对宠物包的接受与拒绝以本文件为准。

宠物包是**只含数据**的目录：JSON 文档加 PNG 图片。包里没有代码，应用也不执行包里的任何东西。

## 1。放在哪，怎么被读到

| 来源 | 位置 | 谁写入 |
|---|---|---|
| 随包宠物 | 安装目录的 `data/pet/<id>/` | 开发者，随发布包分发 |
| 宠物包 | `%APPDATA%\Lens\pets\<id>\` | 读者自己 |

- 包目录必须是 `pets\` 的**直接子目录**，不再向下嵌套。
- 应用在**打开设置的扩展分类时**扫描该目录，启动时也扫描一次，用于加载读者上次选的宠物。刚放进去的包在此之前不会被读到。
- 应用对包目录**只读**：不写入、不删除、不改名、不移动。唯一的写入是读者在设置里点 “打开宠物包目录” 时，目录不存在则先创建它。卸载应用不删除 `%APPDATA%\Lens`，包因此还在。
- 读者分享包时把整个 `<id>\` 目录打包成压缩文件即可；**解压由读者自己用系统工具完成**，应用不内置解压（见 `docs/adr/0043` 的后果一节）。

## 2。目录结构

```
pets\<id>\
  pet.json            画布、缩放、图层序、表情 sheet、格式版本
  animations.json     角色到动作的映射、空闲池、每个动作的播放参数
  anchors.json        每个动作每帧的 head / face / body 锚点
  accessories.json    配饰的 id、槽位、图、zIndex、可穿动作、可选场景权重
  sprites\
    body\<动作>.png    身体 sheet，横排，每帧一格，帧间无间距
    effects\<动作>.png 特效 sheet，可选，尺寸与身体 sheet 相同
    expressions\eyes.png 表情 sheet，一排 canvas × canvas 的格子
    accessories\...    配饰单图，每张 canvas × canvas
```

`<id>` 即包名，也是目录名。除上表列出的 `.json` 与 `.png` 外，包里不应有其他文件；其他扩展名不被读取。

## 3。字段

`pet.json`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `format` | 整数 | 必填，本版本为 `1`。缺失或为其他值整只拒绝 |
| `id` | 字符串 | 必填，必须与目录名相同 |
| `name` | 字符串 | 可选显示名。缺省时显示 `id`。**不翻译**；显示前按 §5 第 13 条去掉控制字符并做 NFC 归一 |
| `canvas` | 整数 | 必填，单帧边长（像素） |
| `scale` | 整数 | 必填，默认放大倍数 |
| `layers` | 对象 | 必填，`body` / `expression` / `effect` 三层的 z 值 |
| `expression` | 字符串 | 必填，表情 sheet 的相对路径 |

`animations.json`、`anchors.json`、`accessories.json`、`sprites\` 的字段与随包宠物**完全相同**，含义见 `docs/pet-art-pipeline.md` 与 `docs/history/PHASE3.md` §3.1。要点：

- `roles` 必须覆盖 14 个动作角色，只有 `stretch`、`angry`、`ask` 与 `dizzy` 可以缺省。角色名是产品定义的闭集，包不能新增；缺 `angry` 的宠物永远不生气，缺 `ask` 的宠物照样有提问牌，只是不演那个展示动作（见 `docs/adr/0088`），缺 `dizzy` 的宠物抛掷落地时改演 `click_react`（见 `docs/adr/0091`）。
- `actions` 里每个动作需要 `frames` 与 `fps`，`loop` 必填，`returnTo` 可选但必须指向 `roles` 里已映射的角色。`duration` 可选，单位毫秒：到点后动作结束，循环动作因此也能自己收场（应用自带的场景用它）。
- 必须有一个 `blink` 条目（只有表情，没有 sheet）。
- `idlePool` 只能列单次动作（`loop` 为假）。应用装载时还会把**没被 `roles` 点到的单次动作**补进池子，所以给包加一个新动作就同时给了它一个待机动作；`idlePool` 里写不写同一个名字都行，写了不会重复。循环动作（含带 `duration` 的场景动作）不进池子，只有应用自己的场景抽签能开始它们。
- `hover` 可选，控制指针靠近时播什么：`notice` 与 `dwell` 是本包已有的**单次**动作名，`dwell` 必须与 `notice` 同时出现；`noticeDelayMs`、`dwellDelayMs`、`cooldownMs` 可选，依次默认 350、2600、8000，范围都是 0–60000。没有 `hover` 的包仍有视线跟随，但没有靠近动作；`hover` 至少要给出 `notice` 或 `dwell` 之一。
- `movement` 可选，控制宠物空闲时走几步、贴边时躲不躲：`style` 必填，是 `glide` / `float` / `hop` / `trot` / `pounce` 之一；`walk` 必填，是本包已有的**循环**动作名；`peek` 可选，也是本包已有的循环动作名，缺省表示不躲藏；`range` 可选，是窗口宽度的百分比，范围 10–60，默认 30。没有 `movement` 的包保持原地待机；有 `walk` 没有 `peek` 的包只闲逛、不躲藏。走一步的时长等于 `walk` 的自然时长（`frames × 1000 / fps`）；可见比例与滑动时长是应用常数，不写进包。
- `behavior` 可选，决定这只宠物的脾气（`docs/adr/0094`）。每个字段单独可选，缺哪个用哪个的缺省：`idleBaseMs`（待机等待的基数，默认 20 000）、`idleSpreadMs`（待机等待的抖动上限，默认 70 000）、`sceneChance`（待机回合抽场景的概率，默认 1/6）、`napChance`（待机回合打盹的概率，默认 1/12）、`angryClicks`（几次连击生气，默认 3）、`angryWindowMs`（相邻两次点击的最大间隔，默认 2 000）、`wanderWaitMinMs` / `wanderWaitMaxMs`（两次闲逛之间累计空闲的下限与上限，默认 120 000 / 300 000，前者不得大于后者）、`strollSteps`（一次闲逛的最多步数，默认 2；`pounce` 步态永远只走一步）、`hideChance`（闲逛到屏幕边缘时躲藏的概率，默认 0.7）、`gazeRadius`（眼神跟随的距离，单位是画布像素，默认 26，必须大于近圈 16 与出圈余量 4 之和）。没有 `behavior` 的包保持应用缺省脾气。
- 锚点条目数必须等于该动作的 `frames`。
- 配饰的 `slot` 只能是 `head` / `face` / `body`，`supportedActions` 只能列本包已有的动作。可选的 `sceneWeights` 是 “场景 id → 正数乘子” 的对象：戴上这件配饰后，待机抽景把对应场景的权重乘上该值；摘下即恢复，多个已佩戴件偏爱同一场景时相乘（`docs/adr/0102`）。键必须是本包已有的动作；场景本身属于应用，只有随包宠物有，所以包里的权重目前只被校验与保存，不改变行为（见 §8）。
- 场景权重不是开关：值大于 0、至多 4，一件配饰至多列 8 个场景；缺省或空对象与没有这个字段相同。
- `anchors.json` 只列有 sheet 的动作。`blink` 只有表情、没有 sheet，所以不列锚点；列了即整只拒绝（`BadAnchors`）。

## 4。上限

超限即拒绝整只包。数值为 v1 规范值。

| 项 | 上限 |
|---|---|
| 包目录总大小 | 32 MB |
| 单个 PNG | 8 MB |
| 单个 JSON | 256 KB |
| 文件总数 | 512 |
| `canvas` | 16–128 |
| `scale` | 1–4 |
| 单个动作的 `frames` | 1–64 |
| `fps` | 1–24 |
| `duration` | 1–3 600 000 ms |
| `hover` 的三个延迟 / 冷却 | 各 0–60 000 ms |
| `behavior` 的时间字段 | `idleBaseMs` 1 000–600 000；`idleSpreadMs`、`angryWindowMs`、`wanderWaitMinMs`、`wanderWaitMaxMs` 各 0–600 000 |
| `behavior` 的概率字段 | `sceneChance`、`napChance`、`hideChance` 各 0–1 |
| `behavior` 的计数字段 | `angryClicks` 1–10；`strollSteps` 1–4 |
| `behavior.gazeRadius` | 大于 20（近圈与出圈余量之和），至 128 |
| 动作数 | 32 |
| 配饰数 | 32 |
| 单件配饰的 `sceneWeights` 条目 | 8 |
| `sceneWeights` 的单个权重 | 大于 0，至多 4 |
| `idlePool` 条目 | 16 |
| `movement.range` | 10–60（窗口宽度的百分比） |
| 单张 sheet 的声明宽度 | `frames × canvas`，且不超过 8192 |
| 表情 sheet 格数 | 64 |
| `name` 长度 | 32 个字符 |
| 整包解码后的像素总量 | 16 M 像素（约 64 MB 位图） |

`frames`、`fps`、`canvas` 的上限同时是溢出防线：`frames × canvas` 与 `frames × 1000` 都在 `int` 范围内。

超过上限的拒绝为 `TooLarge`。低于下限或类型不对的，按所在字段归类：`pet.json` 为 `Format`，动作为 `BadAnimation`，锚点为 `BadAnchors`，配饰为 `BadAccessory`。

`name` 先去掉控制字符、双向文本控制符与零宽字符，再做 NFC 归一，然后才数长度。去掉后为空是 `BadName`；超过 32 个字符是 `TooLarge`，不截断。

## 5。校验规则

按顺序执行，任一条不满足即拒绝整只包，不带残缺状态运行。规则编号供测试引用。

**路径与文件**

1. 资产路径只能是相对路径。拒绝绝对路径、盘符相对路径（`C:foo`）、UNC 路径（`\\server\share`）、以及任何 `..` 段。
2. 解析后的**规范路径**必须仍在包目录的规范路径之内。这一条覆盖重解析点：目录交接点（`mklink /J`）与符号链接不需要管理员权限即可创建，压缩文件也能携带链接条目，因此不能只看字符串。
3. 包目录内的路径不得是重解析点（符号链接或目录交接点）。包目录本身（`pets\<id>`）由读者放在那里，可以是链接；它之下的任何条目都不得是。
4. 只接受图片魔数为 PNG 的文件。**不依扩展名判断类型**：`QImageReader` 默认按内容嗅探格式，SVG 等格式会因此被当图片加载，所以必须核对魔数。
5. 每个 JSON 文件解析失败即拒绝。

**数值**

6. `format` 必须存在且为 `1`。
7. `canvas`、`scale`、`frames`、`fps`、动作数、配饰数、`idlePool` 条目、`hover` 的三个延迟、`movement.range` 与 `behavior` 的各字段均在 §4 范围内；`hover` 的 `notice` / `dwell` 必须是本包已有的单次动作，`dwell` 不能脱离 `notice` 单独出现；`movement.style` 必须在闭集内，`movement.walk` / `peek` 必须是本包已有的循环动作；`behavior` 的 `wanderWaitMinMs` 不得大于 `wanderWaitMaxMs`，`gazeRadius` 必须严格大于 20。`sceneWeights` 必须是对象，条目数与每个权重在 §4 范围内，每个键是本包已有的动作。
8. 每张 sheet 的像素尺寸等于 `frames × canvas` 宽、`canvas` 高；表情 sheet 高为 `canvas`、宽为 `canvas` 的整数倍。
9. 每个表情索引（含 `blink` 的表情列表）小于表情 sheet 的格数。
10. 缩放倍数在 1–4 之内。

**标识与显示**

11. `id` 匹配 `^[a-z0-9_]{1,32}$`，且与目录名相同。
12. `id` 与任一随包 id 冲突时拒绝该包（不覆盖随包宠物）。比较不区分大小写，以覆盖 Windows 的不区分大小写文件系统。随包宠物的文件夹名即使校验失败，也仍保留。
13. `name` 去控制字符、去双向文本控制符、去零宽字符后非空，长度在 §4 之内，并做 NFC 归一。含被去掉字符的名字按去掉后的结果显示，不因此拒绝。

**结构**

14. 角色、动作、锚点、配饰的约束同 §3。
15. 动作名与配饰 id 可以自由取名。它们是读者可见的字符串，但不做清洗：含控制、双向或零宽字符，或超过 32 个字符的，整只拒绝（`BadAnimation` 或 `BadAccessory`）。

## 6。拒绝原因

应用界面显示一条本地化说明，**不直接显示包里的字符串**；原始解析信息只进日志。

| 原因 | 含义 |
|---|---|
| `Unreadable` | 目录或文件读不到，或包目录里没有 `pet.json` |
| `Format` | 缺 `format`、版本不认识，或 JSON 格式错误 |
| `TooLarge` | 超过 §4 的任一上限 |
| `BadName` | `id` 或 `name` 不合规 |
| `MissingRole` | 必需的动作角色没有映射 |
| `BadAnimation` | `frames`、`fps`、`loop`、`returnTo`、`idlePool`、`hover`、`movement`、`behavior` 不合规 |
| `BadAnchors` | 锚点缺失或条目数与帧数不符 |
| `BadAccessory` | 配饰字段不合法、id 重复、槽位未知、引用了不存在的动作，或 `sceneWeights` 的键或值不合法 |
| `BadAssetType` | 不是 PNG，或魔数与扩展名不符 |
| `SheetSize` | sheet 尺寸与帧数、画布不符 |
| `ExpressionIndex` | 表情索引越界 |
| `PathEscapes` | 资产路径越出包目录 |
| `IdTaken` | 与随包宠物 id 冲突 |

## 7。安全规则

**包不得做的，也是应用拒绝或忽略的：**

- 不含可执行文件、脚本、DLL 或 QML。应用不加载包内的任何代码。
- 不访问网络。应用不为包发起任何请求。
- 不读取包目录以外的任何文件（第 1、2、3 条）。
- 不通过显示名伪装成随包宠物或应用自身界面。来源标记由应用绘制，包无法影响它的有无与外观。

**应用守住的：**

- 包目录只读。应用不写入、不删除、不改名、不移动。这一条同时防止 “重解析点把一次写入重定向到任意路径”。
- 读者可见的包字符串一律按**纯文本**渲染（QML 侧 `textFormat: Text.PlainText`）。默认的 `AutoText` 会识别 HTML 并渲染富文本，包提供的名字或动作名因此可能插入图片标签或撑破版面。
- 显示名与 id 展示前做第 13 条的字符清洗与长度截断。
- 被拒包的界面说明只用上表的本地化文案，不用包里的字符串。
- 解析在读取前先按文件头与声明尺寸核定，再决定是否解码，避免 “声明尺寸巨大而文件极小” 的图片炸掉内存。
- 一只包失败只影响它自己：其余宠物与程序照常，启动不受阻。

## 8。与随包宠物的差异

| | 随包宠物 | 宠物包 |
|---|---|---|
| 位置 | 安装目录 `data/pet/` | `%APPDATA%\Lens\pets\` |
| 写入者 | 开发者 | 读者 |
| 显示名 | `qsTranslate` 按 id 对照，跟随界面语言 | `name` 去掉控制字符后显示，不翻译 |
| 来源标记 | 无 | 标注外部来源 |
| 首次启用 | 直接启用 | 弹一次确认 |
| 场景 | 有，每只六个，`sceneWeights` 改变抽景 | 无；`sceneWeights` 通过校验与保存，但当前不生效 |
| 缺失或被拒 | 回退到随包默认宠物，不弹通知 | 回退到随包默认宠物，弹一条通知 |
| 校验 | 同一套 | 同一套，另加上限与路径规则 |

## 9。制作一个包

与随包宠物相同：写一份配方，跑渲染器。

```sh
python -I scripts/pet/render_pet.py <id>
```

渲染器写出 `data/pet/<id>/`。把它整个目录复制到 `%APPDATA%\Lens\pets\` 下，再打开设置的扩展分类即可看到。`docs/pet-art-pipeline.md` 是完整流程、约束与经验。

自查（应用加载时仍会再查一遍）：`roles` 覆盖 14 个角色（`stretch`、`angry`、`ask` 与 `dizzy` 可省）、`idlePool` 只列单次动作、`returnTo` 指向已映射角色、`duration` 为正数、`hover` 的动作存在且为单次、`hover.dwell` 不脱离 `hover.notice`、`movement.walk` / `peek` 存在且为循环、`behavior` 的各字段在 §4 范围内、`sceneWeights` 的键存在且值在 §4 范围内、有 `blink` 条目、锚点条目数等于帧数、sheet 尺寸等于 `frames × canvas`、PNG 魔数正确、路径无 `..`。

## 10。已知边界

- 包未经审核、未签名。本规范只保证 “不执行代码 + 契约校验”，不保证包的外观与行为符合预期。场景是应用自带内容，包没有场景；`sceneWeights` 会被接受与保存，但当前没有可倾斜的抽景。
- 规范只到 v1。未来版本会另行编号，未知版本按 `Format` 拒绝，不做迁移。
- 配饰 PNG 的内容不限画法；随包配方的渲染器支持格子、像素与画布像素，但没有圆形或多边形图元。效果层不跟随身体。这些是渲染器的限制，见 `docs/pet-art-pipeline.md` §6。
- 界面显示名的长度上限是 32 个字符，中文字符按一个计。
