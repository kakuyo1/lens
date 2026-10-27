# Lens OCR 调研：同类开源产品怎么做，Lens 下一步怎么走

> 调研日期：2026-10-27。star 数为当日 GitHub 页面抓取，会继续变化。
> 这是一份调研报告，不是规格；它不直接约束行为。真正落地前，引擎切换与结果契约仍要走 ADR。
> 相关现状：`PRODUCT.md` 的「扫描管道」「取词与通道」、`docs/adr/0014`、`docs/adr/0016`、`TODO.md` 的「自动扫描的 OCR 替换方案」。

## 一页结论

Lens 的 OCR 问题不是「Tesseract 不够好」，而是 **OCR 被当成了一次文本抽取**：一张图进去，一段纯文本出来，没有词框、没有行结构、没有置信度，也没有预处理和版面重建。同类高 star 产品普遍把 OCR 当成一条管道：

```
抓屏 → 预处理 → 文本区域/行检测 → 识别（可路由、可多引擎）→ 版面重建 → 稳定/缓存 → 交给剪贴板、翻译或校对
```

Lens 目前只做了「抓屏 → 识别 → 纯文本」这一段，后四段基本空白。因此最值得先做的不是换引擎，而是：

1. **先补结果契约**：给 `capture::OcrEngine` 加行/词框/置信度，把 Tesseract 已有的 TSV 输出用起来。不换引擎，也能解锁点词、校对、低置信度重试和句子重建。
2. **先修截图路径的预处理与 PSM 路由**：100% DPI 的小字、暗色主题、低对比度是当前最大的误差源；Tesseract 官方建议 300 DPI、深色字浅色底、必要时加白边。
3. **再把扫描路径从「整窗 OCR + 文本全等」换成「行级检测/差分 + 相似度稳定」**：现在 1 Hz 抓整窗、3 帧文本必须逐字相同，既贵又脆；一个字符抖动就会把稳定计数打回 1。
4. **引擎层面，Windows.Media.Ocr 值得作为英文屏幕文本的首选**，但必须保留 Tesseract 兜底：中文 Windows 不一定安装了 en-US 的 OCR 语言包。Text-Grab、PowerToys、ShareX、Translumo 都受这个约束。
5. **RapidOCR/PaddleOCR 更适合作为中文/混排的可选包**，不建议现在默认引入；Surya 这类 650M 文档 VLM 对 Lens 的屏幕短文本是过度设计。

一句话：**先把「文本」升级成「版面」，再把「单引擎」升级成「可路由的两级引擎」，最后才谈连续识别和视频字幕。**

---

## 1. 调研范围与口径

### 1.1 样本

选了 9 个同类产品，覆盖三种形态：

| 形态 | 产品 |
|---|---|
| 截图取字工具 | Text-Grab、PowerToys Text Extractor、ShareX、NormCap、Umi-OCR |
| 翻译/学习工具 | Pot、eSearch、LunaTranslator、Translumo |
| 引擎/模型 | Tesseract、PaddleOCR、RapidOCR、EasyOCR、Surya、Windows OCR |

选择标准：star 数高、近两年仍在维护、OCR 是其核心能力之一、能读到 README 或源码结构。Pot 已于 2026-09-07 归档，保留它是因为它的插件式 provider 设计仍有参考价值。

### 1.2 证据来源

- 各仓库 README、目录结构与关键源码文件；
- 微软官方文档（Windows.Media.Ocr、Windows AI TextRecognizer、PowerToys Text Extractor）；
- Tesseract 官方 tessdoc（PSM、TSV、图像预处理建议）；
- PaddleOCR 官方 OCR pipeline 文档；
- ShareX 官方 OCR 文档。

### 1.3 口径限制

- 本报告没有做统一语料上的横向精度实测；所有「更准/更快」的判断，除引用官方数字外，都是基于产品默认值与实现方式的推断，需要用 P0 的语料实测校准。
- star 数只表示关注度，不表示质量；Pot 已归档，是反例。
- 许可证只核到仓库级；模型权重、第三方运行库的许可证要单独核对。

---

## 2. Lens 现状：一条被截断的管道

### 2.1 代码路径

| 位置 | 事实 |
|---|---|
| `src/capture/ocr_engine.h` | `OcrResult` 只有 `status` 与 `text`；没有行、词框、置信度、引擎标识、耗时 |
| `src/capture/ocr_engine.cpp` | 每次调用启动 `tesseract.exe`，PNG 走 stdin，纯文本走 stdout；固定 `--psm 3`；`-l` 为 `chi_sim+eng` 或 `eng`；超时 10 s；最大 8192 px；输出上限 1 MB；文本上限 64 KB |
| `src/capture/scan_pipeline.cpp` | 1 Hz 采样；像素指纹用于判断画面是否变化；未稳定或画面变化时继续 OCR；文本必须连续 3 帧完全相等才稳定；稳定后过滤候选；请求间隔 2 s，冷却 30 s，单批最多 5 个 |
| `src/app/duties/capture_duty.cpp` | 扫描路径抓**整个前台窗口**再 OCR；截图路径抓框选区域后 OCR，成功即 `classifyText(..., fromScreenshot=true)`，直接翻译，没有校对步骤 |
| 打包产物 | `payload/ocr` 约 **73 MB**：`libicudt75.dll` 30.7 MB、`libtesseract-5.dll` 3.3 MB、`libleptonica-6.dll` 2.8 MB、`eng.traineddata` 4.1 MB，其余为 ICU/GLib/Cairo 等依赖 |

### 2.2 截图路径

```
全局热键 → 每屏压暗蒙版 → 拖拽框选 → 隐藏蒙版 → QScreen::grabWindow 裁剪
→ tesseract stdin/stdout --psm 3 → 纯文本 → 本地分类 → 直接翻译
```

已经做对的部分：蒙版先隐藏再抓屏（ADR 0016）、单屏内框选、区域过大拒绝、OCR 不可用时给通知、预算暂停时拒绝。问题在于 OCR 之后没有任何「结果质量」环节：识别错了也直接翻译，读者看不到原文，也无法改。

### 2.3 自动扫描路径

```
前台进程白名单 → 每秒抓整个窗口 → 像素指纹
→ 未稳定或画面变化则 OCR
→ 文本归一化（小写、空白折叠）→ 与上一帧逐字相等？
→ 连续 3 帧相等 → 词根还原 → 正则/词表过滤 → 候选 → LLM
```

这条路径有三个结构性问题：

1. **整窗 OCR**：浏览器地址栏、标签栏、侧边栏、书签栏都进了 OCR；真正的正文可能只占窗口的一半。Tesseract 仍要为所有像素付时间。
2. **逐字全等**：`scan_pipeline.cpp` 的 `acceptText` 用 `current != previous` 判断变化；任何标点、空格、一个字符的抖动都会把 `matches` 重置为 1。屏幕文字在亚像素渲染、光标闪烁、动画存在时很难连续 3 帧逐字相同，所以「稳定」经常等不到。
3. **没有置信度**：过滤只靠词根、正则、168,205 词表和档位；识别出的垃圾专有名词不在词表里，反而可能被当成实体候选。

### 2.4 已经存在的伏笔

`TODO.md` 已经写了「自动扫描的 OCR 替换方案」，方向与本报告一致：

