# ui-prototypes

每张表面一个独立 HTML，是 `DESIGN.md` 与 `UI-SPEC.md` 的设计对照稿。不是交付物，不进构建，也不代表真实行为：行为契约在 `UI-SPEC.md`。改了这里的形态，同步改 `UI-SPEC.md`；共用令牌变了，同一次改动回写 `DESIGN.md`。

## 文件

| 文件 | 表面 |
| --- | --- |
| `tray-menu.html` | 托盘菜单，含语言子菜单 |
| `stats.html` | 统计弹窗 |
| `words.html` | 词汇弹窗 |
| `year-in-words.html` | 单词年度记录弹窗 |
| `cost.html` | 花费弹窗 |
| `settings.html` | 设置浮层 |
| `bubble.html` | 解释气泡，含单词、实体、句子、译文四种 |
| `selection-bar.html` | 选区动作条 |
| `selection-bar-expand.html` | 复制按钮悬浮展开的候选：五种形态各一份，抽屉延续各自形态的外形（页边签接出第二枚书签、手牌签摊开两张牌、吊牌签挂一张小吊牌、扇面铆出一把小扇，经典长条保持素面），只列当前选区适用的项（纠正拼写 / 纠正语法 / 句子拆解），按选区形状与位置切换；`?open` 停在展开态，供截图 |
| `breakdown-color.html` | 句子拆解的颜色对照：四个角色色（DESIGN.md 3.1e，ADR 0128），三种画法；选定 G 色底，E、F 留作对照。`?lang=` 切语言，`?theme=dark` 暗色 |
| `selection-bar-variants.html` | 选区动作条的四种备选形态（页边签 / 手牌签 / 吊牌签 / 扇面），落成 QML 的设计对照；默认仍是 `selection-bar.html` 的经典长条，见 `docs/adr/0056` |
| `dog-candidates.html` | 狗的形象重做候选：同一副垂耳骨架的四个角色（奶糖 / 礼服 / 小猎 / 雪原），黑灰白为主体、玫红与冰蓝点缀；奶糖已选定并落成 `data/pet/dog`，其余三只留作对照。可切“只看轮廓”、深色底、动起来与 88×88 实际大小，URL `?pick=classic` 预选 |
| `pet.html` | 桌面宠物子页面（扩展 → 桌面宠物，随包宠物的类型下拉、动作与配饰，以及宠物包的导入入口、被拒列表与确认弹窗） |
| `pet-hover.html` | 桌面宠物悬浮感知的对照稿（行为规格见 `UI-SPEC.md` 4.14）；`?zones` 显示两只感知环 |
| `pet-review.html` | 桌面宠物的提问牌与作答气泡 |
| `pet-signs.html` | 七只随包宠物各自的复习牌（选定候选，各带一个牌内动效）与外部包的默认木牌；牌宽随输入的单词重画，逐帧检查脚线不动（ADR 0125、0126） |
| `pet-accessories.html` | 七只宠物的配饰九件套试穿页（63 件，按 88×88 的真实锚点合成） |
| `chain-choices.html` | 释义链的三个设计选择（下钻形式、虚线的词、多义时的表现），提案对照 |
| `pet-candidates.html` | 宠物形象定稿：硬约束与直接进配方的约束对照 |
| `english-in-english.html` | “Read English in English” 的三条交互提案（提案，不是表面规格） |
| `word-images.html` | 图片作为义项锚的 A/B 对照 |
| `update.html` | 检查更新（关于 分类的检查行）与通知卡片的新版本样式 |

`pet-prototype/` 是一个 Vite 小应用，不是单个 standalone HTML：`walk.html` 是宠物步态与边缘躲藏的 demo，直接在 `pet-prototype/` 里跑 `npm run dev`，再打开 `/walk.html`。它读取 `data/pet` 的原始 JSON 与 PNG，`walk`、`edge_peek` 动作和 `movement` 块都取自同一份 `animations.json`；`walk.html` 里可以切七只宠物、左/右边缘、暂停与速度，勾 “原地播放” 看步态循环，点 “边缘演示” 强制一只躲到边缘，再把边缘的宠物拖出来结束躲藏。页面顶部的 “躲藏提问牌” 卡演示躲藏时木牌立在露出的内侧，柱脚踩在宠物脚线；牌子不再跟手逐帧移动。

