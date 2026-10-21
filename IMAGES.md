# Lens · 词卡图片（实时取图）

> 状态：方案已定，未实现。取图已决定走实时搜索；本地图片包不做。
> 相关：`UI.md` §4.3、`API.md`、`SECURITY.md`、`GLOSSARY.md`、`docs/QML.md`、`ui-prototypes/word-images.html`。
> 本文是设计与落地计划。实现时以 `UI.md`、`API.md`、`SECURITY.md` 的行为契约为准，并同步更新它们；本文不替代现有规格。

## 1 结论

- **默认体验是卡内三图（B）**：图片挂在义项上，不挂在单词上；词卡内原地展开三张缩略图，英文图注、来源与署名跟着图走。浏览器直达（A）保留为兜底，不是默认。
- **图源是实时搜索**：首个图源为 Wikimedia Commons；每次读者点 `Pictures` 才请求，不自动加载。
- **稳定性靠缓存与兜底**：缩略图落到读者 profile，第二次打开不再请求；缓存没有、请求失败或结果为空时，`Pictures` 变成 `Open image search`，用系统浏览器打开英文图片搜索。永远不显示空槽或坏图。
- **不做本地图片包**：选词、审图与许可整理的工作量超过了当前收益；也不做图片生成、内嵌浏览器或搜索结果抓取。
- **图片不能挡住释义**：文字定义先到，图片是可选的第二层。没有网络时，已有释义缓存仍可读，只是没有图。

实时搜索的代价要写清楚：**图不保证每次都取到，也不保证义项正确**。缓存让重复查看稳定，兜底让失败不破坏卡片；两者都不是 “每次必出图” 的承诺。

## 2 用户体验

### 2.1 入口

- 只对 `imageable == true` 的义项出现 `Pictures` 入口。抽象词、功能词、不可见的动作不出入口。
- 词卡可能同时列出多条义项（多义解释开启时最多三条）。每条可成像的义项各自拥有入口；一次只展开一条的图片条，展开新的会收起旧的。无需单独的 `Not this sense?`——义项行本身就是选择器。单义项时，行为与原型里的 `Pictures` 胶囊一致。
- 入口放在该义项文本下方，右对齐；带一个 13 px 相机图标和 `Pictures` 文案，高约 22 px。展开后文案变 `Hide pictures`，图标方向或 `aria-expanded` 状态同步。视觉参考 `ui-prototypes/word-images.html` 的 B 场景；实际卡片宽度以 `UI.md` 4.3 的 270 px 为准。
- 卡片只向下长；展开、加载、收起都不能移动锚点或让卡片跳到屏幕外。放置仍走 `Bubble.qml` 现有的贴合与工作区收边逻辑。

### 2.2 状态

| 状态 | 触发 | 卡片里画什么 | 请求 |
|---|---|---|---|
| 未请求 | 刚弹出 | `Pictures` 入口，无图 | 无 |
| 加载中 | 点了入口 | 三格 4:3 骨架，文案 `Loading pictures…`；入口不可重复点击 | 一次 |
| 就绪 | 请求成功 | 1–3 张缩略图；当前图的英文图注、作者、许可、来源 | 无（已缓存） |
| 部分 | 只拿到 1–2 张有效图 | 画有效的几张，不留空骨架 | 无 |
| 失败/空 | 超时、无结果、解码失败、被网络拦住 | 一行 `Pictures unavailable` + `Open image search` | 无 |
| 离线有缓存 | 缓存命中 | 直接画图，不显示加载骨架 | 无 |
| 离线无缓存 | 缓存未命中且请求失败 | 同失败态 | 一次，失败即止 |

- 整个图片操作的超时上限为 4 秒；搜索返回后逐张下载，先到的先画。超过上限就进入失败态，已到的有效图仍可显示。
- 失败不弹通知卡片，不打断阅读；图片是附属信息，失败只影响这一条 `Pictures`。加载中、失败、成功都不重置气泡倒计时；指针在卡片内时按现有规则停表。
- 卡片关闭、切换解释语言或切换义项时，旧的请求结果不得写回新卡片；用请求代号丢弃过期结果。网络请求可以继续完成并落缓存，供下次使用。

### 2.3 图片条

