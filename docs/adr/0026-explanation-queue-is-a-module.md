# 0026 解释的排队与过期是一个纯模块

- **状态**：accepted
- **背景**：`ExplanationDuty` 一手拿着两件事——排队（下一笔发什么、发几笔、上一笔答没答完、这个答案还归不归读者）和动作（发什么到线上、存什么、画什么）。21 个成员里有 11 个是排队状态，加上两个世代计数器，散在 5 个信号处理器与 10 个方法里手工维护，而 “答案到达之后怎么办” 整段是构造函数里一条 62 行的 lambda。它一条用例也够不着：要碰到其中任何一个分支，得先造一个真的 `CostDuty`（因此要 `StatsStore` 与价目表）、一个手搓的 `QNetworkReply` / `QNetworkAccessManager` 对，再跑一个事件循环。于是最需要在改动时被钉住的那段，正是没人敢动的那段。
- **决定**：新增 `src/app/explanation_queue.h`（header-only，与 `notice.h` / `tray_state.h` / `capture_refusal.h` / `year_grid.h` 同形，不进 CMake）：`ExplanationQueue` 持有待发队列、在飞的批次、两个世代与被删词的集合，回答 `take()`（下一笔发什么）、`settled()`（这个答案配到哪几条、还该不该上屏）、`refusalSubject()`（失败要不要报），`complete()` 收尾。**它需要的全部外部事实都是入参**：`take(bool maySend, core::CacheContext const& now)`——预算是 “此刻能不能发”，不是队列的属性，所以它不持 `CostDuty&`；语言与多义模式在批次发出的那一刻抄进 `ExplanationBatch::context`，答不答得上屏就靠它和当时比。`ExplanationDuty` 保留 QObject 外壳、缓存读写、统计、LLM 调用和三个界面（气泡 / 通知 / busy）。

  两个边界是刻意的。其一，`take()` 与 `complete()` 之间**什么也不吐**——原来是 `inFlight_` 与 `handlingResponse_` 两个布尔在挡，后者只在结算那一小段撑开；现在挡的是整段 take 到 complete，窗口比旧的后半程略宽（结算途中来的请求由 “当场发” 变成 “和这一批一起等”），结果一样，而状态少了一个。其二，**读缓存的准入留在 duty**（命中就直接出气泡、不入队），所以队列不持 `KnownStore&`，用例不必建临时目录；`pending_` 也留在 duty，因为队列的任何一个判断都不读它——队列只知道 “读者走开了”（`selectionChanged()`）。
- **后果**：**行为不变**，除两处证明等价后的删除。`scanAnchor_` 只写不读（唯一写入点是 `explanation_duty.cpp` 发批次之前那一行），删。结果归属那处 lambda 里的 `requestedFromScan_` 是死项：`?:` 右结合，`kind == "word"` 时两个分支同归 `title ≡ lemma`，`kind != "word"` 时无论它是真是假都走 `text ≡ 批次首项的 text`——整式等价于 “是词就按词形配，否则按文本配”，`settled()` 就是这么写的。这是证明后删的，不是顺手改的。

  **`take()` 把预算挡在最前面，这条是搬移前的行为被写成了规则。**原来的 `startNext()` 第一句就是 `if (inFlight_ || not cost_.canRequest()) return;`，所以预算在等待期间花完不会丢掉那条排队的请求，它和词表的活儿一起等下一次——这一半在队列里，有用例。另一半在 duty：一个**新到**的、此刻就出不了门的请求仍旧当场丢掉，不排队（facade 已经说过为什么），这是 `explain()` 搬移前就有的行为，这次没动，仍是端到端用例那条线在盖。

  **已知边界，这次不动**：`invalidateWord()` 顺带推进世代，于是删一个词会让**整批**答案都不再上屏，而不只是被删的那个词。这是现状不是这次引入的，记在这里是因为新模块把这层含义明写成了 “读者已经走开”。相邻的 `PendingSelection::kind` 同样照旧顺延（ADR-0025 记的那条）：这次只把队列内部的分支收敛到一个 `isWordKind()`，跨 QML 的仍是字符串 token。

  用例在 `app_seams_test.cpp` 的 `ExplanationQueue` 套件里，10 条，离线、无网络、无文档、无时钟——从前这些分支一条也碰不到。`phase2_explanation_test.cpp` 的 15 条端到端用例一行未动，它们连同 qtest 的三个目标一起把这次搬移钉在原来的行为上。