`ceremony.html` 是进出场仪式的原型，QML 已按它落地（ADR 0089）：在模拟桌面里，开启时宠物从离落点最近的左右边缘走入，播一轮 `look_around` 再站定；换宠时旧宠从同一边缘离场、新宠从那里走入并张望一轮；关闭时宠物走到边上，回头并播 `encourage` 的爱心，再走出屏幕。右侧面板可开关宠物、点选七只、改进出边与速度，并保留 “瞬时显隐” 的现状对照；`?pet=cat`、`?side=left|right`、`?speed=0.5|1|1.5`、`?instant=1`、`?switchlook=0`、`?auto=0` 控制初始状态；新宠张望默认开，`?switchlook=0` 或面板里的开关可关掉对照。舞台下方的时间线显示本轮脚本。它和 `walk.html` 一样直接读 `data/pet`，告辞复用现有动作，没有新增美术。

`scenes.html` 是场景入场的提案，QML 已按它落地（ADR 0101）：抽到场景不再硬切，而按场景编排一段约 0.3–0.8 秒的戏——泡澡先出浴缸再跳进去、纸箱一扑、秋千一跃、窗台趴下；道具透明度、宠物前后层与 idle 身体都来自真实的 `data/scenes` 与 `data/pet`。水花与尘土只是几粒像素，不是新美术。`?scene=bath|cat_box|swing|cat_sunbeam`、`?variant=enter|fade|cut`、`?speed=0.5|1|1.5`、`?reduced=1`、`?auto=0`、`?box=1` 控制初始状态，`?t=<ms>` 把舞台冻结在某一刻，供逐帧核对。它和 `ceremony.html` 一样在 `pet-prototype/` 里跑 `npm run dev` 后打开 `/scenes.html`。落地版本见 `src/app/qml/pet/PetScene.qml` 与 `SceneEntryEffect.qml`；设置页的动作预览仍是静止场景，场景药丸走真实入场。

`scenes-six.html` 是每只宠物从四景扩到六景的设计板，已落地：七只随包宠物各加两个专属场景（共十四景），每景给出道具、入场、时段权重与配饰牵引；页面同时并排现有四景，宠物用 `data/pet` 的真实 idle / sleep 帧，道具仍是本页画的设计稿。14 景已生成 `scripts/pet/scenes` 与 `data/scenes`。场景全部可暂停，`prefers-reduced-motion` 时从静止帧开始；`?t=<ms>` 把全部画布冻结在某一刻，供逐帧核对。和 `scenes.html` 一样在 `pet-prototype/` 里跑 `npm run dev` 后打开 `/scenes-six.html`，入口也加在宠物原型首页。

## 怎么看

双击打开，或访问 `file:///.../settings.html`。URL 追加 `?theme=dark` 直接看静谧暗夜主题，`?theme=frosted` / `?theme=liquid` 看两套玻璃主题（雾面玻璃、流光玻璃），设置面板里的主题下拉也能切。除 `settings.html` 外，每张表面右上角的按钮循环切换四套主题（晴朗白昼、静谧暗夜、雾面玻璃、流光玻璃），按钮文字是当前主题名；带 `?shot` 时按钮隐去。`settings.html` 的流光玻璃与雾面玻璃主题左上角各有一组测试背景单选，底图分别在 `liquid-backdrops/` 与 `frosted-backdrops/`；URL 追加 `?bg=<文件名>` 指定一张（如 `?bg=web-light`、`?bg=checker`），文件属于哪套主题就用哪套。

`selection-bar-variants.html` 追加 `?v=A|E|H|F` 只看一种形态，`?lang=` 与 `?theme=` 照旧，`?shot` 隐去右上角控件；每条动作条进入视野时出场，滚回去会重播，点一下看它自己的按法。它是动作条样式的备选对照；默认仍是 `selection-bar.html` 的经典长条，行为契约在 `UI-SPEC.md` 4.10。

