# ui-prototypes

每张表面一个独立 HTML，是 `UI.md` 的设计对照稿。不是交付物，不进构建，也不代表真实行为：行为契约在 `UI.md`。改了这里的形态，同步改 `UI.md`。

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
| `pet.html` | 桌面宠物子页面（扩展 → 桌面宠物，四只宠物的类型下拉、动作与配饰，以及宠物包的导入入口、被拒列表与确认弹窗） |
| `update.html` | 检查更新（关于 分类的检查行）与通知卡片的新版本样式 |

## 怎么看

双击打开，或访问 `file:///.../settings.html`。URL 追加 `?theme=dark` 直接看静谧暗夜主题，`?theme=frosted` / `?theme=liquid` 看两套玻璃主题（雾面玻璃、流光玻璃），设置面板里的主题下拉也能切。除 `settings.html` 外，每张表面右上角的按钮循环切换四套主题（晴朗白昼、静谧暗夜、雾面玻璃、流光玻璃），按钮文字是当前主题名；带 `?shot` 时按钮隐去。`settings.html` 的流光玻璃与雾面玻璃主题左上角各有一组测试背景单选，底图分别在 `liquid-backdrops/` 与 `frosted-backdrops/`；URL 追加 `?bg=<文件名>` 指定一张（如 `?bg=web-light`、`?bg=checker`），文件属于哪套主题就用哪套。

追加 `?lang=en`、`?lang=ja`、`?lang=es` 按该语言渲染界面，缺省中文。进 README 的五个表面（bubble、selection-bar、settings、words、year-in-words）接了它，其余表面和设置里的子页面仍是中文。pet 页要看到预览舞台，追加 `?on` 直接打开桌面宠物；要直接看宠物包的确认弹窗，追加 `?dlg=1`；README 里的桌面宠物图取自应用本身，不由这里渲染。

`bubble.html` 追加 `?kind=entity`、`?kind=sentence` 或 `?kind=translation` 看实体、句子与中文译文，缺省是单词。前两种的样例文字是英文，与界面语言无关：README 的截图按英文解释渲染，解释语言是设置里另一项；译文那张的样例是中文，因为译文通道只收中文，卡片上画的仍是英文。`year-in-words.html` 追加 `?palette=open` 直接展开配色盘，第 3 档记为按下。

## 修改提示

- 一个文件一块表面，自带一份配色令牌（`:root` 与 `html[data-theme=dark]`）。改配色先改 `UI.md` §3.1 / §3.1b，再同步这几个文件，不要只改一个。
- 页面里的交互只是示意：JS 模拟选中、展开与主题切换，用来对视，不是实现。别把原生 `<select>`、`<details>`、密码框的外形当成 QML 的样子去抠像素；要对齐的是层级、间距与状态。
- 静态页面拍不出毛玻璃与阴影的真实观感，QML 侧用 `MultiEffect`，见 `docs/QML.md`。
- `settings.html` 用分类目录：首页只列分类，进入后是分类设置，再进入是词书、API 等子页面。新增设置放进对应分类，不要加长首页，也不要新增一级分类；“关于” 是目录末尾的唯一例外，取舍见 `docs/adr/0032`。
- 面板固定 380 × 480，超出的内容在 `.set-body` 内滚动。内容变多先考虑合并成分组或下沉为子页面。
- 扩展分类里的桌面宠物已随 1.2.0 上线，`pet.html` 是它的设计对照。`pet.html` 内嵌的 PNG 来自 `data/pet/`，改了数据要跑 `npm run embed` 重新生成。麻糬、猫头鹰、猫的美术是生成的初版，还需要人工复核。
- 宠物包一节用两只写死的样例（`PACKAGES` 与 `REFUSED`）对视来源标记、被拒列表与确认弹窗：它们不是扫出来的，预览也复用随包宠物的美术。契约、上限与拒绝原因见 `docs/pet-package-format.md`，决定见 `docs/adr/0043`。
- 加新表面时，新增一个文件、在这里登记，并在 `UI.md` 的表面表加一行。
- 界面文字四语言各一份，在每个文件脚本开头的 `L` 表里，取值与 `i18n/lens_*.ts` 一致：改文案先改那边，再抄回来。README 的四十四张截图（十一张图 × 四种语言）由 `scripts/qa/readme-shots.sh` 从这些表生成，写进 `docs/readme-images/<语言>/`，改完记得重跑。桌面宠物图不在其中。
- 文件内的中文同样按项目排版规则写，`zhlint` 可查（`.githooks/pre-commit` 的预算里已登记本文件）。