- 内容宽 238 px（270 px 卡宽减去 16 px 左右内边距）；三张时每张约 77 × 58 px，4:3，间距 4 px，圆角 8 px。两张时两列，一张时单张铺满内容宽；不补空槽。
- 图片使用 `PreserveAspectCrop` 填满 4:3 格子；缩略图从图源取 480 px 宽，足够 100% 与 150% DPI。
- 图片下方一行 10.5 px 图注，默认显示第一张，悬停或键盘聚焦某张时切到该张。图注是英文；图源返回的描述不是英文时，退回清洗过的文件名，再退回 `imageQuery`，不机翻、不显示中文图注。
- 图注下再一行 9.5 px 弱文字，必须常驻，不只放在 tooltip：`Photo: {author} · {license} · {source}`。`source` 显示 `Wikimedia Commons` 之类的图源名；作者或许可缺失的图不接收。点缩略图或这一行来源，用系统浏览器打开原图页面。
- v1 不做 Lens 内的 lightbox；原型里的大图层只是设计对照。放大、完整许可与原始分辨率由来源页承担，因此不需要新增表面，也不改 `UI.md` 的表面表。以后若要做 lightbox，按新表面处理并补 ADR。
- 图片条不是玻璃：它画在卡片内容区里，骨架用 `line2`/`line` 令牌，图注用 `muted`/`faint`，不新增颜色。图片本身保持矩形，不加毛玻璃或二次模糊。
- loading 骨架在减少动效时是静态占位；没有旋转 spinner。

### 2.4 交互与可访问性

- `Pictures` 是按钮，可用 Enter / Space 展开与收起；展开后焦点留在按钮上，不跳到图里。缩略图各自是按钮，有 `Accessible.name`（图注）与可聚焦轮廓；Enter 打开来源页。
- 一次只展开一条义项的图片条。点另一条的 `Pictures` 会收起当前条；当前条的请求可以继续落缓存，但不再更新 UI。
- 点 `Open image search` 打开系统默认浏览器的英文图片搜索；查询就是该义项的 `imageQuery`，不带页面原句。它是兜底，不是 Lens 内的浏览器，也不抓取搜索结果。
- 高 DPI、卡片贴近屏幕边缘、玻璃主题、暗色主题、字号 12–18 px 都要在视觉 QA 里过一遍。图片条会让卡片变高，重点检查靠近屏幕底部时是否仍整体收进工作区。
- 卡片上的 `Pictures`、`Hide pictures`、`Loading pictures…`、`Pictures unavailable`、`Open image search`、`Photo` 走界面语言翻译；图注、`imageQuery` 与图片里的文字保持英文。

## 3 模型契约

### 3.1 字段

在词条响应里为每个义项增加两个可选字段：

| 字段 | 类型 | 语义 |
|---|---|---|
| `imageable` | bool | 一张照片能否让这个义项更清楚。抽象性质、情绪、过程、语法词、依赖语言解释的词一律为 false。 |
| `imageQuery` | string | `imageable` 为 true 时必填。2–5 个英文词，带消歧，例如 `garden trowel`、`construction crane`、`crane bird`。不含引号、标点、品牌名或原句。 |

- 单义项形状放在结果对象顶部（与 `en`/`zh` 同级）；多义项形状放在每个 `senses[]` 元素里。两种形状都由同一个词条请求返回，不增加一次模型调用。
- 字段只属于 word 通道；entity / sentence / translation 不请求，也不解析。
- `imageQuery` **恒为英文**，不受解释语言影响；解释语言只决定释义用哪种语言写。
- 模型只拿到单词本身，拿不到读者页面上的原句。因此 `imageQuery` 对的是模型选中的常见义项，可能和读者上下文不一致；多义项行让读者自己选，不要假装模型知道上下文。
- `imageable` 是建议值，不是保证：为 true 但 `imageQuery` 为空、格式不合法或请求失败时，按不可成像处理，不弹错误，不阻止词卡出现。

### 3.2 提示词

在 [`data/llm/request.word.json`](data/llm/request.word.json) 的普通提示词与多义项提示词里同时增加约束。示例（普通提示词）：

```text
Also decide whether a photograph can make the most common sense clearer.
Set "imageable" to true only for a concrete, visible thing, action, or scene;
set it to false for abstract qualities, emotions, processes, grammar words,
and anything whose meaning depends on words.
When "imageable" is true, add "imageQuery": two to five plain English words
that disambiguate this sense for an image search, for example "garden trowel"
or "crane bird". Use no quotes, punctuation, brand names, or the source
sentence. Always write imageQuery in English.
```

普通响应形状增加 `"imageable":true,"imageQuery":"garden trowel"`；多义项形状在每个 sense 对象里增加同样的两个字段。`{outputLanguage}` 只影响 `en` / `zh` / `translation`，不影响 `imageQuery`。