- 先实测 Tesseract `--psm 3` / `--psm 11` / `--dpi 96` 对 96 DPI 小字的影响；
- 中期候选是 Windows.Media.Ocr（系统自带、离线、带词框、不必起子进程，但依赖语言包、与 ADR 0014 冲突）与 UI Automation TextPattern（Chrome/Edge/Word/Acrobat 可直接读文字与词框，但要处理 Chromium 无障碍模式的性能开销与 `SECURITY.md` 边界）；
- 评估口径是 96/125/150% DPI 下的词框精度、整窗耗时、中文 traineddata 依赖、打包体积、隐私边界；
- 切换点是给 `capture::OcrEngine` 加词框能力，扫描侧不感知具体引擎。

这份调研的任务，就是给这个 TODO 补上外部证据、优先级和实验矩阵。

### 2.5 七个具体问题

| # | 现象 | 后果 | 证据 |
|---|---|---|---|
| 1 | 结果只有纯文本 | 无法点词、无法高亮、无法按行校对、无法做低置信度重试、无法把词框映射回屏幕 | `OcrResult` 定义 |
| 2 | 整窗 OCR | 1 Hz 下 CPU/延迟高，噪声多，正文占比低 | `scanTick` 抓 `foreground->window` |
| 3 | 逐字全等才稳定 | 一个字符抖动就重置，稳定慢或永不稳定 | `acceptText` 的 `current != previous` |
| 4 | 无预处理 | 100% DPI 小字、暗色主题、低对比度直接送 Tesseract | `recognizeImage` 只做 PNG 编码 |
| 5 | 固定 PSM 3、固定语言顺序 | 单行/段落/稀疏 UI 文本都不是最优；`chi_sim` 在前会偏向中文 | `--psm 3`、`tesseractLanguages` |
| 6 | 每次起子进程 | 冷启动 + 每次加载 traineddata；10 s 超时与 1 Hz 不匹配 | `QProcess` per call |
| 7 | 没有语料与指标 | 换引擎、调参数都无法证明变好；只能凭感觉 | 仓库无 OCR 基准语料 |

---

## 3. 同类产品：它们怎么规划 OCR

### 3.1 Text-Grab（5,057★，MIT，Windows/.NET）

**定位**：Windows 上「复制你看到的任何文字」。

**OCR 规划**：

- 四种取字方式：全屏框选、Grab Frame（可移动的 OCR 窗口）、Edit Text Window（识别后的编辑工作台）、Quick Simple Lookup（常用文本，不走 OCR）；外加浏览器扩展直接取网页可选文本。
- 四个引擎/通道：**WinAI**（Windows AI API，NPU，Copilot+ PC，官方称对手写、风格化字体、复杂版面更准）、**WinRT OCR**（Windows.Media.Ocr，默认）、**Tesseract**（可配置的替代）、**Direct Text**（UI Automation，直接读原生控件文字，完全跳过 OCR）。
- Windows OCR 会为每个识别词画 bounding box；全屏模式因此可以「点一个词就复制这个词」。
- 识别之后不是结束，而是进入「post-grab actions」：去空行、去重、修 GUID、插入当前应用、打开搜索；这些动作可保存为 Grab Templates。

**值得借鉴**：

- 引擎不是单选题，而是按场景路由：系统 OCR 默认、Tesseract 兜底、UIA 走捷径、WinAI 在支持的硬件上加速。
- 词框不是调试信息，而是产品能力：点词、框选、模板区域都依赖它。
- 「识别 → 校对 → 动作」是一段工作流，不是一次 API 调用。

**不适用 Lens 的部分**：Text-Grab 是通用取字工具，编辑工作台很重；Lens 的沉浸优先原则不允许把每次截图都变成编辑器。

### 3.2 PowerToys Text Extractor（140,044★，MIT，Windows）

**定位**：PowerToys 里的轻量 OCR 工具，代码基于 Text-Grab。

**OCR 规划**：

- 默认快捷键 `Win+Shift+T`，全屏覆盖层，按住主键拖拽框选，松开即识别并复制到剪贴板。
- 使用 Windows OCR；语言取决于系统安装的 OCR 语言包，可在设置里选 preferred language。
- 没有编辑窗口，没有历史，没有多引擎；识别完即结束。
- 官方文档直接给出查询/安装 OCR 语言包的 PowerShell 命令，说明「语言包缺失」是这类工具的头号支持问题。

**值得借鉴**：

- 对「印刷体屏幕文字」这个主场景，系统 OCR 足够好，零配置、零打包、低延迟。
- 语言包是硬约束，必须在 UI 里说清楚，而不是静默失败。

**不适用 Lens 的部分**：PowerToys 只做「复制」，不做词形还原、候选过滤、解释；Lens 的下游更重。

### 3.3 ShareX（40,458★，GPL-3.0，Windows/.NET）

**定位**：截图/录屏/上传/工作流自动化的大而全工具。

**OCR 规划**：

- OCR 是截图后的一个 task，也是 Tools → OCR 的独立入口。
- 使用 Windows OCR；识别在本机完成，只有读者主动选择搜索/翻译服务时才把文本发出去。
- 暴露三个高杠杆旋钮：**语言**、**scale factor**（放大后再识别，改善小字/低分辨率）、**single line**（把结果合成一行，适合序列号、命令、地址）。
- 结果窗口保持打开，可校对；改语言/缩放/单行会**对当前图重新识别**；支持 silent mode 直接进剪贴板。
- 文档明确建议：只框文字、选对语言、小字提高 scale、保持水平、段落不要开 single line。

**值得借鉴**：

- 不需要复杂的预处理管线，先把「缩放」「单行」「语言」三个旋钮做出来，就能覆盖大量屏幕文字。
- 识别结果可编辑、可重跑，是比「一次成功」更现实的产品假设。
- OCR 与搜索/翻译解耦：识别本地，出站由读者决定。

### 3.4 NormCap（2,760★，Python，跨平台）

**定位**：把屏幕上的信息抓成文本，而不是图片。

**OCR 规划**：

- Tesseract 为识别引擎，zxing-cpp 做二维码/条码。
- 代码结构把 **detection** 独立成模块：`normcap/detection/detector.py`、`normcap/detection/ocr/`、`normcap/detection/codes/`；抓屏、检测、识别、剪贴板各自分层。
- 跨平台打包（Windows MSI/ZIP、Linux Flatpak/AppImage、macOS DMG、PyPI），但依赖外部 Tesseract 与语言数据。

**值得借鉴**：

- 「检测」与「识别」分开：先找文字区域，再识别，是控制成本和噪声的第一步。
- 剪贴板优先，结果不打断当前应用。

**不适用 Lens 的部分**：Python + 外部 Tesseract 的部署方式不适合 Lens 的单文件安装包；但分层思想可直接借用。

### 3.5 Umi-OCR（48,232★，MIT，Windows/Linux，Python + Qt）

**定位**：免费、开源、可批量的离线 OCR 软件。

**OCR 规划**：

- 两个离线引擎：**PaddleOCR-json**（速度稍快）与 **RapidOCR-json**（兼容性好），可导入插件切换。
- 四种入口：截图 OCR、批量 OCR、文档 OCR（双层可搜索 PDF）、二维码/公式。
- **文本后处理是一等公民**：多栏/单栏 × 按自然段换行/总是换行/无换行/保留缩进/不做处理；自动处理横排与竖排（从右到左）。
- **忽略区域**：右键画多个矩形，区域内的整个文本块被忽略（不是单字符），用于水印、页眉页脚。
- 提供命令行与 HTTP 接口，能被外部程序调用。

