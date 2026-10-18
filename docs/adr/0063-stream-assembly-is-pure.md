# 0063 流式应答的组装是纯模块

- **状态**：accepted
- **背景**：流式应答的规则原先住在两个 Qt 回复回调里：前五字节才敢判定是不是 SSE、错误状态不产出预览、同一份预览不重复发、整包回退，以及把流重新拼成一个合成信封再交给 `parseExplanations()`。那个信封在线上从不存在，只为了让整包校验能复用；去重与嗅探也因此只能靠真实或桩 `QNetworkReply` 才跑得到，现有两个流式用例只查顺序与内容，不查去重。
- **决定**：新增 `StreamAssembler`，放在 `llm_pure`：`feed(bytes, httpOk)` 收字节、按需嗅探、消费 SSE 事件，只在预览真的画得出新字段时返回它；`finish(tail)` 处理最后一段并给出校验后的结果；`usage()` 给出这次应答的计数。合成信封删除，流式结果直接走与整包共用的 `parseExplanationContent()`，`finishReasonError()` 也由两条路径共用。`LlmClient` 的回调只剩读 socket、判状态、`feed`/`finish` 与发信号。
- **后果**：嗅探、去重、整包回退与结束原因成为 `StreamAssembly` 的离线用例；`parseExplanations()` 的公开契约不变，现有流式与整包用例原样通过。`httpOk` 只压制预览，HTTP 状态到读者消息的映射仍留在客户端；这是回复接线的一部分，不是组装规则。被排除的方案是让组装器自己发信号，那会把 Qt 带回纯层。