### 3.3 Schema、解析与缓存

- [`data/llm/response.word.schema.json`](data/llm/response.word.schema.json)：把两个字段设为可选；`imageable` 必须为布尔值，`imageQuery` 必须为非空字符串。`imageable` 为 true 时 `imageQuery` 缺失，不算整个响应失败，只按不可成像处理。
- `src/llm/llm_client.h` 的 `Sense` 增加 `bool imageable` 与 `QString imageQuery`；`Explanation` 的顶层保留同样的两个字段，用于单义项/旧形状。相等比较要带上这两个字段，避免流式预览把不同的图片状态当成同一张卡。
- `src/llm/llm_pure.cpp` 在解析单义项与 `senses` 数组时读取这两个字段；单义项也要归一到第一个 sense，避免 QML 走两条路径。字段缺失或类型不对时置为 false / 空，不抛整条响应。
- `src/core/document/known_store.h` 的 `CachedSense` 增加这两个字段；`WordCache` 顶层也保留一份，兼容没有 `senses` 的旧缓存。`known_store.cpp` 的读写与缺失字段处理要成对修改。
- `src/app/duties/explanation_duty.cpp` 构造气泡 payload 时，把 `imageable` / `imageQuery` 放进每个 sense 的 `QVariantMap`；`Bubble.qml` 的 `senses` 因此能直接判断每行是否画入口。图片字段不受解释语言筛选影响，始终原样带过去。
- 旧缓存没有这两个字段时，v1 推荐**不静默重买**：不显示 `Pictures`，词卡照常。是否在下次换语言/换设置时顺带补字段，留到实现前决定；不要为了补图自动重发全部旧词。
- 缓存/流式期间：图片入口只在完整响应通过 schema 后出现，不在流式半截里凭猜测画出来；词条完整的定义、缓存、统计与花费规则不变。

### 3.4 例子

```json
{
  "word": "trowel",
  "ipa": "/ˈtraʊəl/",
  "en": "a small hand tool with a flat blade, used for digging and planting",
  "zh": "抹刀；小铲子",
  "imageable": true,
  "imageQuery": "garden trowel"
}
```

```json
{
  "word": "crane",
  "ipa": "/kreɪn/",
  "senses": [
    {
      "en": "a tall machine with a long arm, used for lifting heavy things",
      "zh": "起重机；吊车",
      "imageable": true,
      "imageQuery": "construction crane"
    },
    {
      "en": "a large bird with long legs and a long neck, that lives near water",
      "zh": "鹤",
      "imageable": true,
      "imageQuery": "crane bird"
    }
  ]
}
```

## 4 运行时

### 4.1 组件

| 位置 | 职责 |
|---|---|
| `src/llm/llm_client.h` / `llm_pure.cpp` | 解析 `imageable` / `imageQuery`，保持单义项与多义项一致 |
| `src/core/document/known_store.{h,cpp}` | 把两个字段随释义一起缓存；旧条目缺失时按不可成像 |
| `src/app/duties/explanation_duty.cpp` | 把字段放进气泡 `senses` payload |
| `src/app/app_controller.{h,cpp}` | 暴露 `PictureDuty`（与 `costDuty`、`updateDuty` 同一种接线），供 QML 读取 |
| `src/app/picture_duty.{h,cpp}` | 一次图片请求的状态机、缓存优先、超时、去重、取消/代号、图源调用 |
| `src/app/picture_source.h` | 图源接口；输入 query，输出候选图与许可元数据 |
| `src/app/picture_sources/commons_source.{h,cpp}` | Wikimedia Commons 的实现 |
| `src/app/picture_cache.{h,cpp}` | profile 内的缩略图与元数据缓存，负责键、原子写、LRU/TTL |
| `src/app/qml/components/PictureStrip.qml` | 加载/就绪/失败三态、缩略图、图注、署名、来源与浏览器兜底 |
| `src/app/qml/components/SenseRows.qml` | 每个可选义项的 `Pictures` 入口与激活义项索引 |
| `src/app/qml/Bubble.qml` | 决定当前展开的义项、把 payload 传给条、卡片高度随内容更新 |
| `src/app/CMakeLists.txt` | 加入新源文件；`lens_app` 显式链接 `Qt6::Network` |
| `i18n/lens_{en_US,zh_CN,es_ES,ja_JP}.ts` | 新增界面文案与翻译 |

### 4.2 数据流