**值得借鉴**：

- 版面后处理不是锦上添花：多栏、段落、缩进、竖排，决定了识别结果能不能直接读。
- 忽略区域是低成本高收益的「检测」替代品：不需要模型，读者画框即可。
- 引擎插件化 + 外部 API，让工具能被其他程序复用。

**不适用 Lens 的部分**：Umi-OCR 是通用 OCR 工作台，功能面很宽；Lens 只需要「屏幕上的英文句子」这一窄场景，不应复制它的批量/PDF/二维码。

### 3.6 Pot（19,355★，GPL-3.0，Tauri，跨平台；2026-09-07 归档）

**定位**：跨平台划词翻译与 OCR。

**OCR 规划**：

- 内置多 provider：系统 OCR（Windows 用 `Windows.Media.OCR`、macOS 用 Apple Vision、Linux 用 Tesseract）、Tesseract.js、百度/腾讯/火山/讯飞/腾讯图片翻译/百度图片翻译/SimpleTex、OCR.space，以及 Rapid、Paddle 离线插件。
- 插件系统 `.potext`，可扩展 OCR/翻译/TTS；本地 HTTP API（默认 60828）支持外部截图工具把图片放进缓存目录再调用 `/ocr_recognize?screenshot=false`。
- 截图翻译、划词翻译、输入翻译、剪贴板监听四种入口。

**值得借鉴**：

- provider 注册表 + 统一接口，是「多引擎」最省事的实现方式；Lens 不需要插件市场，但需要一个 `OcrRouter`。
- 外部 API 让截图工具与 OCR 解耦，适合自动化，但也扩大了攻击面；Lens 不需要。

**反例**：Pot 已归档。插件系统与跨平台打包会带来长期维护成本，这是 Lens 在只有两个引擎时不应做插件系统的原因之一。

### 3.7 eSearch（7,365★，GPL-3.0，Electron，跨平台）

**定位**：截图、OCR、翻译、搜索、录屏一体的信息工具。

**OCR 规划**：

- 默认离线 OCR：eSearch-OCR，基于 PaddleOCR；可选百度、有道在线 OCR。
- 框选后按回车即识别，也可配置「框选后自动 OCR」。
- 离线 OCR 支持**段落识别**、**基于标点的分段**、**竖排文本**、**旋转图片**。
- 识别结果可后期编辑；在图片上选文字，选区同步到编辑区，方便校对。
- OCR 后可调用翻译，支持选词翻译。

**值得借鉴**：

- PaddleOCR 在段落、竖排、旋转上的能力，明显强于 Tesseract 的默认 PSM。
- 「图上选区 ↔ 编辑区」同步，是词框能力的高价值用法；Lens 可以用它做「点词取词」和「识别校对」。

### 3.8 LunaTranslator（13,920★，GPL-3.0，Python + Qt，Windows）

**定位**：视觉小说/游戏翻译器。

**OCR 规划**：

- OCR 引擎可切换：内置 OCR、WindowsOCR、百度 OCR、有道 OCR 等。
- 代码把 **text source** 与 **OCR engine** 分开：`textio/` 管剪贴板、OCR、文本钩子等来源，`ocrengines/` 管识别引擎，`translator/` 管翻译，`transoptimi/` 管译后优化；`CVUtils.py` 承担图像处理。
- 面向连续文本流：游戏/字幕的文字一帧一帧出现，必须有稳定的取词与去重。

**值得借鉴**：

- 「文本从哪来」与「文本怎么识别」是两个维度；Lens 现在把选区、截图、扫描都塞进 `CaptureDuty`，未来可以把它们抽成 text source。
- 多引擎不是为了炫技，而是不同游戏/字体/背景的兜底。

### 3.9 Translumo（5,880★，Apache-2.0，.NET 8，Windows）

**定位**：面向游戏与硬字幕的实时屏幕翻译器。

**OCR 规划**：

- 当前引擎：**Windows OCR**（推荐，快）与 **EasyOCR**（不常见字体/复杂背景更稳，但慢、需要 NVIDIA GPU）；2026-09 已移除 Tesseract。
- **多引擎同时识别，再用一个机器学习模型给每个结果打分，选最好的那个**。
- 代码结构：`Translumo.OCR` 有 `IOCREngine`、`OcrEnginesFactory`、`WindowsOCR/`、`EasyOCR/`；`Translumo.Processing/TextProcessing` 有 `TextDetectionProvider`、`TextValidityPredictor`、`TextResultCacheService`、`TextTokenizer`。
- `IOCREngine` 只返回 `string[] GetTextLines(byte[] image)`，但每个引擎带 `PrimaryPriority`、`SecondaryPrimaryCheck`、`Confidence`、`DetectionLanguage`。
- `TextValidityPredictor` 按语言加载一个小模型，对 OCR 结果打「像不像该语言」的分；`TextDetectionProvider` 把引擎结果、预处理文本、validity score 一起返回；`TextResultCacheService` 缓存结果，避免重复翻译。
- 产品上强调「最小捕获区域」：框越小，背景随机字母越少，延迟越低。

**值得借鉴**：

- 多引擎不必每帧都跑；可以在低置信度、首次识别、引擎切换时跑，用打分选优。
- 「有效性预测」比「词表过滤」更通用：它不要求词在词表里，能识别「这串字符根本不像英文」。
- 缓存与最小捕获区域，是低延迟的两个便宜手段。

**不适用 Lens 的部分**：Translumo 的 ML 打分模型需要额外模型文件与训练数据；Lens 可以先用词表 + 字符分布 + 置信度做一个轻量版。

### 3.10 横向对比

| 项目 | Stars | 栈 | 主力 OCR | 词框/版面 | 连续识别 | 关键机制 | 许可/状态 |
|---|---:|---|---|---|---|---|---|
| Text-Grab | 5,057 | Windows/.NET | WinRT OCR；WinAI/Tesseract/UIA 可选 | 词框；点词 | 否 | 引擎路由 + 词框 + 后处理动作 | MIT |
| PowerToys Text Extractor | 140,044 | Windows/C++/.NET | Windows OCR | 引擎提供词框（未暴露点词） | 否 | 极简热键 + 系统 OCR | MIT |
| ShareX | 40,458 | Windows/.NET | Windows OCR | 行/换行 | 否 | scale/single line/可校对/重跑 | GPL-3.0 |
| NormCap | 2,760 | Python/Qt | Tesseract | 检测阶段 | 否 | detection 与 OCR 分层 | NOASSERTION |
| Umi-OCR | 48,232 | Python/Qt | PaddleOCR-json / RapidOCR-json | 版面后处理 | 批量/文档 | 排版预设 + 忽略区域 + HTTP/CLI | MIT |
| Pot | 19,355 | Tauri/Rust | 系统 OCR + 云 + 插件 | 行 | 截图/划词 | provider 注册表 + 插件 + HTTP | GPL-3.0，已归档 |
| eSearch | 7,365 | Electron | PaddleOCR 离线 | 段落/竖排/旋转 | 定时截图 | 段落识别 + 图上校对 | GPL-3.0 |
| LunaTranslator | 13,920 | Python/Qt | 内置 + WindowsOCR + 在线 | 行 | 是 | text source 与 engine 分离 | GPL-3.0 |
| Translumo | 5,880 | .NET 8 | Windows OCR + EasyOCR | 行 | 是 | 多引擎打分 + validity + cache | Apache-2.0 |

