# 0188 扫描读词去重复框读，并记下真实耗时

- **状态**：accepted（2026-11-17）
- **背景**：自动扫描在 Chrome 上出线慢、爬得慢。每个词要走 `step`（Clone+Move+Expand）、`box`（GetBoundingRectangles）、`read`（GetText+GetBoundingRectangles+标点回缩）约 6–10 次跨进程 COM；其中 `extendForward`/`extendBackward` 先 `box()` 探一次框、`read()` 内部又读同一个框，每词重复一次 GetBoundingRectangles。名字集没加载完前 `acquire` 一个词都不读，整页文本约 300 ms 串行阻塞。而 UIA 读正文与对齐的耗时一直没有记录（TODO 耗时未测），无从判断哪一段最贵。
- **决定**：
  - **仪器化**：扫描壳（`PageTrack::run`）每秒记一条步耗时 `avg / max ms`，并在每页第一次画出虚线时记 `scan first line after N ms`（丢页重锚后重新计）。TrackCore 本身不拥有时钟，计时由 stepping 它的壳做，不污染纯策略。max 步耗时即名字读取或页面搜索的尖峰，首线延迟即出线等待。
  - **分项计时**：UIA 适配器内按操作累计 COM 耗时（`present` / `text` / `pageBox` / `wordAt` / `wordBox` / `read` / `step` / `search`），thread-local、无锁，壳每秒 `drainReadTiming()` 取走并记一条 `scan read cost` 行（各操作调用数、总 ms、max ms）。计时全落在具体适配器里，不进纯 std 的 PageReader 缝。它把步耗时的 ~200 ms 周期尖峰坐实到具体操作（`present` 整树检查或 `text` 名字读取），并量化每词 COM 与丢页重读的开销——这是安全修 PDF 丢页前必需的证据。
  - **去重复框读**：两个方向的走词都直接用 `read()->fullBox`，不再单独 `box()`。`read()` 本就返回词框，空框与越界判定改在 `fullBox` 上做，行为不变（同词、同框、同丢块）。
  - **`present()` 改向上爬**：分项计时坐实最大开销是 `present()`——每 500 ms 从窗口根 `FindAll(TreeScope_Descendants)` 整棵文档树，重树上实测 ~180 ms/次、~360 ms/s（约占跟踪线程 36%）；名字读取 `text` 仅 ~100 ms/5 s。改为从页元素沿 ControlViewWalker 向上爬到窗口根（`CompareElements` 命中即 present），`O(深度)` 而非 `O(节点)`，`kMaxLevels=50` 远大于树深。被浏览器重建/丢弃的文档走不到根 → present 假 → 丢页重搜；后台标签窄带仍爬到根 → present 真，由 `fillsTheBand` 拒收（ADR 0163 的窄带防护不动）。
  - **名字门解耦暂缓**：要不闪烁地先画词，需要把名字读取改成异步（大改），且名字过滤依赖整页文本（ADR 0161）——先画词会闪出专名线再收回，违背产品的刷新稳定性。分项计时实测名字读取仅 ~100 ms/5 s（平均 ~20 ms/s），远小于 `present()`，异步化收益小，暂缓更有据。
- **后果**：
  - 确定性测试臂（脚本化 FakePage，每次缝调用记一次操作，∝ 真实 COM 往返）：同一页同 37 词，首线操作数 122 → 86（−29.5%），读完整页 132 → 94（−28.8%）。
  - 真实 ms 降幅取决于框读在每词 COM 中的占比（既有注释约 2 ms/词），由新计时日志在 Chrome 上可观测；操作数臂按缝调用等权计，是调用次数的证明，不是 ms 的直接测量。
  - 每词少一次 GetBoundingRectangles 跨进程往返；走词与画线的行为、滚动丢块判定、首处去重均不变。
  - **`present()` 真机前后（Chrome）**：每次 ~180 ms → ~4 ms（约 45×），每秒合计 ~360 ms → ~8 ms，腾出约 35% 的跟踪线程时间。正确性同机验证：活页正常扫描时 search/lost 保持 0（向上爬不误丢活页），切标签/导航那一下仍触发 lost + 重搜（2 次 search、首线 730 ms 冷恢复）。腾出的时间使读词吞吐上升（read 调用约 230–490/s → 300–650/s），爬升更快。
  - 残留：滚动诱发的锚点读空 → 丢页重读仍在（与 `present()` 正交），是下一刀；名字读取 ~100 ms/5 s 不变。
- **被排除的做法**：
  - 批量文本读取（一次 GetText 读多词）：GetBoundingRectangles 按行返回框，逐词框仍需逐词 range，批量省不掉框读，反增文本切分与框对齐的复杂度。
  - 异步名字读取以解耦名字门：闪烁与线程复杂度大，且无真实 ms 证据证明名字门是首线延迟的主因，先量再定。
  - 加大 `kTrackChunkWords`：不减少每词 COM 总量，只是把同样的往返换个分批，爬升总时长不变。