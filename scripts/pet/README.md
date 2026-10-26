# 桌面宠物美术脚本

七只随包宠物（猫、狗、云、幽灵、水母、麻糬、猫头鹰）都由配方生成。小狗在 2026-10 的奶糖重做后从 `scripts/pet/dog/` 旧渲染器迁到 `recipes/dog.json`（ADR 0081）；旧脚本、`original-dog.json` 与旧美术已删除。完整流程、约束与经验见 [`docs/pet-art-pipeline.md`](../../docs/pet-art-pipeline.md)。

## 配方到输出

| 文件 | 作用 |
| --- | --- |
| `recipes/<id>.json` | 配方：身体、轮廓、眼睛、嘴、角色、动作的各帧姿势与配饰。`mochi.json` 是椭圆范例，`dog.json` 是蒙版与 2 × 2 眼睛的范例 |
| `render_pet.py` | 读配方，写出 `data/pet/<id>/` 下的 `pet.json`、`animations.json`、`anchors.json`、`accessories.json` 与全部 PNG，并核对每张 sheet 的尺寸 |

从仓库根目录运行（需要 Pillow）：

```sh
python -I scripts/pet/render_pet.py dog
```

它直接覆盖 `data/pet/<id>/`，没有暂存副本，也没有预览模式。

配方的主要字段：

- `body`：身体的基础椭圆；非椭圆形状用 `pivot`、`head` 与 `silhouette` 蒙版。
- `bodyPaint`：身体着色格；`facePaint`：表情层固定图形，随 face 锚点移动。奶糖的鼻头与舌头在 `facePaint`，雾灰耳、粉耳内、爪子、项圈与银牌在 `bodyPaint`。
- `eyes`、`mouth`、`yawnMouth`、`angryMouth`：表情层。默认眼睛是 3 × 3 像素；`eyeSize: 2` 使用 `SMALL_EYE_PATTERNS` 的 2 × 2 格子（6 × 6 像素）眼睛，目前只有奶糖。
- `roles`、`idlePool`、`hover`、`movement`：原样写进 `animations.json`。
- `actions.<名>`：`fps`、`loop`，可选 `blink`、`returnTo`、`duration`、`bodyFrom`、`paint`，以及 `frames`。帧里可有 `pose`、`expression`、`shapes` 与 `paint`。`bodyFrom` 复用前面的 body sheet 与锚点；动作级 `bodyPaint` 不生效。
- `accessories`：每件 `{id, slot, except?, sceneWeights?, paint}`；图形可走格子，也可用 `pixel: true` 走像素；`pixel` 再配 `canvas: true` 时 `at` 以画布像素计。`supportedActions` 由渲染器生成；`sceneWeights` 原样带进 `accessories.json`，渲染器不解释它。

## 场景

`scenes/<id>.json` 是场景的逐帧道具与宠物位移；每个场景 12 帧、4 fps、至少一个中段 beat。`render_scenes.py` 写出 `data/scenes/<id>/{back,front}.png` 与运行时目录 `data/scenes/scenes.json`，并读回核对尺寸、检查道具不出画布。场景归属由宠物的动作表决定：专属场景只写进目标宠物，经典场景每只保留两个。

```sh
python -I scripts/pet/render_scenes.py
```

奶糖的专属场景是 `dog_frisbee` 与 `dog_burying`，共享 `skate` 与 `swing`。

## 复习牌

七只随包宠物的复习牌画法在 `ui-prototypes/pet-signs.html` 与 `signs/designs.mjs` 中，两者共用同一份引擎。`signs/export.mjs` 在仓库根目录运行（`node scripts/pet/signs/export.mjs`），按 4 px 宽度档渲染每只宠物的牌，写出 `data/pet/<id>/sign.json` 与 `sign/<宽>.png`。改了画法后重跑导出并提交生成的数据（ADR 0127）。

## 原型与测试

- `cd ui-prototypes/pet-prototype && npm run embed` 重新生成 `pet.html`；`npm run embed:hover` 生成 `pet-hover.html`。
- `ui-prototypes/pet-review.html` 与 `ui-prototypes/pet-prototype/walk.html` 直接读 `data/pet`，不需要 embed。
- 数据契约：`build-ninja/test/googletest/lens_gtest_unit.exe --gtest_filter='PetData.*'`。
- 原型逻辑：`node --test ui-prototypes/pet-prototype/test/*.test.js`。