### 3.11 五条共性

1. **系统 OCR 正在成为默认**：Text-Grab、PowerToys、ShareX、Pot、Translumo 都把 Windows OCR 放在默认或推荐位置；Tesseract 更多作为可配置/兜底/跨平台选项。
2. **词框是分水岭**：有词框，才有「点词」「校对」「按行稳定」「句子重建」；没有词框，只能做「一段文本」。
3. **版面后处理是产品能力**：Umi-OCR 的排版预设、eSearch 的段落/竖排、Surya 的 reading order，都在解决「识别出来但读不了」的问题。
4. **连续识别靠检测、差分、缓存，不靠整窗 OCR**：Translumo 的最小捕获区域、LunaTranslator 的 text source、Umi-OCR 的忽略区域，都是「先缩小范围」。
5. **多引擎是兜底，不是默认**：Translumo 用打分选优，Pot 用插件扩展；但日常路径仍是单引擎，避免每帧付多份成本。

---

## 4. 引擎：路线与取舍

### 4.1 路线

| 路线 | 代表 | 优点 | 代价 | Lens 适配 |
|---|---|---|---|---|
| 系统 OCR | Windows.Media.Ocr | 零打包、离线、快、有词框、系统级语言包 | 依赖已安装的 OCR 语言包；旧 API 无置信度；WinRT/COM 集成 | **高**：英文屏幕文本首选，需 Tesseract 兜底 |
| 系统 AI OCR | Windows AI TextRecognizer | NPU、更快更准、带 polygon 与 MatchConfidence | 仅 Copilot+ PC；需 Windows App SDK；模型可能需下载 | **中**：可选加速，不做基线 |
| 经典开源 | Tesseract 5 | 离线、Apache-2.0、100+ 语言、TSV/hOCR 词框+置信度、可配置 | 预处理敏感；屏幕小字弱；每次子进程冷启动；包体大 | **高**：现有基线，先榨干 |
| 深度学习开源 | PaddleOCR / RapidOCR | det+cls+rec；中文/混排强；多边形+分数；ONNX 可内嵌 | 模型与运行时增加包体；C++ 集成成本；调参复杂 | **中高**：中文/混排可选包 |
| 文档 VLM | Surya | 版面、阅读顺序、表格、91 语言；文档级 SOTA | 650M 参数；需 GPU 或 llama.cpp；面向文档而非屏幕短文本 | **低**：不建议 |
| 云 OCR | Google/Azure/百度 | 精度高、语言多、无需本地模型 | 联网、按次计费、文本出站，违反 Lens 隐私与成本原则 | **否决**：默认不可用 |
| UI Automation | Windows UIA TextPattern | 直接读控件文字，零 OCR 误差、有词框 | 只覆盖支持 UIA 的应用；Chromium 无障碍有性能开销；读取边界需定义 | **中**：长期 text source，非 OCR 替代 |

### 4.2 引擎对比

| 引擎 | Stars | 许可 | 管道 | 输出 | 优势 | 代价 |
|---|---:|---|---|---|---|---|
| Tesseract 5 | 77,195 | Apache-2.0 | LSTM；PSM 0–13 | 纯文本、TSV/hOCR/ALTO（词框 + conf） | 离线、语言多、可配置、生态成熟 | 预处理敏感；小字/暗底弱；CLI 冷启动；Lens 包体 73 MB |
| Windows.Media.Ocr | 系统 | OS | 系统 OCR | Lines/Words + BoundingRect | 零打包、快、词框、Win10+ | 语言包依赖；无置信度；WinRT 集成 |
| Windows AI TextRecognizer | 系统 | OS | NPU OCR | Lines/Words + polygon + MatchConfidence | 更快更准、置信度、语言自动 | Copilot+ only；Windows App SDK；模型下载 |
| PaddleOCR | 91,784 | Apache-2.0 | det + textline orientation + rec | 多边形、分数、可选词框 | 中文/混排强；PP-OCRv6 50 语言；CPU/GPU | 模型/运行时较重；C++ 集成成本 |
| RapidOCR | 8,347 | Apache-2.0 | PaddleOCR 模型转 ONNX | 框 + 分数 | 轻、跨语言（C++/Python/Java/C#）、ONNX Runtime | 模型能力跟随 PaddleOCR；仍需运行时 |
| EasyOCR | 30,110 | Apache-2.0 | CRAFT + CRNN | 框 + 文本 | 80+ 语言、上手快 | 需要 GPU 才快；包体重 |
| Surya | 21,542 | Apache-2.0（代码）/ 修改版模型许可 | 650M VLM + 检测 | 版面块、reading order、表格、HTML | 文档级版面/阅读顺序 | 重；面向文档；不适合屏幕短文本 |

### 4.3 关键事实

**Windows OCR 的词框与语言包**

- `Windows.Media.Ocr.OcrEngine` 把结果拆成 `OcrLine`，每行拆成 `OcrWord`；每个词有文本、尺寸、位置（`BoundingRect`）。这正是 Text-Grab「点词复制」的基础。
- `OcrEngine.AvailableRecognizerLanguages` 列出本机可用语言；`TryCreateFromLanguage` 创建指定语言引擎；`MaxImageDimension` 给出最大边长。
- 语言包是独立的 Windows capability（`Language.OCR~~~en-US` 等），不是装了英文键盘就有。PowerToys 文档专门给出 `Get-WindowsCapability -Online | Where-Object { $_.Name -Like 'Language.OCR*' }` 的查询方式。
- **对 Lens 的含义**：中文 Windows 上 en-US OCR 包可能缺失；不能假设 `Windows.Media.Ocr` 一定能读英文。必须探测 + 回退 + 明确告知。

**Windows AI TextRecognizer**

- 官方文档明确：它“runs exclusively on devices with a neural processing unit (NPU)”，比 `Windows.Media.Ocr` 更快更准；输出包括字符、词、行、多边形边界和每个匹配的置信度（`MatchConfidence`）。
- 需要 `Microsoft.Windows.AI.Imaging.TextRecognizer`、Windows App SDK、Copilot+ PC；模型未就绪时要 `EnsureReadyAsync`。
- **对 Lens 的含义**：适合作为可选加速层，不能作为唯一引擎；否则会把用户群切掉一大块。

**Tesseract 的 TSV**

- `tesseract image stdout -l eng tsv` 输出 `level/page_num/block_num/par_num/line_num/word_num/left/top/width/height/conf/text`；`level` 5 是词，4 是行，3 是段落，2 是块。
- 这意味着 Lens 不需要换引擎，就能拿到词框与置信度；只是当前只取了 `stdout` 的纯文本。
- Tesseract 官方建议：至少 300 DPI；4.x 要深色字浅色底；太紧的裁剪要加白边；倾斜要纠偏；PSM 要按内容选（单行 7、单块 6、稀疏 11、自动 3）。

**PaddleOCR / RapidOCR**

