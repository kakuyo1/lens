# 桌面宠物美术流水线：配方、渲染与换画风

本文说明桌面宠物的美术怎样生成，有两条路。

- **新宠物**（麻糬 `mochi`、猫头鹰 `owl`、短耳猫 `cat`、小幽灵 `ghost`、小水母 `jelly`、小云 `cloud`）：写一份配方 `scripts/pet/recipes/<id>.json`，运行 `scripts/pet/render_pet.py <id>`，输出直接写进 `data/pet/<id>/`。第 5 节说明步骤。
- **小狗**（1.2.0 重绘）：第 3 节记录那次实际走过的步骤，脚本在 `scripts/pet/` 与 `scripts/pet/dog/`，输出在 `data/pet/dog/`。第 4 节是那次的经验。

范围只包括**外观**：形状、眼睛、嘴与配饰的位置。动作的帧数、帧率、锚点和数据结构不在本文范围内。改它们要改配方（新宠物）或 `data/pet/dog/*.json`（小狗），并跑测试（见 `docs/history/PHASE3.md` §3.1、§3.8）。

## 1。先定的约束

这些约束由渲染器与应用的加载检查决定，不满足就会在启动时被拒绝或画错位置。

- **画布 88 × 88**，每帧一格。`sprites/` 里所有 sheet 的宽度是 “帧数 × 88”，高度 88。表情 sheet 是一排 88 × 88 的格子。`render_pet.py` 写完后读回每张 sheet 核对尺寸；`scripts/pet/sync_sprites.py` 核对小狗的尺寸。两者的规则都与 `src/app/pet/pet_assets.cpp` 相同。
- **网格**：格子 `(x, y)` 的左上角在画布上是 `(30 + 3x, 30 + 3y)`，每格 3 × 3 像素。新宠物与小狗用同一映射。
- **锚点**：每帧的 head、face、body 锚点是这一帧姿势造成的位移，单位是画布像素，并且对齐到整格（3 的倍数）。身体 sheet 含形状，以及 `bodyPaint` 着色的格子（压扁与拉伸已经画在形状里），平移由 body 锚点给出；表情层由 face 锚点移动；每件配饰由自己槽位的锚点移动。待机的第 1 帧是基准，锚点为 0。
- **配饰**：新宠物每只 9 件，头、脸、身各 3 件；小狗 7 件。每件按待机姿势画一次，之后只靠锚点移动。
- **原创**：形状只能来自仓库里的配方或源文件，不能从任何外部图片采样。
- **小狗的几何**（只适用于小狗）：狗盒 18 × 17 格，狗盒左上角在画布（24，24），网格从狗盒的（6，6）开始。配饰的 `pixels` 用狗盒单位，0 是狗盒左上角，可为负，表示头顶的空间；`rects` 用格子，平移时位移必须是 3 的倍数。眼睛两格、相距 3 格，嘴在眼下。

## 2。文件地图

| 文件 | 作用 | 怎么改 |
| --- | --- | --- |
| `scripts/pet/recipes/<id>.json` | 新宠物的配方：身体、眼睛、嘴、角色、每个动作的各帧姿势，以及配饰 | 复制 `mochi.json` 后改；字段写在 `render_pet.py` 的文件头 |
| `scripts/pet/render_pet.py` | 渲染器：读配方，写出 `data/pet/<id>/` 的全部文件，并核对尺寸 | 只在换了**结构**（比如新增一种姿势字段）时才改 |
| `data/pet/<id>/` | 应用直接读取的输出：`pet.json`、`animations.json`、`anchors.json`、`accessories.json` 与 `sprites/` | 不手改，改配方后重新运行 `render_pet.py`；没有暂存副本 |
| `scripts/pet/recipes/original-dog.json` | 小狗的形状配方，可复现 1.2.0 的网格 | 复制后改数字 |
| `scripts/pet/dog/sprite.json` | 小狗的网格源：`head`、`body`、`eyes`、`mouth`，以及配饰的 `pixels` / `rects` | 用 `apply_grid.py` 写入；配饰用 `refit_accessories.py` |
| `scripts/pet/dog/manifest.json` | 小狗每个动作的 sheet 路径、帧数、表情索引，以及配饰的资源与 `supportedActions` | 一般不改；动作的帧数改了要同时改 `animations.json` |
| `scripts/pet/dog/build-assets.py` | 小狗的渲染器：读上面两个文件，画出全部 PNG，直接写入 `data/pet/dog/sprites/` | 只在换了**结构**（比如去掉某个姿势）时才改 |
| `data/pet/dog/sprites/` | 小狗的应用读取位置，由 `build-assets.py` 直接写入 | 不手改 |
| `scripts/pet/scenes/<id>.json` | 场景的逐帧道具（原型 `scenes.js` 的机械搬运）与每帧宠物位移 | 改道具、帧数或对齐点时改；`render_scenes.py` 的输入 |
| `scripts/pet/render_scenes.py` | 场景渲染器：读 `scripts/pet/scenes/`，写 `data/scenes/<id>/{back,front}.png` 与运行时目录 `data/scenes/scenes.json`；读回核对尺寸并检查道具不出画布 | 只在换了场景数据结构时改 |
| `data/scenes/` | 应用自带的场景：随包宠物共用，不进宠物包格式 | 不手改，改 `scripts/pet/scenes/` 后重跑渲染器 |
| `ui-prototypes/pet.html` | 原型设置页，内嵌了全部 PNG 的 base64 | `cd ui-prototypes/pet-prototype && npm run embed` 重新生成 |

