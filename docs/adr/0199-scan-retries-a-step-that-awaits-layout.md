# 0199 走词遇到尚未布局的页时原地等待，不当作文档结束

- **状态**：accepted
- **背景**：SumatraPDF 只为已布局的页给词框；翻到下一页的边界时，`Move(Word, +1)` 已经成功，但目标词的 `GetBoundingRectangles` 还是空的。读取器把空框当 “走不通” 返回 null，TrackCore 于是把 forward/backward 标成结束——此后继续下滑、下一页已经布局，轨道也不再读；缩放（重排重读）或重新聚焦（重建轨道）才会恢复。真机在 Sumatra 上稳定复现。
- **决定**：`capture::Page::step` 增加一个 `awaiting` 出参。走词成功但目标没有框（或点查询/展开失败）时，读数器返回 null 并把 `awaiting` 置真；TrackCore 遇到 null+awaiting 只结束这一步的走词，不把方向标成结束，下一步继续从原游标重试。null 且未置位仍是真正的文档两端；Scintilla 与 Chrome 从不置位，行为不变。
- **后果**：跨页滚动时边界词每步重试一次（一次 Move + 一次矩形读），页布局后立即续读；不产生重复跨度，也不丢页。旧的 “空框即结束” 在真正的文档末尾仍由 `Move` 返回 0 判定。新增的 `TrackCore.APageBoundaryThatAwaitsLayoutIsRetriedNotFinished` 覆盖这一路径。