追加 `?lang=en`、`?lang=ja`、`?lang=es` 按该语言渲染界面，缺省中文。进 README 的五个表面（bubble、selection-bar、settings、words、year-in-words）接了它，其余表面和设置里的子页面仍是中文。pet 页要看到预览舞台，追加 `?on` 直接打开桌面宠物；`?act=ask` 让预览直接停在提问牌，`?preview` 把面板滚到预览舞台（headless 截图的静止画面不走动画，要看起伏请在浏览器里打开）；要直接看宠物包的确认弹窗，追加 `?dlg=1`；README 里的复习牌截图由 `pet-review.html` 渲染；旧的桌面宠物图取自应用本身，当前 README 不再引用。

`pet-review.html` 直接停在提问牌，默认立在左侧；左侧的木牌连同外侧的小卡放不下时换右侧，两侧都放不下取溢出少的一侧。`?pet=dog|mochi|ghost` 选择宠物；`?bg=<当前玻璃主题的底图>` 换背景；`?scale=1|2|3.01|4` 改宠物尺寸，木牌随之等比缩放；`?sign=left|right` 强制木牌的一侧；`?f=0` 到 `?f=3` 停在宠物 `ask` 的某一帧，用来看身体的起伏（木牌不跟着动）。`?qa` 把木牌、柱、小卡与气泡的屏幕坐标写在页面左下角，用来核对没有被截断。`?state=hover` 看展开的两枚按钮，`?state=forgot` / `?state=remembered` 看作答后的宠物动作与气泡，`?state=timeout` 看超时生气。作答气泡缺省在木牌那一侧，与宠物垂直居中，那一侧放不下时翻到另一侧；`?side=left|right` 强制气泡一侧，用来看翻边。小卡朝木牌的外侧展开：木牌在左时小卡在左，在右时在右；外侧放不下时翻到内侧。`?lang=` 支持四种界面语言；牌上的单词与气泡里的释义是样例内容，不随界面语言变。牌面的像素字体由页面相对引用 `data/fonts/PixelifySans-Regular.ttf`。

`pet-hover.html` 是悬浮感知的对照稿，行为规格在 `UI-SPEC.md` 4.14：指针靠近宠物时先跟眼神，停一下给个小反应，再停一会给完整动作；右侧可单独关掉每一层，`?zones` 直接显示注视与注意两只环，`?qa` 暴露页面里的 pet 列表供截图脚本暂停帧用。状态文字只在这个原型里出现，产品里的宠物不说话。

`pet-accessories.html` 是七只的配饰九件套：每只 3 头 / 3 脸 / 3 身，共 63 件。右边九件都在柜子里，同一槽位单选；左边试衣台 4×，卡片是缩略图。“现状对照” 看旧的这身，“恢复初始” 回到首选，底部 “复制当前搭配” 抄出七只的 3 件当前搭配。`?pet=dog|mochi|owl|cat|ghost|jelly|cloud` 滚到某一只，`?theme=dark` 暗色，`?shot` 隐去控件与底部条。它是试穿页，不是新表面；九件套已落进配方与 `data/pet`，页面保留为试穿预览。

`english-in-english.html` 是三条交互提案的单页演示：`?scene=1/2/5` 选场景，`?theme=dark` 暗色，`?shot` 隐去顶栏与说明栏。场景 1 加 `?chain=knack` 看释义下钻；场景 2 加 `?hold=1&word=ephemeral&looked=1` 看铅笔层与边注；场景 5 加 `?peek=1`。它只做明暗两套主题，玻璃主题待落 QML 时再对。

`word-images.html` 是图片义项锚的 A/B 对照：`?mode=a|b` 选浏览器直达或卡内三图，`?theme=dark` 暗色，`?shot` 隐去顶栏与说明栏；`?open=1` 直接打开浏览器或展开图片，`?sense=bird` 从 “鹤” 义项开始，`?photo=1` 直接打开第一张图的大图（仅 B）。照片在 `assets/word-images/`，页面用相对路径引用，不联网；署名与许可见大图。

