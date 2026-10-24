# 桌面宠物美术流水线：配方、渲染与换画风

七只随包宠物（猫、狗、云、幽灵、水母、麻糬、猫头鹰）都由配方生成。2026-10 的奶糖重做把小狗从 `scripts/pet/dog/` 旧渲染器迁进同一条管线（ADR 0081）；旧脚本、`original-dog.json` 与旧美术已删除，1.2.0 的流程留在 git 历史。

范围只有**外观**：形状、眼睛、嘴、配饰与动作帧的图形。动作的帧数、帧率、锚点与数据结构由配方决定，应用加载时校验；测试见 `TEST.md` 与 `test/googletest/unit/pet_data_test.cpp`。

## 1。先定的约束

- **画布 88 × 88**，每帧一格。`sprites/` 里每张 sheet 的宽度是 “帧数 × 88”，高度 88。表情 sheet 是一排 88 × 88 的格子。`render_pet.py` 写完后读回每张 sheet 核对尺寸，规则与 `src/app/pet/pet_assets.cpp` 相同。
- **网格**：格子 `(x, y)` 的左上角在画布上是 `(30 + 3x, 30 + 3y)`，每格 3 × 3 像素。七只宠物共用这一映射。
- **锚点**：每帧的 head、face、body 锚点是这一帧姿势造成的位移，单位是画布像素，并且对齐到整格。身体 sheet 含形状与 `bodyPaint` 着色；表情层由 face 锚点移动；每件配饰由自己槽位的锚点移动。待机的第 1 帧是基准，锚点为 0。
- **配饰**：七只随包宠物各 9 件，头、脸、身各 3 件，共 63 件；每只的套装配色与轮廓按该宠物画，不再四只共用一整套。每件按待机姿势画一次，之后只靠锚点移动。`supportedActions` 由渲染器从 `except` 算出。
- **原创**：形状只能来自仓库里的配方，不能从外部图片采样。
- **奶糖的几何**：24 列、23 行的蒙版放在配方坐标 `[-5, -6]`；两只 2 × 2 的格子眼在 `[4, 1]` 与 `[9, 1]`，`eyeSize: 2`；粉耳、爪子、项圈与银牌在身体层，鼻头与舌头在表情层；脚点在 `[6.5, 17]`。
- **场景**：应用自带，`data/scenes/`；归属由动作表决定，和宠物包格式无关。每只宠物两个专属、两个共享；每个场景 12 帧、4 fps、30 秒，至少一个中段 beat。

## 2。文件地图

| 文件 | 作用 | 怎么改 |
| --- | --- | --- |
| `scripts/pet/recipes/<id>.json` | 配方：身体、轮廓、眼睛、嘴、角色、每个动作的各帧姿势与配饰；`dog.json` 是奶糖 | 复制 `mochi.json`（椭圆）或 `dog.json`（蒙版）后改；字段写在 `render_pet.py` 的文件头 |
| `scripts/pet/render_pet.py` | 渲染器：读配方，写出 `data/pet/<id>/` 的全部文件，并核对尺寸 | 只在换了**结构**（例如新增一种姿势字段）时才改 |
| `data/pet/<id>/` | 应用直接读取的输出：`pet.json`、`animations.json`、`anchors.json`、`accessories.json` 与 `sprites/` | 不手改，改配方后重新运行渲染器；没有暂存副本 |
| `scripts/pet/scenes/<id>.json` | 场景的逐帧道具与宠物位移 | 改道具、帧数或对齐点时改；`render_scenes.py` 的输入 |
| `scripts/pet/render_scenes.py` | 场景渲染器：写 `data/scenes/<id>/{back,front}.png` 与 `data/scenes/scenes.json`，读回核对尺寸 | 只在换了场景结构或调色板时改 |
| `data/scenes/` | 应用自带的场景，不入宠物包 | 不手改，改 `scripts/pet/scenes/` 后重跑渲染器 |
| `ui-prototypes/pet.html` | 原型设置页，内嵌全部 PNG 的 base64 | `cd ui-prototypes/pet-prototype && npm run embed` |

