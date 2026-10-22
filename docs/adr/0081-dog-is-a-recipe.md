# 0081 小狗改用配方渲染

- **状态**：accepted（2026-10-22）
- **背景**：1.2.0 的小狗是唯一由 `scripts/pet/dog/build-assets.py` 渲染的宠物；它有自己的 `sprite.json`、manifest、配饰表，以及两份必须同步的 `EYE_PATTERNS`。2026-10 的奶糖重做把狗改成正面坐姿的垂耳小狗，正好把狗收回同一条配方管线。
- **决定**：
  - 狗的源改为 `scripts/pet/recipes/dog.json`，由 `render_pet.py` 生成 `data/pet/dog/`；删除 `scripts/pet/dog/`、`recipes/original-dog.json` 与小狗专用的 grid / apply / refit / sync / compose 工具。
  - 渲染器新增配方级 `eyeSize: 2` 与 `SMALL_EYE_PATTERNS`，保留奶糖的 2 × 2 格子眼（6 × 6 像素）；其他宠物继续用 3 × 3 像素的默认图案。
  - 保留 22 个动作、4 个场景与 7 件配饰的 id；场景动作与配饰按奶糖重画。举牌沿用 9 × 8 的爪子，两手整体外移 3 格，握点测试改为 `(74.5, 51)` 与 `(25.5, 51)`。
  - 旧的 1.2.0 小狗美术、手写 `animations.json` / `anchors.json` / `accessories.json` 与旧构建流程退役；旧图留在 git 历史。
- **后果**：
  - 七只随包宠物共用一套渲染器与一份数据契约；小狗的配饰 `supportedActions`、锚点与 sheet 全部由配方生成。
  - `pet_data_test` 里的动作数、角色、idlePool、场景 2+2、配饰 7 与握点仍是硬契约；改手的位置要同步测试与 `ui-prototypes/pet-review.html` 的 `FIST`。
  - 小狗的旧构建工具与 `docs/pet-art-pipeline.md` 的 1.2.0 流程移除；`scripts/pet/README.md`、`docs/pet-architecture.html` 与 ADR 0049 的实现注记同步。