渲染器里还有几处**写死的几何**，换形状后要逐帧看一遍（只适用于小狗的 `build-assets.py`）：

- `sleep()` 里的鼻子与微笑坐标 `face_pixels`，是按老狗的脸画的。
- `BOOK` 的位置 `(-13, 14, 4, 36)` 是书本的狗盒单位框，不随狗的形状移动。
- 各动作的姿势函数（`standing`、`sleep`、`pokes`、`thought` 等）都直接用 `sprite.json` 的网格，所以换网格后姿势会跟着变，但它们没有为新形状做单独调整。

**小狗的动作不是逐帧画出来的**，是同一个 `standing` 姿势配三样东西：表情层的每帧索引、逐帧锚点（整只位移）、一层可选特效。加一个动作因此不需要新轮廓——`manifest.json` 的 `actions` 加一条、`build-assets.py` 的 `BODY` 表指向 `standing`（要特效就在 `EFFECTS` 表里加一个画标记的函数）、`data/pet/dog/animations.json` 加同名一条、`anchors.json` 由 `manifest.json` 的 `anchors` 镜像而来。别忘了 `data/pet/dog/accessories.json` 的 `supportedActions`：不加，戴着的配饰在新动作里会消失。2026-10-16 按这条路加了 `wag` / `sniff` / `scratch` 三条待机动作。

## 3。小狗的流程（1.2.0，脚本在 `scripts/pet/` 与 `scripts/pet/dog/`）

脚本命令都在仓库根目录运行。需要 Pillow（`python -I` 下要能 import PIL）。

**第 0 步：备份旧版。**把现有的 `scripts/pet/dog/sprite.json` 与 `data/pet/dog/sprites/` 复制到 `assets-private/<名字>/`，它会被忽略，不进仓库。

**第 1 步：得到新网格。**二选一：

- 用配方：复制 `scripts/pet/recipes/original-dog.json`，改其中的形状（`ellipse` 用中心 `cx, cy` 与半径 `rx, ry`，`rect` 用 `cols` 与 `rows`，都是闭区间），然后预览：
  `python -I scripts/pet/grid_from_shapes.py recipe.json`。满意后输出到临时文件：
  `python -I scripts/pet/grid_from_shapes.py recipe.json --out "$TEMP/grid.json"`。
- 手绘：直接编辑 `scripts/pet/dog/sprite.json` 的 `head` 与 `body`（每行 18 个字符，17 行）。

**第 2 步：写入源。**先预览差异，确认只改了四段，再写入：

```sh
python -I scripts/pet/apply_grid.py "$TEMP/grid.json"           # 只显示差异
python -I scripts/pet/apply_grid.py "$TEMP/grid.json" --write
```

**第 3 步：眼睛与嘴。**眼睛两格、相距 3 格，中点尽量接近身体中线（见第 4 步的输出）。嘴在眼下，两端对齐眼睛。直接改 `eyes` 与 `mouth`，或在第 4 步用 `--eyes` 设置。

**第 4 步：配饰对位。**先看质心，再选位移：

```sh
python -I scripts/pet/refit_accessories.py          # 只报告
python -I scripts/pet/refit_accessories.py --dx-head 12 --dx-face 15 --dx-body 8 --write
```

