# 0197 自动扫描接入 SumatraPDF 的 UI Automation 文本

- **状态**：accepted
- **背景**：自动扫描已接 Chrome 与 Notepad++，PDF 阅读器一直只有一行 “待上线”。SumatraPDF 3.7 起在正式构建里暴露 UI Automation：文档控件实现 TextPattern，实测（3.7.0.24556）支持 `RangeFromPoint`（约 1–3 ms）、按词/行/页展开、逐行词框（屏幕坐标），沿父链可从文档回到 Canvas 与窗口。3.6.1 及更早的正式构建在 `WM_GETOBJECT` 里直接退回 `DefWindowProc`，完全没有 UIA；3.7 预发布还有两处与 Chrome 不同：文档范围是整本书，`Move` 从已展开的词范围出发不前进（上游 #321）。
- **决定**：
  - 只接 SumatraPDF：前台进程名以 `sumatrapdf` 开头、`.exe` 结尾；设置页一行独立开关 `autoScanSumatra`，缺省开。读取仍走现有 UIA 读取器，按窗口类 `SUMATRA_PDF_FRAME` / 子窗口 `SUMATRA_PDF_CANVAS` 识别后启用两条替代机械。
  - 名字与语言门只读屏上那一页：`RangeFromPoint`（文档中心）→ `ExpandToEnclosingUnit(Page)` → `GetText`；整本书不读。
  - 走词不用 `Move` 的展开结果：`Move(±1)` 后取范围矩形（前进取末个、后退取首个）的中心，再用 `RangeFromPoint` + `Expand(Word)` 取词柄，绕开 #321。该写法在后续修好 `Move` 的版本上同样成立。
  - Sumatra 的词矩形是整个行距，底部远低于字形；上报词框时收掉底部约六分之一，虚线才贴回字下（`lineBoxOf`，随实际观感微调）。
  - 遮挡判定跳过：Sumatra 的点命中返回页元素而非词的元素，现有探测会把每个词判成被盖住；它也没有更便宜的浮层信号，按 ADR 0193 的 “无命中机械” 约定一律可见。页面组织不变：文档控件即页，box 为画布。
- **后果**：
  - 只有 3.7 及以上可用。3.6 正式版没有 UIA，开关打开也不会有线；设置页写明 3.7+，3.7 正式发布前只能用预发布构建。
  - 名字与语言判定按可见页而非整本；同窗对话框盖住页面时虚线仍会画（没有遮挡判定），Chrome 的遮挡判定不变。
  - 其它 PDF 阅读器（Acrobat、Edge 内置等）未验证，仍不扫描。
  - 被排除的方案：自己解析 PDF 并反推缩放与滚动（拿不到阅读器的视口状态）；整窗 OCR 出词框（违背扫描不经 OCR 的边界，慢且不稳）；剪贴板复制选区（没有词框）。