```text
词条响应（imageable / imageQuery，按义项）
  → ExplanationDuty 放进 bubble.senses
  → 读者点某义项的 Pictures
  → PictureDuty.request(imageQuery, requestId)
      → PictureCache 命中？直接返回文件路径与元数据
      → 否则 CommonsPictureSource.search(imageQuery)
          → Commons API 搜索 10–12 个候选
          → 过滤许可/格式/尺寸/横竖比
          → 取 3 张 480 px 缩略图
      → 校验、原子写缓存
      → 返回 QML
  → 失败/空结果 → PictureStrip 显示 Open image search
```

- 图片请求不经过 LLM，不产生模型费用，也不受每日 LLM 预算限制；它只占网络与磁盘。
- 同一义项重复点击先查缓存；同一 query 已有请求在飞时合并，避免连点打多次。
- 切换义项/换语言/关卡片时，用递增 requestId 丢弃过期回调；缓存仍可完成。

### 4.3 图源：Wikimedia Commons

v1 只接 Commons，不接 Google/Bing 图片 API，也不抓搜索结果页。Commons 的图片授权清楚，适合实时取缩略图；它没有 SLA，也没有 safe-search 参数，这两条是已知限制。

- API：`https://commons.wikimedia.org/w/api.php`，HTTPS，带明确的 `User-Agent: Lens/<version> (word image lookup; https://github.com/kakuyo1/lens)`。API 与缩略图都走应用现有网络栈/系统代理。
- 搜索参数：`action=query`、`generator=search`、`gsrsearch=<imageQuery>`、`gsrnamespace=6`、`gsrlimit=12`、`prop=imageinfo`、`iiprop=url|size|mime|extmetadata`、`iiurlwidth=480`、`format=json`、`formatversion=2`。
- 结果按 API 的 `index` 保持搜索顺序；过滤后取前 3 张。过滤规则：
  - MIME 只收 `image/jpeg` / `image/webp`；不收 SVG、PDF、音频、视频。
  - 原始宽高至少 640 × 480；只要横图（宽 / 高 ≥ 1.2），避免在 4:3 格子里裁掉主体。
  - `LicenseShortName` 必须在允许清单里：Public domain、CC0、CC BY 2.0/3.0/4.0、CC BY-SA 2.0/3.0/4.0。GFDL-only、fair use、未知许可、明确保留所有权利的图一律丢弃。
  - `descriptionurl`（原图页）和 `thumburl` 必须存在；作者从 `Artist`/`Credit` 去掉 HTML 后取，缺失写 `Unknown author`，但许可或来源缺失就丢图。
  - 标题/分类里明显是 diagram、map、logo、chart、海报的图丢弃；这是启发式，不保证全对。
  - 不用 `image/png` 收照片：Commons 的 PNG 多半是图表或透明素材，v1 不做二次人工复核。
- 缩略图只从允许主机下载（`upload.wikimedia.org` / `thumb.wikimedia.org`），HTTPS，单张上限 2 MB，整个请求上限约 8 MB；用 `QImageReader` 解码，限制尺寸与帧数，失败的单张直接丢弃。
- 超时 4 秒；4xx/5xx、429、空结果、全被过滤都映射为 “不可用”，v1 不自动重试，直接兜底；读者可收起再点一次重试。不在 UI 里显示状态码。
- 主图源只发 `imageQuery`，不发释义、原句、选区和读者标识；Commons 能看到查询词、IP 与 User-Agent。Commons 在国内可能不可达；不可达时 B 会落到 A，这是本方案的已知代价，不在 v1 解决。若以后要保证国内可用，需要另选可达图源或做代理，按新 ADR 处理。
- 图源接口保持可替换：以后若接 Brave 或其他商业图搜，必须自带许可/署名元数据，并重新评估合规、费用与区域可达性；不要在 `PictureDuty` 里写死 Commons 的 JSON。

### 4.4 缓存

