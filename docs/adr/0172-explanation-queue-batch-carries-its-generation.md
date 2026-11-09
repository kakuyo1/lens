# 0172 解释队列的在飞批次随身携带代际

- **状态**：accepted
- **背景**：ExplanationQueue 的协议写在类注释里：`take()` 一批、发送、应答到达后读 `settled()` 或 `refusalSubject()`，再 `complete()`。但批次在飞由 `sent_` 一个 optional 表示，判定它是否仍是读者等待的答案还要另读 `requestedGeneration_`；两个成员分开维护，全靠 `open()` 记得同时盖章。Stage 2 的调查确认现有语义正确，这一项只是把协议本身显式化，不改行为。
- **决定**：
  - 在飞批次收成 `InFlight{batch, generation}`：代际与批次同一次赋值、同时清除，判定陈旧时没有第二个成员可读。`requestedGeneration_` 删除。
  - 新增 `Stage{Empty, Waiting, InFlight}` 与 `stage()` 投影：Empty 表示没有等待也没有在飞，Waiting 表示有东西可发但没在飞，InFlight 表示一批在外、其余排在它后面。`inFlight()` 改为读该投影。
  - 新增 `explanation_queue_test.cpp` 三条直接用例（一次一批与阶段迁移、在飞期间到达的读者请求排在应答之后、没有在飞批次时 `complete()` 不改动任何东西）；既有 11 条队列用例与 13 条 exchange 用例不动，为行为保真门禁。
- **后果**：协议多了一个可断言的出口，后续要问队列此刻在哪一步，不必再从两个 optional 组合推断。语义、字符串与调用方接口不变。