- PaddleOCR 的通用 OCR pipeline 是 5 个模块：文档方向分类、文本图像矫正、文本行方向分类（三个可选）+ 文本检测 + 文本识别；检测输出 `dt_polys`/`dt_scores`，识别输出 `rec_texts`/`rec_scores`/`rec_polys`/`rec_boxes`，可选 `return_word_box`。
- PP-OCRv6（2026-06）用单一模型支持 50 种语言，tiny/small/medium 三档参数量 1.5M/7.7M/34.5M；官方称 medium 比 PP-OCRv5_server 检测 +4.6%、识别 +5.1%，CPU（OpenVINO）5.2× 加速。
- RapidOCR 把 PaddleOCR 模型转成 ONNX，提供 C++/Python/Java/C# 组件，默认中英文；Umi-OCR 的 Rapid 引擎就是它。
- **对 Lens 的含义**：如果要做中文/混排、竖排、段落，PaddleOCR/RapidOCR 比 Tesseract 合适；但它是第二条引擎线，应该可选、可下载，而不是默认塞进安装包。

**Surya**

- 650M 参数 VLM，布局/OCR/表格共用一个模型；91 语言；olmOCR-bench 83.3%；RTX 5090 上 5 pages/s。面向文档，不是屏幕短文本。
- **对 Lens 的含义**：不建议。Lens 的输入是几百像素高的屏幕区域，不需要文档级 VLM；引入它会把安装包、内存、延迟都推高一个量级。

### 4.4 对 Lens 的引擎判断

```
截图 / 扫描
  ├─ 前台应用支持 UIA TextPattern? ── 是 → 直接读控件文本（OCR 兜底）
  ├─ 系统 OCR 语言包匹配? ────────── 是 → Windows OCR（快、词框）
  ├─ 中文/混排且装了 Rapid 可选包? ── 是 → RapidOCR
  ├─ 低置信度 / 首次识别? ────────── 是 → 另一引擎交叉验证
  └─ 其余 ───────────────────────────→ Tesseract（现有兜底）
```

| 场景 | 首选 | 兜底 | 理由 |
|---|---|---|---|
| 英文截图（浏览器/PDF/编辑器） | Windows OCR（en-US 包存在时） | Tesseract eng | 快、零打包、词框；Tesseract 覆盖语言包缺失 |
| 中文/混排截图 | Windows OCR（zh-Hans 包存在时）或 Tesseract chi_sim | RapidOCR 可选包 | 系统包覆盖有限；RapidOCR 对中文更稳 |
| 自动扫描 | Windows OCR（快，适合 1 Hz） | Tesseract | 扫描是高频路径，延迟优先 |
| 低置信度/重试 | 另一引擎交叉验证 | — | 多引擎只在失败时付成本 |
| 原生应用文本 | UIA TextPattern | OCR | 零误差、有词框；但只覆盖支持 UIA 的应用 |
| Copilot+ PC | Windows AI TextRecognizer | Windows OCR | NPU 加速 + 置信度；不作为基线 |

---

## 5. 管道拆解：每一段该做什么

### 5.1 Capture：先抓对，再谈识别

**同类做法**：

- 截图前隐藏自己的蒙版/浮层（Lens 已做）。
- 抓「冻结帧」，避免 OCR 期间屏幕变化导致文字撕裂。
- 按物理像素抓，按逻辑坐标映射词框；多屏/DPI 用 monitor handle，不用屏幕友好名（Lens 已有 `monitorOf`）。
- 能抓窗口就不抓全屏；能抓内容区就不抓窗口装饰。

**Lens 差距**：没有词框，所以没有「框坐标 → 屏幕坐标」的映射；扫描抓整窗，包含大量非正文区域。

### 5.2 Preprocess：为引擎准备图，不是为人眼准备图

**同类做法**：

- ShareX 的 scale factor：小字放大后再识别。
- Tesseract 官方：300 DPI、深色字浅色底、加白边、纠偏、去噪。
- 暗色主题：先反色，再识别；或先估计背景亮度，决定是否反色。

**建议的 Lens 预处理顺序**：

```
裁切 → 估计文字高度 → 小于阈值则放大（2×/3×，双三次）
→ 灰度 → 对比度拉伸（1%–99% 分位）
→ 暗底反色（背景均值 < 0.5 且文字比背景亮）
→ 加 10 px 白边（可选）
→ 送引擎
```

**注意**：放大不是越多越好；过度放大会让 Tesseract 的行分割变慢、变差。以「大写字母高度 20–30 px」为目标，而不是固定 2×。

### 5.3 Detect：不要为不需要的像素付钱

**同类做法**：

- NormCap 把 detection 独立成模块。
- PaddleOCR 先检测文本框，再逐框识别。
- Umi-OCR 用「忽略区域」排除水印/页眉页脚。
- Surya 做版面块与阅读顺序。

**Lens 可选的三级方案**：

1. **便宜级**：用引擎自带的检测结果（Tesseract TSV 的 block/line、Windows OCR 的 line boxes）做「行级」裁剪与差分；不额外做 CV。
2. **中间级**：对整窗做下采样，计算边缘密度/梯度方差，找出文本样区域，裁掉浏览器 chrome、侧栏、大片空白。
3. **长期级**：UIA TextPattern 直接给出控件文本与矩形，完全跳过 OCR。

不建议现在上 MSER/EAST 之类的文本检测模型：Lens 的截图路径已经由用户框选，扫描路径的窗口结构相对稳定，先用「引擎自带框 + 简单启发式」就够。

### 5.4 Recognize：引擎路由 + PSM 路由 + 语言路由

**引擎路由**：见 4.4。

**PSM 路由**（Tesseract）：

| 输入形态 | PSM | 说明 |
|---|---|---|
| 单个词 | 8 或 10 | 单词/单字符 |
| 单行（高度 < 40 px 或宽高比 > 6） | 7 | 单行文本 |
| 段落/正文块 | 6 或 4 | 单块/单列 |
| 整窗/稀疏 UI | 11 | 稀疏文本，不假设版面 |
| 未知 | 3 | 默认，最后兜底 |

**语言路由**：

- 默认 `eng`。Lens 的用户是「有英语基础的成人学习者」，主场景是英文阅读；`chi_sim+eng` 会把中文模型放在前面，既慢又可能把英文词读成中文。
- 仅当裁剪区域里 CJK 字符占比超过阈值（例如 5% 的非空白字符），或读者显式选择「中英混排」，才用 `chi_sim+eng`。
- 更好的做法是「按行判语言」：先 `eng` 跑一遍，若某行中文比例高，再对该行用 `chi_sim` 重跑；成本高，适合 P3。

**多引擎**：

- 不要在每帧跑两个引擎。只在「首次识别」「低置信度」「读者点重试」「引擎不可用」时交叉验证。
- 交叉验证的选优规则：置信度高者优先；置信度相近时，词表命中率高者优先；仍相近时，行长/字符分布更「像英文」者优先。

### 5.5 Reconstruct：把「词」拼回「句子」

**同类做法**：

- Umi-OCR 的多栏/单栏、按自然段换行、保留缩进。
- eSearch 的段落识别、标点分段、竖排、旋转。
- PaddleOCR 的 PP-StructureV3 输出 Markdown/JSON，带表格单元格与文本坐标。
- Surya 的 reading order。

**Lens 需要的重建**（按优先级）：

1. **行合并**：同一段落的多行合并为一句；列表、代码、标题不合并。
2. **断词修复**：行尾 `-` + 下一行小写开头 → 去掉连字符合并；行尾无连字符但被硬换行 → 用空格合并。
3. **CJK/拉丁混排**：中文与英文/数字之间补空格；全角标点转半角（仅用于匹配，显示可保留）。
4. **连字/特殊字符**：`ﬁ ﬂ ﬀ ﬃ ﬄ` → `fi fl ff ffi ffl`；智能引号统一。
5. **阅读顺序**：单栏按 y 排序；多栏先分栏再按栏内 y 排序；竖排从右到左（Lens 主场景不需要，但中文网页可能有）。
6. **句子边界**：用标点 + 大写 + 行长度做启发式；不要用 LLM。

