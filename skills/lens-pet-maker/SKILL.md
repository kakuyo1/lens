---
name: lens-pet-maker
description: 为 Lens 桌面宠物制作第三方宠物包（%APPDATA%\Lens\pets\）。走"参考图确认 → idle 变体对照 gate → 配方化渲染 → 校验 → 拼图确认"的流水线。用户要求制作、生成、重做 Lens 宠物包或桌面宠物时使用。
---

# Lens 宠物包制作

## 文件地图

| 文件 | 作用 |
|---|---|
| `references/pet-package-format.md` | 宠物包格式契约正本。先完整读它 |
| `scripts/pet_kit.py` | 配方渲染器。**从 `PRESETS["dog"]` 起步改**；文件头 docstring 是 recipe schema；`rig.scale` 一个数字整体缩放 |
| `scripts/validate_package.py` | 独立契约校验器。`python -I validate_package.py <包目录>` |
| `scripts/preview.py` | `--idle` 单帧预览；`--sheet` 全动作拼图；`--compare <pkgA> <pkgB> … <out>` 多变体对照条 |

## 工作流（严格按序，两道 gate 缺一不可）

### 阶段 0 · 契约
完整读 `references/pet-package-format.md`。跳过会直接导致包被拒。

### 阶段 1 · 形象 ground truth（已存在角色必做）
用户描述的是已存在的形象时：**用户已给参考图就跳过搜索**；否则 WebSearch 找官方图，下载 1–3 张到 `%TEMP%\pet-ref-<id>\`，用 Read 亲眼逐张看。列出标志特征（剪影、配色、脸、比例），**停下**交用户确认。原创形象直接进阶段 2。

### 阶段 2 · 检查点一：idle 变体对照（一次给多个候选）
1. 从 `PRESETS` 挑最接近的预设起步，按特征清单改 rig。
2. **一次渲染 2–3 个候选**（如 `rig.scale` 0.9 / 1.0 / 1.1，或两种耳形），分别 render 出包，`preview.py --compare` 拼成一张对照条。
3. **停下**把对照条交用户，一次选定。微调用 `rig.scale` 和个别数值，改完再对照，直到认可。**认可之前不生成其他动作。**

### 阶段 3 · 全套生成
1. 扩展 recipe：14 角色一角色一动作 + blink + idlePool + hover + movement + behavior + 9 件配饰（三槽各 3）。姿势数据写法见 pet_kit.py docstring。
2. 渲染 + validate + `preview.py --sheet`。校验失败按输出修 recipe，不修渲染器。
3. **停下**把拼图交用户做最终确认。

### 阶段 4 · 交付
经用户同意后把 `pets/<id>\` 复制到 `%APPDATA%\Lens\pets\`，报告启用步骤（打开设置的扩展分类）。

## 提速规则

- **永远写 recipe，不手写逐像素绘图代码。**对齐、锚点、sheet 尺寸由 pet_kit 从姿势推导。
- **gate 2 一次出多个候选**，用对照条一轮收敛，别再一版一版地磨。
- **迭代只改 recipe 数值**，一条 Bash 把渲染、校验、预览一次跑完；迭代之间不写长篇分析，一两句定位与改动即可。
- rig 起步用 `PRESETS`，不要从零发明坐标；大幅放大（>1.1）直接改 body/feet 数值，`rig.scale` 只管小幅微调。
- 效果层（hearts/stars/zzz/notes/dust）与 blink 由 kit 内置生成，不自绘。
- 视觉检查只有两道 gate；中间交给 validate + preview，不逐帧目检。
