<p align="center"><img src="docs/readme/brand/lens-mark.png" width="92" alt="Lens"></p>

<h1 align="center">Lens</h1>

<h3 align="center">用英文，读懂英文。</h3>

<p align="center">
  Lens 是一款常驻 Windows 系统托盘的英语阅读助手。选中一个单词、一个名字或一个句子，它会在文字旁用更清楚的英文解释——不切页，不开词典，也不把英文先翻译成中文。不取词的时候，桌面宠物会把到期的词举回来问你。
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><strong>下载 Windows 版</strong></a> ·
  <a href="#先看一眼">看它怎么工作</a> ·
  <a href="https://kakuyo1.github.io/lens/">宣传页</a> ·
  <a href="https://github.com/kakuyo1/lens/releases">全部版本</a>
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/kakuyo1/lens?label=release&color=2f9e6e"></a>
  <a href="#下载"><img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-1a1a1f"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/github/license/kakuyo1/lens?color=2f9e6e"></a>
  <a href="#语言"><img alt="英语、中文、繁体中文、西班牙语、日语" src="https://img.shields.io/badge/languages-EN%20%7C%20ZH%20%7C%20ZH--HANT%20%7C%20ES%20%7C%20JA-2f9e6e"></a>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Qt 6 / QML" src="https://img.shields.io/badge/Qt-6.2%2B%20%2F%20QML-41CD52?logo=qt&logoColor=white">
  <img alt="CMake 3.21+" src="https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white">
</p>

<p align="center"><sub><a href="README.en.md">English</a> · 简体中文 · <a href="README.zh-TW.md">繁體中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.es.md">Español</a></sub></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/readme/hero-dark.png">
    <img src="docs/readme/hero-light.png" width="100%" alt="Lens 在英文文章旁用英文解释 serendipity 这个词">
  </picture>
</p>

<p align="center"><sub>解释默认使用英文；按住卡片上的地球就能看到母语释义，松手即收。永久切换解释语言在设置里。</sub></p>

## 为什么是 Lens

大多数查词工具会把你带离页面，再给一个中文答案。你读完了一段，却没有用英文读它。Lens 的选择相反：提高英语阅读最有效的方式，是留在英语里。

- **英文进，清楚的英文出**。一条你能直接读懂的英文释义、音标；需要时再加上词源和例句。翻译仍然在，只是它不是默认动作。
- **能听见的词**。音标旁边有一枚喇叭：点它，或者点音标本身，就会读一次词头。句子、名字和译文不会出声。
- **答案留在文字旁**。Lens 常驻系统托盘，没有主窗口。气泡出现在选区旁边，鼠标停在上面就会一直留着；你继续往下读，它就退场。
- **安静的记忆**。把词标成 “已会” 或 “新词”，Lens 替你记着。没有连续打卡，也没有课程表。
- **会问你的宠物**。到期的词会在宠物脚边立起一块提问牌：答对，下一次隔得更久；答错，就提前回来。宠物只收事件类型，读不到屏幕、选区或释义。默认关闭，在设置的 “扩展” 里开启。
- **能走进去的释义**。英文释义里超出你档位的词会留下点线；点开它，同一张卡片向下走一层，并留一条面包屑带你回来。缓存、预算与判定规则跟着每一步走。

## 先看一眼

<p align="center"><img src="docs/readme/english-first.gif" width="600" alt="按住卡片上的地球，在英文释义下看到中文意思"></p>

答案仍然是英文。按住地球看母语，松手，卡片又回到英文。

<p align="center"><img src="docs/readme/feature-tour.gif" width="600" alt="在 crane 的卡片上点 “图片”，照片加载出来；点 “不是这个义项？”，卡片原地换成起重机的释义和照片"></p>

单词卡上点 “图片”，照片就加载出来；点 “不是这个义项？”，卡片原地换到另一个义项，照片跟着变。

## 英文优先，需要时再翻译

- **解释默认使用英文**。中文、日文或西班牙文的选区，都会用英文回答。
- **解释语言是设置里的一项，不在卡片上**。英语、中文、繁体中文、西班牙语或日语——改一次，之后每张卡都跟着走，已有缓存的词直接取缓存。
- **动作条提供翻译、解释和复制文本**。Lens 围绕的是 “解释”：用你已经认识的英文，说明这个词在这里做什么，而不是换成一个你可以直接跳过、不再需要读英文的中文词。
- **翻译是例外**。中文、日文或西班牙文的选区会译成英文；英文选区则按解释语言回答——默认英文，除非你改。
- **卡片上留下的语言控件只有那枚地球**。按住半秒，英文释义下方浮出母语的意思，松手即收。母语本身是设置里的一项，默认中文。解释时已经取回那门语言的词，按住不花钱；其余的词每个会话问一次。