**为什么重要**：Lens 的句子通道要发「选中的句子」，实体通道要判断「句中大写」。没有行合并，截图里的段落会被当成多行碎片，翻译和实体判定都会变差。

### 5.6 Stabilize：稳定是独立问题，不是识别精度问题

**同类做法**：

- Translumo 用 `TextResultCacheService` 缓存、`TextValidityPredictor` 打分、最小捕获区域降低噪声。
- Umi-OCR 用忽略区域减少水印/页脚干扰。
- 视频字幕工具通常要求「连续 N 帧文本相似」而不是「完全相等」。

**Lens 现状**：`normalizedText` 后逐字相等，任何差异重置 `matches`。

**建议**：

- 用**相似度**替代全等：归一化后计算 Levenshtein ratio 或 token Jaccard；阈值 0.9–0.95，连续 2–3 帧。
- 用**行级差分**：把上一帧的行与当前帧的行按位置/文本匹配；只有变化的行才重新识别/重新计分。
- 用**像素变化率**做前置门：整窗像素变化率低于阈值时，跳过 OCR；高于阈值时，才进入行级比较。
- 用**冷却**避免同一段文字反复弹：现有 30 s 冷却可保留，但键从「全文」改为「段落/行指纹」。
- 视频：变化率持续高于阈值时直接跳过，不把视频文字当稳定文本；字幕模式另做。

### 5.7 Consume：识别结果要给谁、怎么给

**同类做法**：

- Text-Grab：剪贴板、编辑窗口、模板动作。
- ShareX：剪贴板、可校对窗口、搜索/翻译服务。
- Umi-OCR：剪贴板、批量文件、PDF、HTTP。
- eSearch：图上选区 ↔ 编辑区、翻译、搜索。
- Translumo：翻译浮层、TTS、Anki。

**Lens 的选择**：

- 截图：识别 → 可选的「校对/重试」→ 分类 → 翻译。校对只在低置信度或长文本时出现，避免打断。
- 扫描：识别 → 行级稳定 → 候选 → 浮层；不把整段文本发出去（发送最小化原则不变）。
- 词框：用于点词、气泡锚点、识别高亮；不改变现有「发送最小化」的边界。

---

## 6. 对 Lens 的具体建议

### 6.1 结果契约：从 `OcrResult` 到 `OcrDocument`

**现状**：

```cpp
struct OcrResult {
    OcrStatus status;
    QString   text;
};
```

**建议**（保持向后兼容，先加字段，不改调用方）：

```cpp
struct OcrWord {
    QString text;
    QRectF  box;          // 输入图像坐标系，像素
    float   confidence;   // 0..1；无置信度的引擎填 -1
};

struct OcrLine {
    QString text;
    QRectF  box;
    std::vector<OcrWord> words;
    float   confidence;
};

struct OcrResult {
    OcrStatus status;
    QString   text;                 // 扁平文本，保持现有语义
    std::vector<OcrLine> lines;     // 新增：版面
    QString   engine;               // "tesseract" / "windows" / "rapid"
    QString   language;             // "eng" / "chi_sim+eng" / "en-US"
    int       durationMs = 0;
    float     meanConfidence = -1;  // 无置信度时为 -1
};
```

**坐标约定**：

- `box` 用输入图像的像素坐标；调用方知道裁剪原点与 DPR，负责映射回屏幕逻辑坐标。
- 不把屏幕坐标塞进 `capture` 层：`capture` 不应知道窗口/屏幕，保持现在的分层。
- 词框用于点词时，允许 `box` 略微外扩 2 px，提升点击命中。

**状态扩展**：

- 现有 `OcrStatus` 增加 `LanguagePackMissing`（Windows OCR 专用）或复用 `LanguageMissing`，但要在 `ocrStatus()` 里区分「Tesseract 语言数据缺失」与「Windows OCR 语言包缺失」，否则设置页无法给出正确指引。
- `probe()` 返回可用语言列表，而不只是 `Ready`；设置页可以显示「系统英文 OCR 包：可用/缺失」。

### 6.2 引擎路由：两级引擎 + 一个路由器

**接口**：

```cpp
struct OcrCapabilities {
    bool wordBoxes;
    bool confidence;
    QStringList languages;
    int maxDimension;
};

struct OcrEngine {
    virtual OcrStatus probe(...) const = 0;
    virtual OcrCapabilities capabilities() const = 0;
    virtual OcrResult recognize(QImage const&, ...) const = 0;
};
```

**路由器**：

```cpp
enum class OcrEngineChoice { Auto, Windows, Tesseract, Rapid };
```

- `Auto`：Windows OCR 可用且语言包匹配 → Windows；否则 Tesseract；将来 Rapid 作为中文包可选。
- `Windows`：强制；不可用则返回 `LanguagePackMissing`/`RuntimeMissing`，不回退（让读者看到原因）。
- `Tesseract`：强制；保持现有行为。
- `Rapid`：可选包安装后才出现。

**设置**：

- 新增一行 `ocrEngine`（Auto/Windows/Tesseract），遵守 ADR 0083 的「一行一个设置」。
- 语言不新增全局开关：沿用「数据目录里有 chi_sim 就中英混排」的现有规则，但改为「按内容路由」，并在设置页用一行小字说明。
- 现有 `tesseractExecutable` / `tesseractDataDirectory` 保留，作为 Tesseract 兜底的路径。

**ADR**：

- ADR 0014 说「截图与扫描统一使用 Tesseract」。引入 Windows OCR 需要新 ADR（或修订 0014），写明：
  - 引擎可替换，结果契约不变；
  - 默认仍是本地 OCR，不上云；
  - Windows OCR 只在语言包可用时启用，Tesseract 始终兜底；
  - 两条路径（截图/扫描）可以共用同一路由器，保证结果一致。

### 6.3 截图路径：先让读者能校对

**P1 改动**：

1. 预处理 + PSM 路由（见 5.2/5.4）。
2. Tesseract 输出 TSV，解析行/词框/置信度。
3. 低置信度（例如平均 conf < 60）或长文本（> 120 字符）时，不直接翻译，先显示一个小校对条：
   - 原文可编辑；
   - 「重试」按钮：用更紧的裁剪 / 单行 PSM / 反色重跑；
   - 「翻译」按钮：确认后走现有通道。
4. 单行/单词截图自动选 PSM 7/8，不再用 PSM 3。

**P2 改动**：

1. 词框 → 鼠标悬停高亮；点击某个词，直接取该词（类似 Text-Grab）。
2. 识别结果与截图叠加显示，读者能看出哪一段被读错。
3. 多显示器/DPI：词框按 DPR 映射，气泡锚点用词框中心而不是选区左上角。

**产品边界**：

- 校对条不是编辑器，不提供 Text-Grab 那样的批量清洗/正则/表格转换。
- 默认不显示校对条；只在「可能不准」时出现，保持沉浸。

### 6.4 自动扫描：从整窗到行级

**P1 低成本改动**：

