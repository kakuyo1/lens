# Lens marketing 页

## 文件

- `index.html`：单文件页面（内联 CSS/JS）。整页 = 一张展开的词条卡；
  七只随包像素宠物是滑动时的交互来源。
- `assets/`：全部由 `tools/prep-assets.py` 从仓库自有素材生成，无外部图库。
- `tools/prep-assets.py`：素材生成脚本。改完素材配方后从仓库根目录运行
  `python -I marketing/tools/prep-assets.py` 重建。

## 素材来源（assets/）

- `pets/<pet>/<action>.png`：按 App 的三层规则（body 0 / expression 2 / effect 6）
  逐帧合成——身体帧 + `expressions[i]` 表情格 + 特效层（尘土、彩带、星星）。
  只生成页面实际播放的动作（脚本里的 `USED` 表）；帧率见 `meta.json`。
  猫的 `catyarn.png` 是场景合成：`data/scenes/cat_yarn/` 的 back + body + front
  三层按帧叠加（玩线球，含表情层）。
- `pets/<pet>/acc.png`：一件配饰的静态覆盖层（宠物动、配饰不单独动）：
  猫·铃铛项圈、云·星星毛线帽、狗·领巾、幽灵·女巫帽、水母·海星发夹、
  麻糬·发簪、猫头鹰·学士帽（头/身槽，不含眼镜）。
- `signs/<pet>.png`：`data/pet/<pet>/sign/` 立起状态末帧切片；词由页面按
  `sign.json` 的 word.top / word.color 叠绘，字体为随包 Pixelify Sans（OFL）。
- `pictures/crane-*.jpg`：义项图演示照片，取自 `ui-prototypes/assets/word-images/`，
  裁为 4：3。署名（Wikimedia Commons）：`Charles J. Sharp`（CC BY-SA 4.0）、`Andreas Weith`
  （CC BY-SA 4.0）、`Alex Panoiu`（CC BY 2.0）、`Lee Ann Ratledge`（CC BY 4.0）、`KKPCW`（CC BY-SA 4.0）；
  署名同时写在卡片 `.pic-credit` 行内。
- `brand/lens-mark.svg`：`docs/readme/brand/`。

## 交互编排

- 狗：首屏从左缘走入、张望、站定（真实进出场仪式）。
- 猫：取词节 pounce 崩出，落地玩线球（catyarn 场景循环）。
- 水母：缩小停在词卡里 `Not the Sense?` 那一行的旁边，疑惑（thinking）一次、换一次义项。
- 猫头鹰：举牌提问（ask 加自己的牌）；悬停出现忘了、记得的小卡，
  作答后 celebrate / encourage，等级 0–5 圆点与词轮换。
- 麻糬：蹦到右缘撞击（挤压 + 像素碎屑）→ pickup 姿态弹飞向左缘 → 再撞 →
  落到中间 → dizzy 眩晕（15 帧 @6fps，星星绕头）→ 站定。滚走再回可重播。
- 幽灵：固定视口左缘 edge_peek，记录节进入时探头（窄屏隐藏）。
- 小云：封底缓漂。页脚：七只 conga 游行。
- 复制抽屉自动演示：词选区给纠正拼写，句子选区给纠正语法和句子拆解。
- 所有动效 prefers-reduced-motion 下冻结为第 0 帧、位移取消、内容完整可见；
  `<noscript>` 有静态兜底。页面精灵帧表/帧率由 meta.json 生成，勿手抄。

## 待上线前替换

- 年度记录格子与汇总数字（1,284 / 486）是示例数据。
- 复习演示词（knack / grasp / terse / apt）与 crane 词卡为演示内容。
- 下载按钮指向 GitHub Releases，换正式分发地址即可。

## QA 口径

taste-skill gate：`lint.mjs` 0 FAIL 0 WARN；`inspect.mjs --allow mono`
（mono 为 DESIGN.md 数字等宽规范的正当豁免）1440 / 768 / 390 全 0 FAIL。
交互（记得/忘了、抽屉两态、水母换义、麻糬抛掷）经 CDP 实测。
