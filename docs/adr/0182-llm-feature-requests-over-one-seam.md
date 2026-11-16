# 0182 模型的七种功能请求走同一道接缝

- **状态**：accepted（2026-11-16）
- **背景**：`LlmClient` 每加一个功能就加一对方法与信号（`explainWords`、`peekWord`、
  `nextSenseWord`、`breakdownSentence`、`correctSpelling`、`correctGrammar`、`requestProfile`、
  `requestStory`）。七条发送路径各自重复 URL 拼接、请求头、超时、POST 与 “状态码到应答” 的
  映射；超时散成五个常量，测试给不了值；取消不存在，过期只在上游按卡片丢弃，请求照旧跑完。
- **决定**：
  - 新增 `Handle request(Request)` 一道入口：`Request` 是七种功能请求的 `std::variant`，
  返回值 `Handle` 带 id，`cancel(Handle)` 中止在飞的应答且不再报告。这是 `0170` 的 id 范式
  在 LLM 侧的延伸。
  - 应答汇成一道 `answered(Handle, Answer)`（`Answer` 是七种应答 struct 的 variant，各自的
    `Usage` 与 `failure` 留在结构里）；流式预览保留 `partial(Handle, Explanation)`；原
    `failed()` 信号并入 `ExplainAnswer.failure`。
  - 七个内部构造器（`askExplain` … `askStory`）承担校验、请求体与状态码到应答的映射；一个
    `post()` 承担 URL、请求头、超时、POST、在飞登记、4 MiB 应答上限，以及取消后不再交应答。
  - 超时改为构造注入的 `Timeouts` 策略（explain / peek / sense / probe / modelList）；
    breakdown、correction、profile、story 沿用 explain 的时长。
  - 探针与模型列表不走 `request()`：它们不是卡片上的功能请求，形状照 `0110`、`0137` 保留，
    只有超时改从同一策略读。
  - 消费端各连一次 `answered` 并按键分派；气泡清掉时 `ExplanationDuty` 取消在飞的
    peek / sense / breakdown / correction——这四个答案的唯一去处就是那张卡。explain 归
    exchange 管，不取消。
  - 周报故事的载荷 `StoryAnswer` 改名 `Story`，`StoryAnswer` 留给应答信封，两种类型不
    同名。
- **后果**：
  - 客户端允许同时多个在飞请求（重构前也允许：peek 与 explain 可以并行）。id 让每个应答
    带身份；取消只对登记在案的在飞请求有效，已答的 handle 是 no-op。
  - 取消改变了一个可观察行为：卡片消失时，在飞的 peek / breakdown / correction 不再到达，
    此前是到达后被上游按卡片丢弃。记账点不变——`Usage` 已在应答里带，预算与统计仍在应答
    处入账；被取消的请求没有应答，也就没有入账，与 “买了没回来” 一致。
  - 新增 4 MiB 应答上限：超过即 abort，按传输失败收尾。它挡的是迟迟不结束的病态应答，
    正常解释（20 个词）远在阈值之下。
  - 被排除：一功能一信号保留（等于只折叠发送侧，`failed` 与批次信号仍要在三个消费端各
    分派一次）；把探针、模型列表塞进同一个 `Request` variant（没有卡片、没有 `Usage`、
    失败语义不同）。
  - 测试改为跨接缝驱动；新增两条离线用例：注入的超时确实落到 `QNetworkRequest`，取消后
    中止应答且不上报。