## 读出来

每张单词卡的音标旁都有一枚喇叭：点喇叭或点音标本身，读一次词头。卡片上的其他内容不会出声——释义、句子、名字都不读。

- **默认先由 Windows 的系统音色发声**，口音按你选的美式（默认）或英式。
- **系统没有该口音的音色时**，按你在设置 › 阅读学习里选的兜底走：在线音频（默认有道，也可选 Google 翻译）、下载一次的本地模型（Kokoro 约 180 MB，或 LJSpeech 约 41 MB），或者不发音。这一档由你明确选择——Lens 不会在在线与本地之间悄悄切换。
- **自动播放默认关闭**。打开后，取词弹出的那张卡出现即读一次，同一个词不重复。
- **在线音频缓存在 `%APPDATA%\Lens\audio`**，上限 50 MB，不计入每日预算。

## 选不中的文字

按 `Ctrl+Alt+S`（可在设置中修改），框选 PDF、图片或视频画面中的一块区域，Lens 会在本机用 OCR 读取。

- **随包的 Tesseract 是自动引擎**。也可以自行安装 RapidOCR-json，明确选择它，并在一次信任检查后指向 ASCII 模型目录。
- **在线 OCR 是可选项**。每次上传前都会询问，可指向百度 OCR、腾讯云 OCR、阿里云 OCR、Google Cloud Vision 或 OCR.space。
- **自动扫描仍是实验功能，尚在开发**。它只监看你加入白名单的应用，结果尚不可靠。

## 五种动作条形态

<p align="center"><img src="docs/readme/selection-styles.gif" width="800" alt="经典长条、页边签、手牌签、吊牌签和扇面五种动作条形态"></p>

在设置 › 取词与弹窗里选择：经典长条、页边签、手牌签、吊牌签或扇面。三项动作和通道规则完全不变，复制抽屉按它展开的那副外形画。

<p align="center"><img src="docs/readme/selection-drawer.png" width="800" alt="选中一个句子时，复制抽屉列出纠正语法与拆解"></p>

复制文本是一张抽屉，不是单个动作：鼠标停在上面，只有这张选区能做的事会出来。

- 单词可修拼写，句子可修语法，80 词以内的英文句子还可拆解。
- 纠正与拆解都不入缓存，各自按一次请求计入每日预算。

<p align="center"><img src="docs/readme/breakdown.png" width="800" alt="拆解的作答：句子按四类成分染色，下方配一行图例"></p>

拆解把句子的四类成分各染一色——主语、谓语、表语、状语——下面配一行图例；纠正只给改动过的词画下划线。

## 一个安静的学习循环

<p align="center"><img src="docs/readme/learning-loop.gif" width="900" alt="小幽灵脚边的提问牌，以及单词年度记录"></p>

已会和新词在同一张词表里。

- 把指针停在某一行上，可以重读 Lens 已经存下的解释，不会发出请求。
- 单词到期时，宠物脚边会立起它自己的提问牌；回答 “忘了” 或 “记得”，它会按 10 分钟到 60 天的间隔再次被问起。
- 年度记录把阅读留下的痕迹画成一格一格的绿色，而不是一条连续打卡。

## 会把词举回来问你的宠物

<p align="center"><img src="docs/readme/pets.gif" width="900" alt="Lens 的七只像素宠物"></p>

Lens 随包提供七只像素宠物：短耳猫、猫头鹰、麻糬、小幽灵、小水母、小云和小狗。它们默认关闭，可在设置的 “扩展” 里开启。

- 每一只都有自己的九件配饰和六个场景——四个专属、两个共享；一条围巾或一副眼镜会改变它更常去哪个场景。
- 大小与穿搭按宠物各记一份，窗口位置仍只属于你。
- 它们会闲逛、在屏幕边缘躲起来、注意到指针，也能被拎起来抛出去。
- 它们只接收事件类型——从不读取你的选区、解释或屏幕文字；宠物包也只含数据。

小幽灵的六个：午夜座钟、南瓜灯、镜中无影与幽灵茶会是它自己的，敲电脑和泡澡与其他宠物共享——每个场景都是一段自己的小动画。南瓜灯播起来是这样：

<p align="center"><img src="docs/readme/pet-lantern.gif" width="420" alt="小幽灵的南瓜灯场景，正在播放"></p>

在设置的 “扩展” 里把指针停到一只宠物上，它会举起自己的明信片——按部位各穿一件，站在只属于它的风景里，贴着邮票、盖着邮戳。

<p align="center"><img src="docs/readme/pet-postcards.gif" width="700" alt="七只宠物的明信片，逐一亮相"></p>

## 还有这些

