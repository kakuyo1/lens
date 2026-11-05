# 0154 自动扫描读 Chrome 的正文，用 Tesseract 的词框定位，按 Alt 显示路标

- **状态**：accepted
- **背景**：整窗 OCR 把标签栏、地址栏和工具栏的字也读进来，候选里满是界面词（youtube、lens、eng），
  且 OCR 的行框给不出词的位置。RapidOCR 只给行框，不给词框；`docs/metrics/ocr-baseline-2026-10-28.md`
  明确不能把行框均分成词框。
- **决定**：
  - 文字来自 UI Automation 文档控件的 TextPattern：`DocumentRange.GetText` 约 120 ms 读完一本书。
    不用 `GetVisibleRanges`，它在 Chrome 上每次约 12 s。
  - 位置来自 Tesseract 的 TSV 词框。OCR 的一帧逐词与正文对齐：三词连续段在正文里只出现一次才定位
    （`capture::PageIndex`）。Chrome 的 PDF 上实测 80% 的 OCR 词能定位，正文区域 85%，定位点连续落在
    UIA 文本的 200 个 token 之内，说明屏上这一页在整本书里是一段连续区间。
  - 扫描只在 `chrome.exe` 前台生效。画面指纹在一跳（约一秒）内不再变化才读，同一指纹不重读，两次读取至少隔 3 秒。
    Tesseract 单次约 1 秒 CPU，不能每秒跑。
  - 候选只画线、只显示路标、按 Alt 加悬浮弹出气泡。扫描批次不再弹卡；LLM 请求只在按住 Alt 时发出，每词一次。
  - 铅笔层是点击穿透的 QML 窗口，不参与截图。穿透用 `WS_EX_TRANSPARENT`，与桌宠的穿透相同，不用 Qt 的 `Qt.WindowTransparentForInput`：后者在本机把整屏铅笔层画成黑色。单独轻点 Alt 时注入 0xE8，使 Chrome 的菜单栏不被唤起。
- **后果**：
  - 只覆盖 Chrome。Edge、Acrobat 和其他阅读器没有验证；图片里的字读不到。
  - Chrome 为 PDF 生成的不可见文字（“This PDF is not accessible…”）不在 OCR 里，不会被定位，也就不会成为候选。
  - 候选质量尚不够：人名、同批重复和口语词没有过滤，记在 `TODO.md`。
  - 注入的 0xE8 是系统级输入事件，只在自动扫描开启、Chrome 前台、单独轻点 Alt 时发生。
  - 气泡出现在页面上会被抓屏读到，改变指纹，虚线因此收起；记在 `TODO.md`。
  - 稳定判定取一跳（约一秒）：停止滚动到虚线出现，实测平均约 2.3 秒（原两跳约 3.3 秒）。代价是短停顿会被当作稳定页面读取；
    两轮共 33 次读取中，读取途中页面又变的过期放置只出现一次，已改为丢弃。
  - 被排除的方案：把 RapidOCR 的行框均分成词框（违反基线文档）；`GetVisibleRanges`（12 s）；
    保留整窗 OCR 直接出候选（界面词的问题无法靠过滤根除）。
