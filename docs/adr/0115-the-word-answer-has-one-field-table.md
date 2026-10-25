# 0115 词答案的字段表只有一个主人

- **状态**：accepted
- **背景**：一个词答案的字段要在四种形状里出现：线上 JSON（`data/llm/response.word.schema.json`）、`llm::Explanation`（全量解析与流式预览各读一遍）、`core::WordCache`（`known_store` 的读与写）、以及气泡的 `QVariantMap`（`showBubble`、`chainEntry`、`emitSenseSwapped` 三个构造器）。每加一个字段——例如 `pos`——要在这些地方各抄一遍，约 14 个文件、20 处；流式预览因此只读了 12 个字段里的 8 个，`hasAnythingToDraw` 的字段列表也停在更早的一版。`docs/adr/0114` 已经定下词答案不另造形状，所以不能靠新增一个 `WordAnswer` 记录来收敛。
- **决定**：新增 `src/util/word_fields.h`，一张 `constexpr` 字段表。每行有身份（`Scope` / `Slot`）、线上与存储共用的键、值的种类、以及各形状走表所需的小事实：存储是否必需、是否非空、例句三态、预览能否画、类型错误是丢条目还是忽略、卡片的显示开关。`llm_pure` 的全量与流式解析共用 `readAnswerFields` / `readSenseFields` 一个读者；`known_store` 的 `readCache` / `writeCache` 按表走；新增 `src/app/duties/word_answer.{h,cpp}` 持有线上记录、存储条目与卡片载荷之间的转换，duty 退为 adapter。
  - 表放在 `util` 而不是 `app`：`core` 与 `llm` 是平级，`docs/adr/0085` 不允许 `lens_llm` 链接 `lens_core`，两边都要读的字段词汇只能落在两者之下。它是数据，不是领域判定；schema 的必填列表、缓存按语言要求的 `translation`、卡片选择哪种语言，仍留在各自的模块里。
  - 应用层的 `AnswerRef` / `SenseRef` 只把 `Slot` 绑到两个记录的成员上，不重述键与规则；字段一旦只在表里而没有绑定，全字段往返用例会失败。
- **后果**：
  - 新增字段的落点收成 schema、表的一行、两个记录各一个成员、应用层绑定表的一行与 QML 块；解析、存储、卡片三套字段列表不再各写一份。记录的成员声明不能由表生成而不把两个公开结构变成宏展开，所以保留显式成员，绑定由全字段往返用例兜住；`WordAnswerFields.NamesEveryFieldTheSchemaDoes` 在 schema 与表不一致时直接变红。
  - 流式预览与全量解析不再各写一份字段列表；预览开始读 `pos`、`signpost`、`collocations`，但图片建议仍留在 `preview = false`：一张画到一半的答案不该先买一次图片搜索。
  - 存储文件的字节形状不变（键仍按字典序写出，空值省略，例句三态与图片对规则照旧）；`docs/adr/0019` 的缓存键与 `docs/adr/0033` 的例句三态不变。
  - 表里的必填、非空、三态与类型错误策略是对记录形状的描述，不是缓存策略；schema 的必填列表、缓存按语言要求的 `translation`、卡片选择哪种语言，仍留在各自的模块里。
  - 被排除的方案：把表放进 `core` 或 `llm` 任一侧，会让另一侧包含更高层的头文件；把字段绑成成员指针的表放进 `app` 并由 `llm` 引用，需要 `lens_llm` 包含 `app` 或把解析搬出 `lens_llm`，两者都把 0085 的边界拆掉；再定义一个 `WordAnswer` 记录，则与 0114 的 “不另造答案形状” 相反。
