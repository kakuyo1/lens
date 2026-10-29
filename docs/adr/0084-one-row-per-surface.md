# 0084 一个表面只有一行：名字与哪些表列它

- **状态**：accepted（候选 1 “Name the surface module” 的第一步：先把 “窗口清单” 合成一张表）
- **背景**：同一批窗口被手写了四份清单——玻璃幕布的 sheet 集（`glass_backdrop.cpp` 的 `kSurfaces`）、开发面 `state` 报的名字（`dev_command.h` 的 `kSurfaceNames`）、开发面的 opener 表（`dev_surface.cpp` 的 `kOpeners`）、以及表面测试的 sheet 映射（`tst_phase2e.qml`）。四份清单只靠一个名字字符串互相勾连，而它们已经漂移了：玻璃列 16 个、开发面 12 个、opener 6 个、测试 11 个。最危险的是玻璃那份：QML 用 `image://glass/<name>` 取幕布，名字若不在 `kSurfaces` 里，`GlassBackdrop::surface()` 返回 `nullptr` 且**无声**——没有日志、没有断言，只是那一块玻璃不出现。
- **决定**：
  - 新增 `src/core/domain/surface_table.h`，一行一个表面：`name`（QML 的 `objectName`，也是 `image://glass/<name>` 的键）、`sheet`（玻璃背板是否为它持有一张幕布）、`dev`（开发面的 `state` 是否报它）、`open`（开发面 `surface show` 调用的 `Main.qml` 方法名，由流程唤起的表面为 `nullptr`）、`takesFrom`。表 Qt-free，与 `settings_table.h` 同在 `core/domain`。
  - 三处手写清单改成从表里读：`kSurfaceNames` 变成 `surface::devNames()`；玻璃背板构造时遍历表里 `sheet` 的行建幕布（`sheetCount()` 预留），`kSurfaces` 整个删除；`kOpeners` 由表里带 `open` 的行生成，删掉手写的 6 项。
  - `static_assert(tableIsSound())` 编译期自检：名字唯一且非空、有 opener 的表面必须被 `dev` 报、`takesFrom` 只在有 opener 时置位。
  - QML 侧规则不变、写在 `docs/QML.md`：表面的根窗口带 `objectName`，与行的 `name` 逐字一致。单元测试读不到 QML，所以 `tst_phase2e.qml` 仍是断言 “它点到的那几个表面确实从各自行名的幕布取图” 的那道防线。
- **后果**：
  - 三处漂移源合成一处。玻璃的 “名字错就静默 nullptr” 从源头消除：glass 的 sheet 集现在就是表里 `sheet` 的行，`surface_table_test.cpp` 的 “每个 `sheet` 行都能被 `find()` 解析” 守着它。
  - 新增 `surface_table_test.cpp`：名字唯一且可解析、`devNames()` 恰为 `dev` 行、`reviewSign` 是唯一 “被报但无幕布” 的表面、五张 `*Preview` 是唯一 “有幕布但开发面不报” 的、opener 都是被报表面上的 `show*` 方法、`sheetCount()` 与 `sheet` 行数相等。
  - 新表面现在是 “加一行”：`name` 加四个旗标。要不要幕布、开发面报不报、能不能被 dev 唤起，都是这一行的字段，不再散落四份。
  - 这是候选 1 的第一步，只合了 “窗口清单”。每个表面手写的协议（`shadowMargin`、`leaving`、`openChildRect`、show/hide 排序、互斥、hit rect）仍在候选 1 的后续里——表是它要挂靠的脊梁。
- **被排除的选项**：
  - 只给玻璃加一句 “名字找不到就记日志”：修了症状，没合清单，漂移会继续。
  - 让 QML 从 C++ 读名字表：QML 拿不到 `constexpr` 表，要么代码生成、要么一份 QML 侧副本——又是一处漂移。保持 `objectName` 手写、由测试守。
  - 把 opener 表留在 `dev_surface.cpp`：它是第四份漂移源，删掉它正是这次的目的之一。
