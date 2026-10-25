# 0114 当前卡由一个卡片会话拥有

- **状态**：accepted
- **背景**：解释卡的状态是 `ExplanationDuty` 里 12 个可变成员：卡的四种身份（新选区、词汇表悬浮复习、宠物复习答案、设置预览）由 `countsDown` / `verdictButtons` / `review` / `preview` 四个布尔拼出，QML 在 `Bubble.qml`、`BubbleCard.qml`、`SurfaceHost.qml`、`ReviewSignWindow.qml` 各自再推导一遍；“当前词卡” 守卫在 duty 里写了四份；一瞥与换义这两条不带请求身份的回答各只有一个键槽，词 A 的请求未回时词 B 发请求，A 的回答会记到 B 的键下，A 的义项轮换停在 fetching。`ExplanationQueue` 已经证明把 “决定” 从 duty 拆出来可以只靠值测试。
- **决定**：新增 `src/app/duties/card_session.{h,cpp}`。它不是一个 QObject，也不碰事件循环，只吃值、吐值：卡的种类与主题、复习卡的持有/收回、义项轮换、母语一瞥、释义链的站立判断都归它。duty 退为 adapter——发请求、存缓存、写 payload。payload 增加 `cardKind`（`selection` / `review` / `reviewAnswer`），`countsDown` 与 `verdictButtons` 改由种类一次结算；QML 不再从布尔组合推导卡的规则，`PendingSelection` 里的三个卡片布尔删除。
  - 义项轮换与一瞥的行按结构化键记：语言 → 词 → 义号，不再拼字符串，也不再 `endsWith` / `section` 拆回来。
  - 在途请求按词记，而不是一个键槽：`peekAnswered` / `senseAnswered` 带回来的词名决定它属于哪个请求。同一个词的第二个一瞥/换义请求在途时等待，不重复花钱——信号只带词名，两个同词请求本来也无法区分。
- **后果**：
  - 词删除时，它的义项、一瞥行与在途请求一起作废；迟到的回答不会被记到别的词下。
  - 模块用的是 Qt 值类型与 `llm::Sense`，不是字面意义的 “无 Qt”：另造一份答案形状会让词答案出现第二个主人（候选 02 要消掉的正相反）。它仍然不需要窗口、事件循环或真实网络，表格用例直接驱动，`card_session_test.cpp` 以 A/B 两词在途的用例钉住旧缺陷。
  - 设置预览仍归视图：`SettingsPopup` 从 `PreviewSample` 升起它，不走 duty，所以它保留 `Bubble` 的 `preview` 标志，不是 `CardKind`。
  - 复习卡（悬浮回放）的 `verdictButtons` 现在结算为 false；旧 payload 给 true 再让 QML 用 `replay` 把它关住。两条路径的像素相同，后者少一个布尔与两处推导。词列表回放不再随悬浮展开判定，与 `UI-SPEC.md` 4.8 一致。