报告会打印头、身体的质心与眼睛中点（单位）。`--dx-head` 移动帽子、毛线帽、嫩芽、王冠；`--dx-face` 移动两副眼镜；`--dx-body` 移动领结。1.2.0 的取值是 12、15、8，依据见第 4 节。

**第 5 步：渲染并核对。**

```sh
python -I scripts/pet/dog/build-assets.py
python -I scripts/pet/sync_sprites.py
```

`build-assets.py` 直接写入 `data/pet/dog/sprites/`。`sync_sprites.py` 把另一份渲染结果写到临时目录，核对每个 sheet 的尺寸，再列出每个 PNG 与 `data/pet/dog/sprites` 是否相同，不改任何文件。尺寸不对会直接报错，不会进入下一步。

**第 6 步：看图。**用复合预览逐个动作、逐帧检查，不要只看一张：

```sh
python -I scripts/pet/compose_check.py "$TEMP/check.png" --action study --frame 2
```

然后用 Read 工具打开图片。预览的第一格是裸狗加表情，其余每格加一件配饰。重点看：眼睛是否落在框内、配饰是否贴着头、领结是否在脖子下面、睡觉与书本两帧。

**第 7 步：不需要复制。**`build-assets.py` 已经写进 `data/pet/dog/sprites/`，应用读的就是它。

**第 8 步：原型页与原型测试。**

```sh
cd ui-prototypes/pet-prototype
npm run embed
node --test test/*.test.js
```

**第 9 步：应用测试。**尺寸与层序由 `lens_gtest_unit` 的 `pet_data_test` 检查，其中 `data/pet/` 下每个宠物都必须能被应用整体接受；桌面宠物的加载与配饰由 `lens_qtest_pet` 检查，设置页由 `lens_qtest_surfaces` 检查。`scripts\build\build.bat` 会自动跑 qml-lint。

**第 10 步：对比原型与设置页。**运行 `scripts/qa/pet/pet-capture.mjs`，看它输出的动作对比图。只能确认两边画得一致，不能确认好看，好看要人来判断。

**第 11 步：文档。**如果形状或配饰的描述变了，同步 `UI.md`、`PRODUCT.md` 与 `GLOSSARY.md`。

## 4。1.2.0 的经验

- **质心不在画布中心。**1.2.0 的头质心在第 31.0 单位，而画布中心在第 32.5 单位，相差半格。这是因为耳朵的形状不完全对称。配饰按画布中心对齐（即 +12 单位），而不是按质心对齐，因为戴在头上的东西要看整张图的中线。换形状后，先看报告再决定位移。
- **位移回放。**用 1.2.0 的旧备份作为起点，依次执行第 2 步与第 4 步（`--dx-head 12 --dx-face 15 --dx-body 8 --eyes "7,3 10,3"`），得到的 `sprite.json` 与仓库里的 1.2.0 版本的 JSON 内容相同（文本排版可能不同）。这说明流程是可复现的。
- **不要用通用的 JSON 序列化重写 `sprite.json`。**它的格式是人手写的：网格每行一行，坐标数组单行，配饰每件一行。通用序列化会把每个数字展开成一行，diff 无法阅读。`apply_grid.py` 与 `refit_accessories.py` 都只替换需要改的段落。
- **整数单位。**`rects` 的位移不是 3 的倍数时脚本会报错。原因是格子只能整格移动。
- **原型 `pet.html` 是生成物。**它内嵌了 `data/pet/` 里的 PNG。改了数据却不跑 `npm run embed`，设置页里仍然是旧图。

## 5。新宠物的流程

### 配方里有什么

配方只描述形状与姿势，其余文件由渲染器算出。

