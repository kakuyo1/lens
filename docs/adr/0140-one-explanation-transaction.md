# 0140 一次解释事务只由一个 module 拥有

- **状态**：accepted（2026-10-30）
- **背景**：`look → 预算 → 请求 → 存 → 显示` 这段事务在四个入口各写了一遍：选区、扫描候选、词汇表悬浮和宠物复习。当前卡与在途请求同时由 `ExplanationQueue::sent_`、`CardSession::subject_` 和 duty 的 `bubble_` / `pending_` 记着；`ExplanationCache::look` 在 duty 里有五处；`emitSenseSwapped` 还由 duty 手改视图载荷。ADR 0114 已把卡的规则交出去，但事务本身没有主人。
- **决定**：新增 `src/app/duties/explanation_exchange.{h,cpp}`，一个无 QObject、无时钟、无网络的事务 module：它拥有缓存查询、预算门、请求形状（kind → channel）、批次结算与存储、卡与在途请求。四个入口收成一个 `ask(selection, origin)`，由 origin 决定命中的价值与未命中的动作：选区可买，扫描只记并排队，悬浮只显示已存，宠物复习只显示完整答案。`ExplanationDuty` 退为 adapter：读设置、发请求、写存储、把 `CardView` 投影成 payload。`PendingSelection` 搬进 `pending_selection.h`，队列与卡片会话不再包含 `capture_duty.h`。
- **后果**：缓存与预算检查只剩一处，四份事务一份；卡的状态只有一个主人，duty 保留的 `bubble_` 只是投影。`Phase2*` 的 24 例、`ExplanationExchangeTest` 的 12 例与卡片、字答案、缓存、捕获的用例全部通过。duty 仍负责 reader-facing 字符串、显示开关与设置读取；预览卡仍归视图，不进 exchange。被排除的方案：把四段留在 duty 只加端到端用例，仍要 mock 网络与事件循环；再定义一套答案形状或卡状态，会与 0114 / 0115 的红线相反。
- **ADR 关系**：0114 的卡会话与 0115 的字段表不变，成为 exchange 的内部部件；0085 的层边界不变；`PendingSelection` 仍是捕获侧交给解释侧的唯一值。
