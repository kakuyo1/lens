# 0064 词汇的导入、导出与日期标签是纯模块

- **状态**：accepted
- **背景**：`AppController` 里同一份 `"all"` / `"known"` / `"new"` 判断写了三次（文本导出、JSON 导出、保存前的校验），导入的解析、去重与落库混在一个方法里，测试树下没有一处引用 `importWords`。词条行的今天、昨天与更早直接读 `QDate::currentDate()`，导入时间戳直接读 `QDateTime::currentDateTime()`，所以这些日期判断无法在用例里钉住。
- **决定**：新增头文件 `src/app/words_view.h`，按 `model_catalog.h` 的写法只放纯函数：`parseScope()` 给出导出范围，`isJsonPath()` 读扩展名，`dayLabel(moment, today)` 给出词条行的日桶，`wordRows(today, history)` 给出整张词表的行，`importLemmas(data, json)` 读出文件里的词，`importRows(incoming, existing, minute)` 去掉已有词与文件内重复并盖上导入时间。`AppController` 的 QML invokable 保留在门面（ADR 0028），只在边界读时钟、读文件、写 store；导出函数共用一次范围解析，不再各自 switch。
- **后果**：词表的行、日标签、导入规则与范围词汇有了 `words_view_test.cpp` 的离线用例；`words()` 的每词一行、首次出现者与总弹出数行为不变，导入仍然按文件顺序落库、校验未知范围、拒绝不是数组的 JSON。`yearDays()` 本来就走 `yearGrid()`，不重复。写入仍然通过 `StatsStore`，模块本身不碰文件、网络或时钟。
