# 0060 一次解释请求用一个记录表达

- **状态**：accepted
- **背景**：`LlmClient` 原先是个有状态的会话：每次请求先在 `ExplanationDuty::send()` 里调五个 setter（通道、语言、预设、词源、例句），再调 `explainWords()`，次序规则留在调用方。`AppController` 还在构造时和 `setExplanationLang()` 里各写一次客户端的语言，但 duty 在每次发送前都会覆盖它，这两次写入到不了任何请求。响应侧的义项上限由 `preset == "multiple"` 推断：预设字符串同时承担选提示词和选结果截断两个职责。ADR 0019 与 ADR 0033 要求词源与例句是随请求走的值，而不是回复时再读设置。
- **决定**：新增 `ExplainRequest`，携带 `words`、`channel`、`preset`、`language`、`multipleSenses`、`etymology`、`example`；`explainWords(ExplainRequest)` 一次收齐。预设名由 `llm_pure` 的 `presetFor()` 从记录里选：`multipleSenses` 为真时用 `"multiple"`，否则用记录自带的预设。`parseExplanations()` 的义项上限直接接收 `multipleSenses`，不再看预设字符串。`LlmClient` 的五个 setter 与它们的状态成员删除，`AppController` 的两处客户端语言写入删除（`KnownStore` 的写入保留）。
- **后果**：请求之间不再共享任何提示词状态，调用的全部决定都在一个实参里；预设选择紧挨着提示词模板。行为上唯一的改变是义项截断改由 `multipleSenses` 决定：生产路径里 `"multiple"` 一直只在这个标志为真时出现，所以既有调用的线上行为不变；一个标志为假却名为 `"multiple"` 的预设现在只保留一个义项，由 `llm_mock_transport_test.cpp` 的一条用例钉住。`kMaxSenses` 原先没有任何断言，现在由纯函数用例与经过 `LlmClient` 的 `"multiple"` 路径各钉一次。冒烟与 phase2 用例改为构造默认记录，语义与原先的默认 setter 状态一致。