1. 抓窗后先下采样到最大宽度 1280 px 做「文本样区域」估计；只对包含文字的带状区域做 OCR。没有可靠区域时，退回整窗。
2. 用 Tesseract TSV 的行框做行级缓存：上一帧的行文本/位置与当前帧匹配，未变化的行不重复计分。
3. 稳定判定改为「相似度 ≥ 0.92，连续 2 帧」，而不是「逐字相等，连续 3 帧」。
4. 像素变化率低于阈值（例如 0.5%）时直接跳过 OCR；高于阈值（例如 15%）时判定为视频/动画，跳过稳定逻辑。

**P2 改动**：

1. 引入 Windows OCR 后，直接用 `OcrLine` 的框做行级差分；不再依赖 Tesseract TSV。
2. 候选提取带「所在行」上下文：实体通道可以用整行判断 `Apple` 是水果还是公司；句子通道可以用行合并后的句子。
3. 自适应采样：静止页面 1 Hz；文本变化后 2 Hz 追 2–3 帧；视频/全屏动画暂停。

**P3 改动**：

1. UIA TextPattern：Chrome/Edge/Word/Acrobat 直接读文本与词框，OCR 只做兜底。
2. 字幕模式：固定字幕区域 + 2 帧稳定 + 行级缓存；这是 `PRODUCT.md` 里「视频扫描」的前置。

### 6.5 后处理：用本地词表做「保守纠错」

**原则**：只修「明显不像词」的 token，不动合法词、专有名词、实体。

**规则**：

1. 连字：`ﬁ ﬂ ﬀ ﬃ ﬄ` → `fi fl ff ffi ffl`。
2. 断词：行尾 `-` + 下一行小写 → 合并；行尾无连字符 → 空格合并。
3. 空白：多空格折叠；CJK 与拉丁之间补空格；全角标点转半角（仅匹配用）。
4. 引号：`“”‘’` → `"'`（匹配用）；显示可保留原样。
5. 常见混淆：`rn`↔`m`、`l`↔`I`↔`1`、`O`↔`0`、`S`↔`5`；只在 token 不在 168,205 词表、且编辑距离 1 能命中词表时替换。
6. 置信度：Tesseract `conf < 60` 的词标记为「不确定」；不确定词不进入自动弹词，但保留在句子里供翻译/上下文。
7. 不做：LLM 纠错、云端拼写检查、把专有名词强行改成词典词。

**为什么不用 LLM**：违反「本地判定，模型只解释」；会把 OCR 文本发给模型，增加成本与隐私面；而且 LLM 可能把正确的人名/术语改错。

### 6.6 质量闭环：没有语料就没有迭代

**语料**：

- 60–100 张真实截图，覆盖：
  - 浏览器正文、PDF、代码/终端、UI 标签、视频字幕；
  - 100%/125%/150%/200% DPI；
  - 浅色/暗色主题；
  - 英文为主、中英混排、少量专有名词。
- 每张图配：人工转写的 ground truth（按行）、词框（可选，先做 20 张）。
- 放在 `test/fixtures/ocr/`（图片 + `expected.txt` + `meta.json`），不放进安装包。

**指标**：

| 指标 | 说明 | 目标（待基线校准） |
|---|---|---|
| WER / CER | 词/字符错误率 | 新引擎 ≤ 基线；P1 预处理后 100% DPI 小字 WER −30% |
| 行框 F1 | 行检测与 ground truth 的 IoU ≥ 0.5 | ≥ 0.9 |
| 词框 IoU | 词级定位 | ≥ 0.8（有词框的引擎） |
| 延迟 p50/p95 | 单次识别 | 截图 p95 < 1.5 s；扫描 p95 < 800 ms |
| CPU/内存 | 1 Hz 扫描 10 分钟 | 不高于基线 |
| 安装包增量 | 引擎 + 模型 | Windows OCR 0 MB；Rapid 可选包按实测，目标 ≤ 50 MB |
| 语言包覆盖 | 测试机 en-US/zh-Hans | 缺失时可回退且提示 |

**记录**：

- `scripts/qa/ocr-bench.ps1` 跑矩阵，输出 CSV + Markdown 摘要；
- 每次引擎/预处理/PSM 改动，附一份 `docs/metrics/ocr-YYYY-MM-DD.md`；
- 误弹反馈进入语料，形成回归。

### 6.7 不做什么

- **不做云 OCR 默认路径**：违反隐私与成本原则；如要做，必须是显式 opt-in、单独 ADR、单独计费提示。
- **不做插件系统**：两个引擎 + 一个路由器足够；Pot 的归档是前车之鉴。
- **不引入文档 VLM**：Surya 这类模型对屏幕短文本是过度设计。
- **不让 LLM 纠错**：本地词表 + 规则足够，且不破坏「模型只解释」。
- **不每帧跑多引擎**：只在低置信度/重试时交叉验证。
- **不继续整窗 1 Hz OCR**：这是当前 CPU/噪声/稳定性的共同根源。

---

## 7. 路线图

### P0：度量与契约（3–5 天）

**做什么**：

1. 建 60–100 张语料与 ground truth；先做 20 张词框标注。
2. `scripts/qa/ocr-bench.ps1`：跑 Tesseract（psm 3/6/7/11，预处理开关）与 Windows OCR（如可用），输出 CSV/Markdown。
3. 给 `OcrResult` 加 `lines/engine/language/durationMs/meanConfidence`，先只填 Tesseract TSV；调用方暂不消费。
4. 记录基线：WER、行框 F1、p50/p95、CPU、安装包体积。

**验收**：一份基线报告，能回答「哪个引擎在哪种 DPI/主题/内容上更好」；不改变用户可见行为。

### P1：榨干 Tesseract（1–2 周）

**做什么**：

1. `ocr_preprocess`：灰度、对比度、暗底反色、放大到目标字高、白边。
2. PSM 路由 + 语言路由（默认 eng，CJK 比例高才 chi_sim+eng）。
3. TSV 解析 → 行/词框/置信度；截图路径先消费置信度。
4. 稳定判定改相似度；行级缓存。
5. 低置信度/长文本的校对条（可放 P1.5）。

**验收**：

- 100% DPI 小字 WER 相对基线 −30%；
- 截图 p95 < 1.5 s；
- 自动扫描误弹 −50%；
- 现有 gtest/qtest 不回归。

### P2：Windows OCR 路由器（2 周）

**做什么**：

1. `WinOcrEngine`（C++/WinRT）：probe 语言包、`TryCreateFromLanguage`、`SoftwareBitmap` 转换、行/词框输出。
2. `OcrRouter` + `ocrEngine` 设置 + 状态提示。
3. 新 ADR：引擎可替换，Windows OCR 在语言包可用时优先，Tesseract 兜底。
4. 安装包继续带 Tesseract；测量「去掉 Tesseract」的收益，但暂不删。

**验收**：

- en-US 包可用的机器上，p95 < 400 ms，WER ≤ Tesseract；
- en-US 包缺失的机器上，自动回退 Tesseract，设置页明确提示；
- 中英混排不劣化。

### P3：扫描 v2（2–3 周）

**做什么**：

1. 行级检测/差分：优先用 Windows OCR 的 line boxes，Tesseract TSV 兜底。
2. 自适应采样：静止 1 Hz、变化 2 Hz、视频暂停。
3. 候选带行上下文；句子重建。
4. 词框 → 气泡锚点/点词。

**验收**：

- 扫描 CPU −50%；
- 稳定文本 2 s 内触发；
- 视频/全屏动画不触发；
- 误弹率继续下降。

