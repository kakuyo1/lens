# 桌面宠物美术脚本

七只随包宠物（猫、狗、云、幽灵、水母、麻糬、猫头鹰）都由配方生成。配方字段、渲染约束与检查流程见 [`docs/pet-art-pipeline.md`](../../docs/pet-art-pipeline.md)；这里只留运行入口。渲染器直接覆盖 `data/pet/<id>/`，没有暂存副本。

```sh
python -I scripts/pet/render_pet.py <id>   # 配方 → data/pet/<id>/（需要 Pillow）
python -I scripts/pet/render_scenes.py     # scripts/pet/scenes/ → data/scenes/
node scripts/pet/signs/export.mjs          # 复习牌：按 4 px 宽度档渲染，写 data/pet/<id>/sign.json 与 sign/（ADR 0127）
```

场景归属由宠物的动作表决定：专属场景只写进目标宠物（奶糖的 `dog_frisbee` 与 `dog_burying`），共享场景每只保留两个（`skate` 与 `swing`）。改了复习牌画法后重跑导出并提交生成的数据。

## 原型与测试

- `cd ui-prototypes/pet-prototype && npm run embed` 重新生成 `pet.html`；`npm run embed:hover` 生成 `pet-hover.html`。
- `ui-prototypes/pet-review.html` 与 `ui-prototypes/pet-prototype/walk.html` 直接读 `data/pet`，不需要 embed。
- 数据契约：`build-ninja/test/googletest/lens_gtest_unit.exe --gtest_filter='PetData.*'`；原型逻辑：`node --test ui-prototypes/pet-prototype/test/*.test.js`。