- `body`：身体的基础椭圆，单位是格子。它的底部中心是所有姿势的轴，除非配方另外给了 `pivot`（姿势轴）与 `head`（头锚点）；四只定稿宠物都显式给了这两个点。
- `silhouette`：可选的像素轮廓，`{shape: "mask", at, rows}`，`#` 是身体内部。它按姿势缩放，用来画一组椭圆/矩形拼不出的形状（水母触手、猫耳、云的宽顶）。同一份配方里的每一帧都用它。
- `bodyPaint`：身体着色格。配方级是默认值，动作或单帧可以覆盖。压扁/拉伸时它是显示空间，生成器按姿势重算（短耳猫、小幽灵、小水母与小云的 `sleep` 就是这样）。
- `facePaint`：表情层上的固定像素，随 `face` 锚点移动；短耳猫的胡须用它，因为在轮廓之外，不能进身体层。
- `eyes`、`mouth`、`yawnMouth`、`angryMouth`：表情层的格子，表情图案 `EYE_PATTERNS` 与小狗共用。`angryMouth` 是生气那一格的撇嘴，缺省时沿用普通嘴。
- `roles`、`idlePool`：原样写进 `animations.json`。
- `hover`：可选。`{notice, dwell, noticeDelayMs, dwellDelayMs, cooldownMs}` 原样写进 `animations.json`，供指针靠近时的招呼与完整反应使用；两个动作名必须是配方已有的单次动作。字段与校验见 `docs/pet-package-format.md`。
- `actions`：每个动作的 `fps`、`loop`，以及按帧排列的 `frames`。每帧可以有姿势 `pose`（`sx`、`sy` 压扁或拉伸，`dx`、`dy` 平移）、表情、额外的形状与效果层的图形。`duration`（毫秒）让循环动作到点结束，场景动作靠它收场；`bodyFrom` 让一个动作复用前面某个动作的 body sheet 与锚点，按自己的帧数循环，生气与场景动作因此不必重画身体，帧里写了 `pose` 就覆盖复用的锚点。
- **表情图案的竖向偏移**：`EYE_PATTERNS` 里第 9 格是生气眼，它的眉画在眼睛上方一个单位，因此图案有四行，由 `EYE_OFFSETS` 把它整体上移一行；右眼用 `EYE_MIRRORS` 把图案左右镜像，眉才向鼻梁一侧斜。嘴按表情查表：哈欠用 `yawnMouth`，生气用 `angryMouth`，其余共用 `mouth`。两份 `EYE_PATTERNS`（`render_pet.py` 与 `dog/build-assets.py`）必须同改。
- `bodyPaint`：身体 sheet 上的着色格子，只画在身体覆盖的格子上。写在配方里时是默认值，写在动作里时每帧都用，写在某一帧里时替换该动作的列表。坐标是画布格子，渲染器不对它做压扁或拉伸，换姿势要在配方里重新算；短耳猫、小幽灵、小水母与小云的睡眠帧都这样生成。猫的粉色耳内与鼻子用它。
- `accessories`：每件配饰的槽位 `slot`、不能穿的动作 `except`，以及它的图形。

锚点与每件配饰的 `supportedActions` 由渲染器从上面的字段算出，不需要在配方里再写一遍。`blink` 动作照常写在 `actions` 里（3 帧，只有表情没有 sheet）。完整字段见 `render_pet.py` 的文件头。

### 步骤

1. 复制 `scripts/pet/recipes/mochi.json` 为 `scripts/pet/recipes/<id>.json`，改身体、眼睛、嘴、动作与配饰。
2. 从仓库根目录运行 `python -I scripts/pet/render_pet.py <id>`。渲染器先写出文件，再读回每张 sheet 核对尺寸；尺寸不对会报错退出。
3. 看图。用 Read 工具打开每张身体 sheet、效果 sheet、表情 sheet 与配饰 PNG，按原尺寸看；可疑的格子截图放大看。发现问题就改配方，不改输出，然后重新运行第 2 步。
4. 核对数据契约。渲染器只核对尺寸，下面几条由应用加载时检查，不满足会被拒绝（见 `src/core/pet/pet_config.cpp`）：
   - `roles` 覆盖 11 个核心角色，只有伸懒腰可以省；
   - `idlePool` 只列单次动作，即 `loop` 为假的动作；
   - `returnTo` 指向 `roles` 里已映射的角色；
   - 有 `blink` 动作，3 帧，表情为 `[1, 2, 1]`；
   - 每个动作的锚点条目数等于帧数。
5. 应用侧的选择（设置页的 `pet` 键）见 `docs/adr/0031-pet-picker-from-shipped-pets.md`。

## 6。脚本索引