### P4：可选与探索（按需）

| 方向 | 价值 | 风险 | 建议 |
|---|---|---|---|
| RapidOCR 中文包 | 中文/混排更稳；可选下载 | 包体 + 运行时；模型许可 | 先做 1 个 spike，再决定 |
| UIA TextPattern | 零 OCR 误差、词框、句子上下文 | Chromium 无障碍性能；读取边界 | 先做 Chrome/Edge 的只读 spike |
| Windows AI TextRecognizer | NPU 加速、置信度 | Copilot+ only；SDK 依赖 | 作为可选加速，不做基线 |
| 视频字幕模式 | 产品 TODO 的明确项 | 稳定性/延迟/成本 | P3 之后再做 |

### 风险清单

| 风险 | 影响 | 缓解 |
|---|---|---|
| 中文 Windows 缺 en-US OCR 包 | Windows OCR 不可用 | 探测 + Tesseract 兜底 + 设置页提示安装 |
| WinRT/COM 集成复杂 | 崩溃/线程问题 | 先 spike，worker 线程 + 超时 + 异常兜底 |
| Tesseract 子进程冷启动 | 1 Hz 下延迟高 | 持久 worker 或 in-process libtesseract |
| 安装包变大 | 下载/安装体验 | Windows OCR 零包体；Rapid 可选下载 |
| 多引擎结果不一致 | 截图/扫描行为漂移 | 同一路由器 + 同一结果契约；按场景固定引擎 |
| 词框坐标错位 | 点词/气泡错位 | 统一「输入图像像素」坐标；调用方做 DPR 映射；加 qtest |
| UIA 读取边界不清 | 隐私/安全 | 只读、只在前台窗口、写进 `SECURITY.md`，默认关闭 |
| 过度设计 | 维护成本 | 两个引擎 + 一个路由器；不做插件系统 |

---

## 8. 实验清单（可直接执行）

### 8.1 语料

| 维度 | 取值 | 张数 |
|---|---|---|
| 内容 | 浏览器正文 / PDF / 代码终端 / UI 标签 / 视频字幕 | 各 12–20 |
| DPI | 100% / 125% / 150% / 200% | 每类覆盖 |
| 主题 | 浅色 / 暗色 | 各半 |
| 语言 | 英文 / 中英混排 / 含专有名词 | 各覆盖 |
| 总计 | — | 60–100 |

### 8.2 矩阵

| 因子 | 取值 |
|---|---|
| 引擎 | Tesseract eng；Tesseract chi_sim+eng；Windows OCR en-US；Windows OCR zh-Hans；RapidOCR（可选） |
| PSM | 3 / 4 / 6 / 7 / 11 |
| 预处理 | 无；2× 放大；灰度+对比度；暗底反色；白边 |
| 指标 | WER/CER；行框 F1；词框 IoU；p50/p95；CPU；内存；失败率 |

### 8.3 命令示例

```powershell
# Tesseract TSV：拿词框与置信度
tesseract crop.png stdout -l eng --psm 6 tsv

# 暗底反色后再识别（ImageMagick 仅用于实验）
magick crop.png -colorspace Gray -negate -contrast-stretch 1%x1% crop-pre.png
tesseract crop-pre.png stdout -l eng --psm 6 tsv
```

### 8.4 记录格式

```markdown
# OCR bench 2026-11-XX

| engine | psm | preprocess | dpi | theme | content | WER | line-F1 | p50 | p95 | notes |
|---|---|---|---|---|---:|---:|---:|---:|---|
| tesseract | 3 | none | 100 | light | browser | 0.18 | 0.82 | 620 | 980 | baseline |
| tesseract | 6 | 2x+gray | 100 | light | browser | 0.09 | 0.94 | 540 | 860 |  |
| windows | - | none | 100 | light | browser | 0.07 | 0.96 | 120 | 210 | en-US pack |
```

---

## 附录 A：证据表

| # | 来源 | 链接 | Stars（2026-10-27） | 用途 |
|---|---|---|---:|---|
| 1 | Text-Grab | https://github.com/TheJoeFin/Text-Grab | 5,057 | 引擎路由、词框、UIA、后处理 |
| 2 | PowerToys Text Extractor | https://learn.microsoft.com/en-us/windows/powertoys/text-extractor | 140,044（PowerToys） | 系统 OCR、语言包 |
| 3 | ShareX OCR 文档 | https://getsharex.com/docs/ocr | 40,458（ShareX） | scale/single line/校对 |
| 4 | NormCap | https://github.com/dynobo/normcap | 2,760 | detection 分层 |
| 5 | Umi-OCR | https://github.com/hiroi-sora/Umi-OCR | 48,232 | 版面后处理、忽略区域、引擎插件 |
| 6 | Pot | https://github.com/pot-app/pot-desktop | 19,355 | provider 注册表、HTTP API、归档反例 |
| 7 | eSearch | https://github.com/xushengfeng/eSearch | 7,365 | PaddleOCR 段落/竖排/校对 |
| 8 | LunaTranslator | https://github.com/HIllya51/LunaTranslator | 13,920 | text source 与 engine 分离 |
| 9 | Translumo | https://github.com/ramjke/Translumo | 5,880 | 多引擎打分、validity、cache |
| 10 | Tesseract | https://github.com/tesseract-ocr/tesseract | 77,195 | PSM/TSV/预处理 |
| 11 | tessdoc ImproveQuality | https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html | — | 300 DPI、反色、白边、PSM |
| 12 | PaddleOCR | https://github.com/PaddlePaddle/PaddleOCR | 91,784 | PP-OCR 管道、模型档位 |
| 13 | PaddleOCR OCR pipeline | https://www.paddleocr.ai/latest/en/version3.x/pipeline_usage/OCR.html | — | det/cls/rec 输出 |
| 14 | RapidOCR | https://github.com/RapidAI/RapidOCR | 8,347 | ONNX 轻量引擎 |
| 15 | EasyOCR | https://github.com/JaidedAI/EasyOCR | 30,110 | 多语言 DL OCR 对比 |
| 16 | Surya | https://github.com/VikParuchuri/surya | 21,542 | 文档 VLM 对比 |
| 17 | Windows.Media.Ocr | https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr.ocrengine | — | 行/词/框、语言、MaxImageDimension |
| 18 | Windows AI TextRecognizer | https://learn.microsoft.com/en-us/windows/ai/apis/text-recognition | — | NPU、polygon、MatchConfidence |

## 附录 B：术语

| 术语 | 含义 |
|---|---|
| OCR | 光学字符识别 |
| PSM | Tesseract 的页面分割模式（Page Segmentation Mode） |
| TSV | Tesseract 的表格输出，含层级、坐标、置信度、文本 |
| hOCR | 带词框与置信度的 HTML 格式 OCR 输出 |
| det / cls / rec | 文本检测 / 方向分类 / 文本识别 |
| WER / CER | 词错误率 / 字符错误率 |
| IoU | 交并比，衡量框重叠 |
| UIA | Windows UI Automation，可读控件文本与矩形 |
| WinRT | Windows Runtime，Windows OCR 的 API 形态 |
| ONNX | 开放神经网络交换格式，RapidOCR 的运行时格式 |
| ROI | 感兴趣区域，本文指「可能含文字的区域」 |
| DPR | 设备像素比，逻辑坐标与物理像素的换算 |
