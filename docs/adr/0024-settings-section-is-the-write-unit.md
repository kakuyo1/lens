# 0024 设置文档按分区读写

- **状态**：accepted
- **背景**：`settings.json` 是一份被多个主人共写的 JSON，而 “谁拥有哪个键” 只活在代码里：`KnownStore::save()` 从它自己的强类型成员重新生成 `level`、`explanationLang`、`known`、`cache`，盖进那份要写出的文档；`StatsStore::writeBack()` 同路重建 `history`、`daily`、`dailyBudget`。于是直写这七个键的代码照旧编译、照旧保存，而文件里留下的是它们自己的值 —— 读者看不到自己刚才那一下。同一段时间里，每个主人还各自重写一套嵌套守卫与类型检查（`UpdateDuty` 四处、`ExplanationDuty` 三处、`PetStore` 两处），布尔值有真 `bool` 与 `"true"` 两种编码，整数有真 `int` 与 `"3"` 两种。
- **决定**：新增 `core::SettingsSection`（`src/core/settings_section.h` 与 `.cpp`），把 “文档的一块” 做成读写单位。入口两个：`section()` 是文档自己（顶层标量），`section(name)` 是其中一块具名对象，再深的走 `object(name)`，三者交出同一种句柄。表面六个：`value<T>` / `set<T>` / `has(key)` / `clear(key)`（写 `null`）/ `remove(key)`（把键拿掉）/ `object(name)` —— 缺席、null 与空串是三件事，句柄分开表达。读时两种编码都认，写时保留该键已有的编码，键不存在时按真类型出生。句柄不提供 `json&` 逃生口。每个键的写者由一张 `{键名, 写者}` 表声明，表列在 `test/googletest/unit/settings_section_test.cpp` 里：它没有任何生产读者，放进 `src/` 就是死数据，而用例才是要读它的人。七个被盖回的键在表里标明句柄不可写，并由用例断言。`StorageDuty::documentString` / `documentBool` / `writeDocument` 随之删除，四个 duty 与 `PetStore` 改走同一条 interface。
- **后果**：写时保留旧编码是为降级安全 —— 装着的 1.2.0 用 `documentString(k,"false")=="true"` 读，遇到真 `bool true` 会读成 false，一律改写真类型会让读者降级后看到那六个开关全被关掉。代价是句柄里多一条 “先看这个键现在是什么” 的分支。它护的是**已有的**键：新键按真类型出生，所以降级回来后，升级期间新设的那几个开关仍会读成关。

  读的行为有四处放宽，没有一处收紧。文本形式的布尔与整数现在也读得出来（`PetStore::scale` 曾只认 `is_number_integer()`，`animationsEnabled` 曾只认真 `bool`）；值不成样子时一律取默认值，于是手改成垃圾值的 `selectionCapture` 由 “关” 变成 “开”（它的默认是开，原写法是 `!= "true"` 即关）；`PetStore::position()` 遇到类型不对的 `screen` / `x` / `y` 不再抛 `nlohmann::json::type_error`，按 “没有存过位置” 处理，手改坏的文档因此不会让宠物窗口建不起来；`std::from_chars` 比 `QString::toInt` 严，手改的 `" 4"` 现在读成默认的 3 而不是 4。

  落盘变勤：`set()` 每次落盘，而原来有几处在一次改动里把多次赋值合成一次 `save()`。今天这样写的有四处 —— `ExplanationDuty::setProvider`（2–3 次）、`ExplanationDuty::noteModelsFetched`（每个服务商一次）、`PetStore::setOutfit` 与 `setPosition`（各 3 次）、`StorageDuty` 构造里的一次性迁移（至多 2 次）。

  另外两条。`AppController::settings()` 从此不是 const：读一个文档键要过一个可写视图，而 `storage_` 是这个 facade 唯一按值持有（其余依赖一律按引用借入）的成员 —— 这一条是它逼出来的。把 `StorageDuty` 也改成由 `main()` 持有、facade 借入会更彻底，本次没做。`StatsStore` 是唯一仍直接持文档引用的主人，因为它的三个键正是上面那七个里的三个，本就不走句柄。

  被排除或推迟的三条：每节一个类型化 struct（`update::StoredUpdate` 这个成例只覆盖读，它旁边三个写仍在手抄守卫）；句柄只从 owner 处取（先要回答 “哪个模块拥有哪个标量”，比本次大）；写批量化（每次弹词都跑 `save()`，而它每次重建 `known` 与 `cache` 两棵 json —— 先量再谈）。`levelOptions()` 是设置页的选项表、与存储无关，另行搬走。