场景没有自己的 `pets` 字段：专属场景使用全局唯一的 id（如 `dog_frisbee`），只出现在狗的 `animations.json`；共享场景（`skate`、`swing`）给每只宠物保留两个。`pet_data_test` 固定每只为 “两个专属 + 两个共享”，并核对动作帧数、场景帧数与 30 秒时长。

## 3。配方里有什么

- `body`：身体的基础椭圆，单位是格子。默认轴是底部中心；非椭圆轮廓用 `pivot`（姿势轴）与 `head`（头锚点）显式给点。
- `silhouette`：像素轮廓 `{shape: "mask", at, rows}`，`#` 是身体内部；它按姿势缩放，用于椭圆与矩形拼不出的形状。奶糖的耳朵、头与身体都在这一张蒙版里。
- `bodyPaint`：身体着色格，画布坐标。配方级是默认值，动作或单帧可以覆盖；压扁的帧要在配方里把格子重新算过去。奶糖的雾灰耳、粉耳内、爪子、项圈与银牌都在这里。
- `facePaint`：表情层上的固定图形，随 face 锚点移动；奶糖的鼻头与舌头用它，保证和眼睛、嘴一起动。
- `eyes`、`mouth`、`yawnMouth`、`angryMouth`：表情层的格子。默认眼睛图案是 3 × 3 **像素**；`eyeSize: 2` 时用 `SMALL_EYE_PATTERNS` 的 2 × 2 格子（6 × 6 像素）眼睛，奶糖是唯一使用它的宠物。
- `roles`、`idlePool`、`hover`、`movement`：原样写进 `animations.json`；两个 hover 动作必须是已有的单次动作，movement 的 walk 与 peek 必须循环。
- `actions`：每个动作的 `fps`、`loop`，可选 `blink`、`returnTo`、`duration`、`bodyFrom`、`paint`，以及按帧排列的 `frames`。每帧可有 `pose`（`dx`、`dy` 平移，`sx`、`sy` 绕轴压扁或拉伸）、`expression`、`shapes`（额外形状）与 `paint`（效果层图形）。
- `bodyFrom`：复用前面动作的 body sheet 与锚点，按自己的帧数循环；生气、庆祝、场景等因此不用重画身体。复用的是成品 sheet，动作级 `bodyPaint` 不生效；帧里写了 `pose` 就覆盖复用锚点。
- `paint`：效果层图形，支持 `at`、`colour`、`art` 或 `glyph`，以及 `pixel: true`（一字符一像素）。效果层是该动作自己的图形；提问牌不再由宠物拿着，所以没有左右手 sheet 的概念。
- `accessories`：每件 `{id, slot, paint, except?}`。`paint` 可以走格子，也可以走 `pixel`：每字符一个画布像素，起点按 `at` 的格子角换算；`pixel` 再配 `canvas: true` 时 `at` 直接按画布像素计，用于不落在 3 px 网格上的描边。`except` 里的动作不显示这件配饰。
- 表情图案的竖偏移：第 9 格生气眼向上多一行画眉，由 `EYE_OFFSETS` 处理；右眼用 `EYE_MIRRORS` 镜像。嘴按表情查表：哈欠用 `yawnMouth`，生气用 `angryMouth`，其余共用 `mouth`。
- `blink` 动作照常写在 `actions` 里：3 帧，只有表情，没有 body sheet。
- **`dizzy`**：抛掷落地后的可选角色动作，15 帧、6 fps、单次，合 2.5 秒；身体在基础姿势上做 ±1 格、±2–3% 的轻晃，效果层每帧画两颗金色（`gold`）三格星星绕头，星星环每 6 帧转一圈，2.5 秒里转两圈半；表情前 10 帧闭眼、最后 5 帧半睁。它不用 `bodyFrom`：复用 sheet 只会换锚点，身体本身不会晃。金色只属于这个效果，不进宠物身体配色。

## 4。怎么生成与检查