- 位置：`%APPDATA%\Lens\pictures\cache\v1\`（profile，不在安装目录，不写进 `settings.json`，不参与 sync）。
- 键：`sha256("commons\n" + normalizedQuery + "\n480")`。归一化只做 trim、折叠空格、小写；不改写查询语义。
- 条目：`<hash>/meta.json` + `<hash>/0.<ext>`、`1.<ext>`、`2.<ext>`。`meta.json` 记录 query、provider、抓取时间、每张图的宽高、图注、作者、许可、许可链接、来源页。元数据最后写入，半成品条目在读取时视为不存在。
- 命中：校验 meta 与至少一张图可解码后直接返回；不再请求。v1 只在读者再次点 `Pictures` 时检查 TTL；超过 30 天视为未命中并重新请求，不在卡片打开时自动刷新，也不在后台偷偷刷新。
- 上限：默认 64 MB / 约 500–1,000 个 query，先到先淘汰；按最后访问时间 LRU。TTL 30 天。缓存损坏、缺文件、解码失败时删除该条并当未命中。
- 缓存是本地私有数据：它能反映读者查过哪些词，不写日志正文、不上传、不同步；退出应用不删除，卸载行为沿用现有的 `%APPDATA%\Lens` 规则。

### 4.5 失败与兜底

- 任何失败都在图片条内收束，不弹通知，不隐藏词卡，不影响释义。
- 失败态固定为一行 `Pictures unavailable` 加一个 `Open image search` 按钮；按钮用系统浏览器打开英文图片搜索，查询仍只含 `imageQuery`。
- v1 的兜底 URL 固定为 Bing Images 的英文页面（`https://www.bing.com/images/search?q=<encoded>&setlang=en`），由读者浏览器决定最终可达性；站点以后可换，但要保持 “不抓结果、只开浏览器” 的边界。
- 不能用 “空图片条 + 占位图” 冒充失败；宁可少一行图，也不要坏图或永久骨架。

## 5 隐私、安全与许可

- **发送最小化**：图源只收到 `imageQuery`（词 + 义项消歧），不收到页面原句、选区、释义、API key 或安装标识。词条请求仍只把单词发给 LLM；两个请求是分开的。
- **无新密钥**：Commons 不需要 key；若以后接需要 key 的图源，key 进读者 profile，按现有 API key 规则处理，绝不写日志、绝不提交。
- **不可信图片**：只从 HTTPS 允许主机下载；校验 MIME、大小、尺寸、解码结果；不收 SVG/HTML；限制总字节与并发。缓存文件名由哈希生成，不接受图源提供的路径。
- **来源与署名**：卡片常驻作者、许可与图源；点图打开原图页。CC BY-SA 只显示、不重新发布；缓存只是本机显示副本，不同步、不打包。不要用带可识别人脸/隐私场景的图作为词义主图；v1 靠查询与人工抽检，不能完全保证。
- **内容安全**：Commons 没有 safe-search；v1 依赖具体名词查询与图源的项目范围。以后可以加标题/分类 denylist 或视觉审核；不要在文档里声称已经安全过滤。
- **日志**：只记录图源、状态、耗时与图片数量；不要把完整图片 URL（含查询）或图片内容写进日志。查询词本身已与现有查词日志同类，按现有规则处理。
- **文档同步**：实现时在 `SECURITY.md` 的 outbound 列表里补 `commons.wikimedia.org` 与缩略图主机、请求内容、缓存位置、图片校验与失败兜底；在 `PRODUCT.md` 的发送表里注明图片查询是发给单独图源的。

## 6 接入点清单

实现时按这个顺序改，避免把 UI、网络和缓存混在一个提交里：

1. `data/llm/request.word.json` 与 `data/llm/response.word.schema.json`：字段与提示词。
2. `src/llm/llm_client.h`、`src/llm/llm_pure.cpp`：解析与相等比较。
3. `src/core/document/known_store.{h,cpp}`：缓存字段与旧条目兼容。
4. `src/app/duties/explanation_duty.cpp`：`senses` payload。
5. `src/app/picture_source.h`、`src/app/picture_sources/commons_source.{h,cpp}`、`src/app/picture_cache.{h,cpp}`、`src/app/picture_duty.{h,cpp}`。
6. `src/app/app_controller.{h,cpp}` 与 `src/app/CMakeLists.txt`：暴露 duty、链接 Qt Network。
7. `src/app/qml/components/PictureStrip.qml`、`SenseRows.qml`、`Bubble.qml`：入口、三态、高度与布局。
8. `i18n/*.ts`：新增文案，`lupdate` / `lrelease` 到零 unfinished。
9. `UI.md` §4.3、`API.md`、`SECURITY.md`、`PRODUCT.md`、`GLOSSARY.md`：行为契约与术语同步。
10. `CHANGELOG.md`：随发布记录；若把 “实时图源 + 兜底” 作为重要取舍，补一篇 `docs/adr/`。

## 7 测试与验收

按 `TEST.md` 与 `AGENTS.md` 的最小测试原则，不跑全套：