`bubble.html` 追加 `?kind=entity`、`?kind=sentence` 或 `?kind=translation` 看实体、句子与中文译文，缺省是单词。前两种的样例文字是英文，与界面语言无关：README 的截图按英文解释渲染，解释语言是设置里另一项；译文那张的样例是中文，因为译文通道只收中文，卡片上画的仍是英文。README 的静态与 GIF 帧还会用 `?explain=zh`（以另一门解释语言开场）、`?menu=open`（展开语言条）、`?actions=open`（露出已会/新词行）和 `?transparent`（去掉页面底色）。`?page=1` 会把卡片放到同一文档的一张阅读页上，`?pageScale=1.5` 控制卡片缩放；themes 和语言切换 GIF 都用它，玻璃主题因此会真正模糊背后的文字。带 `?shot` 时页面会加上 `.shot`，关闭过渡与动画，截图停在最终状态。

`settings.html` 的功能预览（UI-SPEC.md 4.4）：指针停在通用 › 打字速度 / 字体子页里的字体一行，或阅读学习里解释语言 / 多义解释 / 词源 / 例句五行的**标题文字**上，卡片就在面板旁浮出，离开标题即收；命中区只有标题，开关与滑块不触发。字体那一处的卡片按所选的英文字族画英文（词、音标、英文释义），其余文字仍用应用那面——设置只管英文，卡片就只演示英文。卡片按当前主题取玻璃，样例文字一律英文（同 `bubble.html`：跟着解释语言换行的那一版只在应用里有）。它贴在面板右侧优先、右侧放不下才翻到左侧，而原型把面板摆在视口正中，所以这里多半在右侧；应用里面板挂在托盘图标旁，实际多为左侧。README 的截图不拍它（它是一次悬浮，不是一屏）。

`year-in-words.html` 追加 `?palette=open` 直接展开配色盘，第 3 档记为按下。

## 修改提示

- 一个文件一块表面，自带一份配色令牌（`:root` 与 `html[data-theme=dark]`）。改配色先改 `DESIGN.md` §3.1 / §3.1b，再同步这几个文件，不要只改一个。
- 页面里的交互只是示意：JS 模拟选中、展开与主题切换，用来对视，不是实现。别把原生 `<select>`、`<details>`、密码框的外形当成 QML 的样子去抠像素；要对齐的是层级、间距与状态。
- 静态页面拍不出毛玻璃与阴影的真实观感，QML 侧用 `MultiEffect`，见 `docs/QML.md`。
- `settings.html` 用分类目录：首页只列分类，进入后是分类设置，再进入是词书、API 等子页面。新增设置放进对应分类，不要加长首页，也不要新增一级分类；“关于” 是目录末尾的唯一例外，取舍见 `docs/adr/0032`。
- 面板固定 380 × 480，超出的内容在 `.set-body` 内滚动。内容变多先考虑合并成分组或下沉为子页面。
- 扩展分类里的桌面宠物已随 1.2.0 上线，`pet.html` 是它的设计对照。`pet.html` 内嵌的 PNG 来自 `data/pet/`，改了数据要跑 `npm run embed` 重新生成；`pet-hover.html` 用同一份数据，跑 `npm run embed:hover`。改了 `pet-prototype/src/pet-renderer.js` 这类共享文件，两条都要重跑。麻糬、猫头鹰、猫的美术是生成的初版，还需要人工复核。
- 宠物包一节用两只写死的样例（`PACKAGES` 与 `REFUSED`）对视来源标记、被拒列表与确认弹窗：它们不是扫出来的，预览也复用随包宠物的美术。契约、上限与拒绝原因见 `docs/pet-package-format.md`，决定见 `docs/adr/0043`。
- 加新表面时，新增一个文件、在这里登记，并在 `UI-SPEC.md` §4.0 的表面表加一行。
- 界面文字四语言各一份，在每个文件脚本开头的 `L` 表里，取值与 `i18n/lens_*.ts` 一致：改文案先改那边，再抄回来。README 的营销图由 `scripts/qa/readme-html-shots.sh` 从这些表面渲染，再由 `scripts/qa/readme-html-compose.py` 裁切、合成 GIF、学习循环拼图与主题拼图，写进 `docs/readme/`。旧的 `readme-shots.sh` 与 `readme-gifs.ps1` 生成 `docs/readme-images/`，已不供当前 README 使用。
- 文件内的中文同样按项目排版规则写，`zhlint` 可查（`.githooks/pre-commit` 的预算里已登记本文件）。