| 脚本 | 作用 | 会改文件吗 |
| --- | --- | --- |
| `scripts/pet/render_pet.py` | 新宠物：配方 → `data/pet/<id>/` | 直接写入，没有预览模式 |
| `scripts/pet/grid_from_shapes.py` | 小狗：配方 → 网格；打印 ASCII 预览 | 只在给 `--out` 时写临时文件 |
| `scripts/pet/apply_grid.py` | 小狗：网格 → `scripts/pet/dog/sprite.json` 的四段 | 只在给 `--write` 时写 |
| `scripts/pet/refit_accessories.py` | 小狗：报告质心，按位移对位配饰，可设眼睛 | 只在给 `--write` 时写 |
| `scripts/pet/dog/build-assets.py` | 小狗：渲染全部 PNG 到 `data/pet/dog/sprites/` | 直接写入 |
| `scripts/pet/render_scenes.py` | 场景：`scripts/pet/scenes/` → `data/scenes/` | 直接写入，读回核对尺寸 |
| `scripts/pet/sync_sprites.py` | 小狗：核对尺寸，渲染到临时目录后比对 | 不写文件 |
| `scripts/pet/compose_check.py` | 小狗：一帧加表情与每件配饰的审查图 | 只写指定的 PNG |
| `scripts/pet/recipes/original-dog.json` | 1.2.0 小狗的配方，可复现当前网格 | 复制后改 |
| `scripts/pet/recipes/mochi.json`、`owl.json`、`cat.json`、`ghost.json`、`jelly.json`、`cloud.json` | 六只新宠物的配方，`render_pet.py` 的输入 | 复制后改 |

## 7。已知限制

这些限制来自现有的渲染器，不是某只宠物的问题。改它们要改渲染器，不要只改某只宠物的配方。

- **眼睛是 3 × 3 的方格**，所有宠物共用 `EYE_PATTERNS`；生气那一格向上多出一行画眉。圆眼睛需要改渲染器。
- **效果层不跟随身体**。效果层固定在画布（0，0）处，身体的平移与压扁都不影响它。挂在身体上的图形要放进 `bodyPaint`，不要放进效果层；短耳猫的胡须是表情层（`facePaint`），它跟 `face` 锚点走，但也不随身体压扁。
- **轮廓蒙版只能写格子**。`silhouette` 的像素与 `bodyPaint` 都在配方里手工维护；用生成器改形时先看一遍 `sprites/reference/bare.png`。
- **配饰只能用格子画**，没有圆形或多边形图元，圆框眼镜是手绘的格子。
- **压扁小于 1 时，形状画出的耳朵会脱离身体**，所以新宠物的压扁只用 1 或更大的值。

## 8。举牌的手（`ask` 动作）

`ask` 动作里有一只握着杆的手（ADR 0052）。手画在效果层（`effect` sheet）上，不改身体轮廓；杆与牌由 UI 绘制，不进美术。

- **位置**：右手在身体右侧缘，左手在左侧缘，都在胸口一行（拳头的顶行在画布第 48 px，握点在第 51 px），与身体边缘重叠 3 px。身体在 `ask` 里右移 3 px，所以右手的右缘最多到画布第 87 px：右手贴住身体时会多盖住几列身体，不能超出画布。左手是右手的镜像，两份分别画。
- **跟随身体**：效果层不跟身体锚点，所以每一帧手的格子要按该帧的身体锚点平移。`ask` 的 `dx` 与 `dy` 同样作用在手上，手随身体上下起伏。小狗由 `scripts/pet/dog/build-assets.py` 的 `hand()` 读 `manifest.json` 的 `ask` 锚点完成这一步；其他三只写在配方每一帧的 `paint` 里。
- **握法**：9 × 8 像素的拳头，两根手指从上包住杆子，中间一列留空让杆子穿过，杆末端在第 3 行埋进拳头。墨线与指节是同一个轮廓推出来的，颜色用 `ink` 与 `fur`。
- **配色**：取该宠物已有的调色。不新画身体轮廓，只加这只手。
- **现状**：左右两只手都已写进 `scripts/pet/recipes/{cat,ghost,jelly,cloud,mochi,owl}.json` 的 `ask` 帧（右手 `paint`，左手 `paintLeft`）与 `scripts/pet/dog/build-assets.py` 的 `hand(n, side)`，并渲染进 `data/pet/*/sprites/effects/`：`ask.png` 是右手，`ask-left.png` 是左手。`render_pet.py` 见到 `paintLeft` 就写出 `effectLeft`，狗的 `manifest.json` 与 `animations.json` 也已对应。原型与设置页都按空间选左右手，默认左手。应用在装载时读取 `effectLeft`，逐帧量出两只手的握点，并按手挂牌（ADR 0052、0053）。