1. 复制 `scripts/pet/recipes/mochi.json` 或 `dog.json`，改身体、眼睛、嘴、动作与配饰。
2. 从仓库根目录运行 `python -I scripts/pet/render_pet.py <id>`。渲染器直接覆盖 `data/pet/<id>/`，先清空 `sprites/accessories/`，退役件不会留下孤图；写完后读回每张 sheet 核对尺寸，尺寸不对会退出。
3. 看图。至少看 `sprites/reference/bare.png`、表情 sheet、每个动作的 body/effect sheet 与配饰 PNG；按原尺寸和放大各看一遍。可疑的格子改配方，不改输出。
4. 核对数据契约：角色、idlePool、blink、锚点条目、场景帧数与 30 秒时长、配饰的 supportedActions。`pet_data_test` 会把狗逐项验一遍，包括 `ask` 只有身体、没有效果层。
5. 原型：`cd ui-prototypes/pet-prototype && npm run embed && npm run embed:hover`；`pet-review.html` 与 `walk.html` 直接读 `data/pet`，不用重新生成。改完狗或动作后，至少打开 `pet-review.html?pet=dog` 与 `walk.html` 看一次。
6. 测试：`lens_gtest_unit --gtest_filter='PetData.*'`；原型侧 `node --test ui-prototypes/pet-prototype/test/*.test.js`。真机看画面另见 `docs/qa.md`。

## 5。脚本索引

| 脚本 | 作用 | 会改文件吗 |
| --- | --- | --- |
| `scripts/pet/render_pet.py` | 全部宠物：配方 → `data/pet/<id>/` | 直接写入，没有预览模式 |
| `scripts/pet/render_scenes.py` | 场景：`scripts/pet/scenes/` → `data/scenes/` | 直接写入，读回核对尺寸 |
| `scripts/pet/recipes/{mochi,owl,cat,ghost,jelly,cloud,dog}.json` | 七只宠物的配方 | 复制后改 |

## 6。已知限制

- **眼睛默认 3 × 3 像素**；圆眼与更大的点眼需要配方级 `eyeSize`，目前只有 2 与 3 两档。奶糖的 2 × 2 格子眼是 6 × 6 像素，和 3×3 的默认图案不共用位置逻辑。
- **效果层不跟随身体**。效果层固定在画布 `(0, 0)`，身体的平移与压扁不影响它；挂在身体上的图形放 `bodyPaint`，跟脸但不受压扁的放 `facePaint`。`dizzy` 的星星因此绕基础头位转，身体在星星下面轻晃；这是刻意的。
- **轮廓蒙版只能写格子**，像素与 `bodyPaint` 都在配方里手工维护；换姿势后先看 `reference/bare.png`。
- **配饰的格子、像素与画布像素**都支持，但没有圆形或多边形图元；细框与描边是 `pixel` 手绘的。
- **压扁小于 1 时蒙版里的耳朵会跟着变**；奶糖的 `sleep` 只用 1.04 / 0.96 的轻压，避免脸与身体错位。
- 小狗没有 `blink` 之外的旧专用动作表；动作名与场景 id 都与旧版一致。配饰 id 在 ADR 0099 中整套换新，旧存档里不认识的配饰会被丢弃。

## 7。步态与边缘偷看

`walk` 与 `edge_peek` 是循环动作，不映射角色，也不能进 `idlePool`。它们用配方的 `body_frame`、逐帧 `pose`、表情与效果层。

- **奶糖**：walk 是 6 帧的轻快小跑，身体上下起伏、左右移半格，脚下有尘土；edge_peek 是 6 帧的 duck 与 look，头与表情锚点一起下移。窗口负责探入、缩回。
- **共享场景**：`skate` 与 `swing` 的宠物动作由各自配方提供；奶糖坐在滑板与秋千上，场景位移负责摆动，帧里只留表情与必要的重心。专属的 `dog_frisbee`、`dog_burying` 由狗单独拥有。
- **配饰**：渲染器会为每件配饰自动补上 walk 与 peek；小狗没有手写 `supportedActions` 了。

## 8。验证与真机

- 数据契约：`PetData.*` 覆盖动作数、角色、idlePool、blink、锚点、场景 2+2、配饰 9（每槽 3）、`ask` 没有效果层与全部 sheet 尺寸。
- 原型：`pet.html` / `pet-hover.html` 是内嵌生成物；`pet-review.html`、`walk.html` 读 `data/pet`。改任何随包宠物后重跑 embed，并打开这两个页面看一次。
- 真机：应用在启动时装载 `data/pet/dog`，正在运行的旧进程不会热换；重启 Lens 后才看得到奶糖。宠物窗口默认不进录屏，拍照用 `docs/qa.md` 的 QA 开关。
