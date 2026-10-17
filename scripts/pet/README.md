# 桌面宠物美术脚本

桌面宠物的 PNG 与数据文件都从形状配方生成。新宠物用 `render_pet.py`；小狗（1.2.0）仍用 `dog/` 里的旧渲染器。完整流程、约束与经验见 [`docs/pet-art-pipeline.md`](../../docs/pet-art-pipeline.md)。

## 新桌宠：配方到输出

| 文件 | 作用 |
| --- | --- |
| `recipes/<id>.json` | 配方：身体、眼睛、嘴、角色、每个动作的各帧姿势，以及配饰。`mochi.json` 是范例 |
| `render_pet.py` | 读配方，写出 `data/pet/<id>/` 下的 `pet.json`、`animations.json`、`anchors.json`、`accessories.json` 与全部 PNG，并核对每张 sheet 的尺寸 |

从仓库根目录运行（需要 Pillow）：

```sh
python -I scripts/pet/render_pet.py mochi
```

它直接覆盖 `data/pet/<id>/`，没有暂存副本，也没有预览模式。

配方的字段（完整说明在 `render_pet.py` 的文件头）：

- `body`：身体的基础椭圆 `{cx, cy, rx, ry}`，单位是格子；它的底部中心是所有姿势的轴。
- `eyes`、`mouth`、`yawnMouth`、`angryMouth`：表情层的格子，以及张嘴（表情 7）的矩形 `[x, y, w, h]` 与生气（表情 9）的撇嘴格子。
- `roles`、`idlePool`：原样写进 `animations.json`。
- `actions.<名>`：`fps`、`loop`，可选 `blink`、`returnTo`、`duration`、`bodyFrom`、`paint`（每帧都画），以及 `frames`。每一帧可有 `pose`（`dx`、`dy` 平移，`sx`、`sy` 绕底部中心缩放）、`expression`、`shapes`（额外形状，`add` 或 `discard`）与 `paint`。`duration`（毫秒）让循环动作到点结束；`bodyFrom` 复用前面某个动作的 body sheet 与锚点，按自己的帧数循环，帧里写了 `pose` 就覆盖锚点。
- `accessories`：每件 `{id, slot, except, paint}`，`except` 列出它不能穿的动作。

## 场景

`scenes/<id>.json` 是场景的逐帧道具（从原型的 `scenes.js` 机械搬来）与宠物每帧的 `dx`、`dy`、`scale`。`render_scenes.py` 读它们，写 `data/scenes/<id>/{back,front}.png` 与运行时目录 `data/scenes/scenes.json`，并读回核对尺寸、检查道具不出画布。改了场景数据后重跑：

```sh
python -I scripts/pet/render_scenes.py
```

## 小狗的旧脚本

小狗的 PNG 已定稿，`data/pet/dog/sprites` 就是源。下面的脚本只在要改小狗的形状时用。

| 脚本 | 作用 | 会改文件吗 |
| --- | --- | --- |
| `grid_from_shapes.py` | 形状配方（`recipes/original-dog.json`）→ 网格，打印 ASCII 预览 | 只在给 `--out` 时写临时文件 |
| `apply_grid.py` | 把网格写进 `dog/sprite.json` 的 head、body、eyes、mouth 四段，默认只显示差异 | 只在给 `--write` 时写 |
| `refit_accessories.py` | 报告头、身体、眼睛的位置，按位移重新对位配饰 | 只在给 `--write` 时写 |
| `sync_sprites.py` | 核对小狗 sheet 的尺寸，渲染到临时目录后与 `data/pet/dog/sprites` 比对 | 不写文件 |
| `compose_check.py` | 一帧加表情与每件配饰，输出审查图 | 只写指定的 PNG |
| `dog/build-assets.py` | 小狗的渲染器，读 `dog/sprite.json` 与 `dog/manifest.json`，写 `data/pet/dog/sprites`（`--out` 时写到指定目录） | 直接运行即写入 |

这些脚本都从仓库根目录用 `python -I` 运行，需要 Pillow。`apply_grid.py` 与 `refit_accessories.py` 默认只预览，加 `--write` 才写入。