- **C++ 单元**：一个 `lens_gtest_unit` 用例覆盖字段解析、旧缓存缺失、缓存键/命中/损坏条目、图源过滤（固定 JSON fixture）、超时/空结果映射；不测真实网络。
- **C++ 逻辑改动**：只跑覆盖该文件的 `--gtest_filter`，不需要 UI/QML 测试。
- **QML 可见表面**：一次 `scripts/qa/ui-run.ps1` 截图，覆盖 0/1/2/3 张图、加载、失败、暗色/玻璃、高 DPI 与靠近屏幕底部；不新增 qtest 套件。
- **真实网络**：Commons 的搜索与缩略图下载作为人工 integration smoke；CI 不依赖外网。
- **安全用例**：非 HTTPS、重定向到非允许主机、超大响应、损坏图片、路径穿越式文件名、缺失许可/来源；全部应被拒绝或丢弃。
- **验收**：没有自动请求；没有空条/坏图；失败可一键回浏览器；作者/许可/来源常驻；关闭或换义项后旧结果不写回；不把原句发给图源。

## 8 分阶段

1. **元数据先落地**：提示词、schema、解析、缓存、payload；UI 不变。旧词卡不显示图片入口，先保证不破坏现有链路。
2. **图源与缓存**：`PictureDuty` + Commons + 磁盘缓存 + fixture 测试；用一个调试入口验证真实网络。
3. **卡内 UI**：入口、图片条、三态、署名/来源、浏览器兜底；做一次视觉 QA。
4. **文档与发布**：`UI.md`/`API.md`/`SECURITY.md`/`PRODUCT.md`/`GLOSSARY.md`、i18n、CHANGELOG；必要时补 ADR。

## 9 待决与风险

- **国内可达性**：Commons 可能不可达，B 会经常落到 A。v1 接受；要保证 B 的国内可用性，需要另选图源或代理，不在本次范围。
- **旧缓存**：没有 `imageable` / `imageQuery` 的旧条目是否补请求，尚未定。推荐不静默重买，先让新词生效；若体验上不可接受，再做一次显式的 “刷新图片” 动作。
- **相关性**：Commons 搜索会混入错义项、图表、文字图；v1 依靠具体 query、过滤、义项行与来源页，不能保证每张都对。人工/视觉审核与举报入口是后续。
- **内容安全**：没有 safe-search；只靠查询与 Commons 范围。若出现风险，先加 denylist，再考虑审核。
- **入口位置**：每条义项一个入口在 270 px 卡内是否拥挤，需要用真实字号和三种义项做视觉 QA；必要时只给第一条义项入口，但那就失去了 “图挂在义项上” 的核心，不建议轻易退。
- **点击去向**：v1 直接开来源页，不做 lightbox；若读者反馈需要卡内放大，再按新表面评估，不把它偷偷塞进气泡。

## 10 被否掉的方案

| 方案 | 结论 | 原因 |
|---|---|---|
| 本地图片包 | 不做 | 选词、审图、许可与分发工作量过大；本次选择实时搜索 + 缓存 + 兜底 |
| 图片生成 | 不做 | 对具体名词不保证事实准确，慢且贵；词义图需要真实照片 |
| 内嵌浏览器 / WebView | 不做 | 体积、安全、搜索服务条款与维护成本都不合适 |
| 抓取图片搜索结果页 | 不做 | 不稳定，容易违反服务条款，无法保证许可元数据 |
| 默认浏览器直达 | 只做兜底 | 打断阅读现场；B 的卡内体验是选它的原因 |
| 打开卡片就自动取图 | 不做 | 未经点击就发请求，增加隐私、网络与注意力成本 |
| 为图片查询再调一次 LLM | 不做 | 多一次延迟与费用；随词条响应一起返回足够 |
| 缓存原始大图 | 不做 | 体积大、解码慢；缩略图 + 来源页足够 |

## 11 完成定义

- 词条请求返回每个义项的 `imageable` / `imageQuery`，schema、解析、缓存、payload 全部打通；旧缓存不崩。
- 卡内每条可成像义项有入口；三态、1/2/3 张、署名与来源、浏览器兜底都可用。
- 图源只收 `imageQuery`，没有原句、key、安装标识；HTTPS、主机、大小、解码校验齐全。
- 缓存命中不请求，失败不空卡，过期结果不写回；关闭/换义项/换语言不串图。
- 视觉 QA 通过；i18n 零 unfinished；`UI.md`、`API.md`、`SECURITY.md`、`PRODUCT.md`、`GLOSSARY.md` 已同步。