- **自带模型**。DeepSeek、OpenAI、Gemini、OpenRouter、xAI、Moonshot、智谱、通义千问，或任何兼容 OpenAI 的自定义端点。
- **看得见的花费**。按 token 计算每次请求的费用；每日预算到顶时，暂停取词、截图和请求。
- **想同步时再同步**。已会/新词标记、查词历史、统计和选定设置用你的口令加密，存放在你自己 GitHub 账号的 secret gist 里；API key 始终留在本机。
- **词表进得来也出得去**。导入或导出 `.txt` 和 `.json`；删除单词需要一次确认。
- **该给图时给图**。模型判断某条义项用照片更好说明时，卡片可以展开三张 Wikimedia Commons 图片；图片缓存在本机，“关于” 页显示缓存大小并可原地清除。
- **按你的习惯来**。五种动作条形态；晴朗白昼、静谧暗夜、雾面玻璃、流光玻璃和自定义主题；以及英文字体与 12–18 px 气泡字号。
- **五种语言**。界面和解释各自提供英语、中文、繁体中文、西班牙语和日语。繁体中文、西班牙语与日语的翻译由机器生成，尚未经母语人士审校。

## 主题

<p align="center"><img src="docs/readme/themes.png" width="900" alt="晴朗白昼、静谧暗夜、雾面玻璃和流光玻璃"></p>

- **晴朗白昼和静谧暗夜**是纯色主题。
- **雾面玻璃和流光玻璃**把每张卡片画在模糊的桌面之上；玻璃主题开启时，Lens 会把自己排除在截图和屏幕共享之外。
- **自定义配色**会按 WCAG AA 检查对比度。
- **动画开关**跟随 Windows 的动画效果设置。

## 下载

从[发布页](https://github.com/kakuyo1/lens/releases/latest)下载 `Lens-<版本>-setup.exe`，或同一发布里的 `Lens-<版本>-portable.zip`。

- **安装版**按用户安装，Windows 不会要求管理员权限；**便携版**解压后即可运行。
- **Windows 10/11 x64**。你需要一个受支持服务商的 API key。
- Lens 不内置释义词典；它包含一份约 168,000 个英语单词的词表，只用来判断候选词是不是真词。

<details>
<summary>Windows 可能会先拦一下</summary>

Lens 没有代码签名，SmartScreen 会拦截它不认识的程序。点 **“更多信息”**，再点 **“仍要运行”**。这是缺少签名，不是文件损坏。想核对下载是否完整，可以把文件的 SHA-256 和发布里的 `SHA256SUMS` 对应行比一下；这只能证明文件在传输中没有被改动，不能证明发布者是谁。

</details>

## 隐私

Lens 没有自己的账号，也没有自己的服务器。

- **发送的内容**。只有你选中或截取到的文字会发送给你配置的服务商，发送前会在本机遮盖 URL、电子邮件地址和六位及以上的数字串。
- **OCR**。默认在本机运行；在线 OCR 需要你逐次确认，确认卡会写明这次裁剪发往哪个服务，实验中的自动扫描从不使用它。
- **朗读**。喇叭优先用系统音色或已下载的本地模型在本机朗读；兜底到在线音频时，离开本机的只有那个词，录音缓存在 `%APPDATA%\Lens\audio`。
- **本机存储**。API key、OCR 凭据和学习数据保存在 `%APPDATA%\Lens\settings.json`。
- **同步**。开启后，云端副本用你的口令加密，存放在你自己的 GitHub gist 里；密钥始终只在本机。

完整的数据边界见 [SECURITY.md](SECURITY.md)。

## 常见问题

**Lens 是翻译器吗**？它可以翻译，但它是为 “解释” 做的。中文、日文或西班牙文的选区返回英文。

**需要 API key 吗**？需要。用受支持服务商的 key；Lens 会显示每次请求的费用，也可以设置每日上限。

**能离线用吗**？OCR、词表和已下载的本地音色在本机可用；解释、在线 OCR 与在线音频需要你配置的服务。

**什么会离开我的电脑**？只有本地脱敏后的选区文字，加上在线 OCR 里逐次确认的裁剪，以及兜底到在线音频时的那一个词。见[隐私](#隐私)。

**为什么 Windows 会警告**？安装包没有代码签名；怎么运行与核对文件见[下载](#下载)一节的说明。

**有 macOS 或 Linux 版吗**？目前只有 Windows。

## 语言

[English](README.en.md) · [简体中文](README.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [Español](README.es.md)

界面语言和解释语言是两个独立设置，都可以选择英语、中文、繁体中文、西班牙语和日语。

## 参与和许可

Lens 以 [MIT 许可证](LICENSE)开源。欢迎提 issue 和 PR。如果 Lens 帮你多读了一点英文，点一个 star 会让更多读者找到它。

<sub>界面图片由 Lens 当前的设计原型渲染，实际构建遵循同一套布局与令牌。</sub>
