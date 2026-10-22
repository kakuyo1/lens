# 0085 释义缓存只有一个模块：上下文、三态与键

- **状态**：accepted（把 ADR-0019 的键形状与 ADR-0033 的三态收进一个模块，两条本身不变）
- **背景**：释义缓存没有模块拥有它自己。`ExplanationDuty` 在 9 处手拼 `core::CacheContext{explanationLang(), section().value<bool>("multiSense")}`（cpp:67、101、199、245、265、300、351、366、432），`ReviewDuty::askable()` 还有第 10 处。“多义只在单词通道算数” 这条规则写了 5 遍：duty 的 80、352、367 行，以及队列的 `stillTheReaders()` 与 `open()`。“能不能直接服务” 由 4 个调用点各自判断（`servable()` 只被其中 3 个用，`reviewWord` 故意不看）。`PendingSelection.kind` 是 QString：`classifyText` 写出，duty 里 8 处字面量比较，再转成 `llm::Channel`。键是 `lang \x1f mode \x1f lemma` 的打包串，`removeLemma` 用 `rfind`、`save()` 用两次 `find` 拆回来。一个缓存 bug 只能靠 duty 图、临时 store 和磁盘上的 `Pricing` 才看得见：`phase2_explanation_test.cpp` 把同一段九行装配写了十遍。
- **决定**：
  - 新增 `src/core/document/explanation_cache.{h,cpp}`，Qt-free。`readerContext()` 给出未掩码的设置，是队列判断设置有没有动过的 now；`contextFor(kind)` 与 `masked(captured, kind)` 是掩码规则的唯一出处；`look(lemma, context)` 返回 `Missing / Rebuy / Hit` 与条目，三态按 ADR-0033 解释（`Rebuy` 仍带条目，指针悬停的复习气泡照旧展示已存内容）；`store()` 转交 `KnownStore::cachePut`。
  - `PendingSelection.kind` 改为 `core::SelectionKind`。QML 线格式不变：气泡的 `payload.type` 与动作条的 `payload.kind` 仍由 `kindName()` 写出一次；`isWordKind()` 随枚举搬家。扫描候选本就全是单词，`scanCandidatesReady` 的 `"kind"` 字段一并删除。
  - `KnownStore` 的内存缓存改用 `std::map<CacheKey, WordCache>`，`CacheKey{language, lemma, multipleSenses}`。打包串、`rfind`、两次 `find` 全部删除；JSON 文件格式不变。
  - `Bubble.qml` 删掉 `payload.senses.slice(0, 3)`：载荷里的义项在解析、入库、绘制三层都已封顶，QML 那一刀永远切不到东西。
- **后果**：
  - duty 的上下文构造点 9 → 0，`servable()` 删除，掩码规则 5 处 → 1 处，QString 种类比较 8 处 → 0 处（`kindName()` 是唯一一次转字符串）。
  - 新增 `explanation_cache_test.cpp` 8 个用例，只用一个临时 store，没有 client、cost、`Pricing` 和事件循环；`phase2_explanation_test.cpp` 的十段装配收成一个 `Rig`，断言未改。
  - 缓存遍历从无序变有序；`save()` 写进 `nlohmann::object` 后本就按字典序，文件字节与原先一致。
  - 代价：QML 载荷里的种类仍是字符串，任何新载荷都要经过 `kindName()`；换来的是 C++ 里的拼写错误从运行期变成编译期。
- **被排除的选项**：
  - 把 `llm::Channel` 与 `SelectionKind` 合成一个：`lens_llm` 不链接 `lens_core`，合并会把这个边界拆掉；`send()` 里一次枚举到枚举的转换就是保留两条通道的代价。
  - 把缓存表搬出 `KnownStore`：持久化、标记与同步共用一份文档，模块只管规则，store 只管存储。
  - 让 `ExplanationCache` 持有 `StorageDuty&`：那会把 core 拉回 Qt 与 duty，测试又得搭客户端和事件循环。
  - 只把缓存规则收拢、保留 QString 的 kind：8 处字面量比较和 5 处掩码还在，而这正是本次要收的东西。
