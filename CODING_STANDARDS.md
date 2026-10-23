# 代码标准

> 只放**审查阶段能判的判断题**：能被机械检查的（格式、排版）一律交给 `.githooks/pre-commit`，这里不重复；
> 所以本文宁缺毋滥，短是正常状态。逐文件的 QML 事实（模块与目录、阴影、定位、线程）见 `docs/QML.md`，
> 注释的写法见 `AGENTS.md` 的 Code Style（不在这里复制一份）。

## C++

- **注释用英文、说 why 不说 what、密度与邻座一致**。写法的正文在 `AGENTS.md`。
- **Qt 的 qDebug / qWarning 由 `src/util/qt_log.h` 折进同一个 logger**，源位置指向 Qt 调用点；日志的
  规范见 `AGENTS.md` 的 Code Style。
- **一个设计若与既有注释冲突，改注释与改代码放进同一个提交**。留在树里的旧注释比没有注释更坏——下一个
  读者会照着它做。
- **同一段工具逻辑写到第二处时，先找现成的库，再决定写在哪**。标准库、Qt 与成熟的三方库能答的
  就不再手写，仓库已带的（nlohmann/json、spdlog）优先；为几行工具新增一个依赖要先算安装包与审计
  成本，并按 `docs/adr/` 记一笔。没有合适的库时自己写：Qt 无关的进 `src/util` 并补一条 gtest，
  带 Qt 类型的进最低那个已依赖 Qt 的模块。领域判定不因为 “收敛” 搬进 util——HTTP 主机白名单、
  宠物名清洗、选区形状分类各有各的错法，留在自己的模块。收敛以删掉原实现为准，不留转发的薄壳。

## 分层

- **目录就是层，依赖只能向下**。`src/util` 不依赖任何模块；`src/core` 不依赖 Qt，只向下依赖 `util`；`src/app/duties` 可以依赖 `platform` 与下层能力，但不碰 `glass`、`pet`、QML 与门面；`glass` 与 `pet` 可以依赖 `platform`，但两者互不依赖；`platform` 不依赖 `glass` 与 `pet`；只有 `app` 根的门面、组合根与 `app/dev` 可以碰全部。`src/app` 根下的设置与投影头文件是跨组共享词汇，不在这条限制里。
- **越界由 `scripts/quality/layer-check.sh` 检查**，pre-commit 与 CI 都跑它。目录移动时把旧 include 留给同事的编译器，不如让这条检查先说话。
- **为什么不拆 target**：`qt_add_qml_module` 只注册 backing target 自己的类型，duties、platform、glass 里的 QML 类型要拆出去就得补 `QML_FOREIGN` 垫片；在需要编译器强制之前，目录与检查足够。理由见 `docs/adr/0077`。

## QML

- **阴影来自一份只有形状的 `visible: false` 副本**，不要把内容包进 `MultiEffect`：它把源渲进离屏贴图，
  125% 下被重采样，同一个字形会从锐利边变成糊团。
- **表面的定位按卡片算，不按窗口算**：窗口四周有 26 px 透明阴影边距，每次都要把 `shadowMargin` 减掉。
- **显示一张表面是两步**：`visible = true` 之后还要 `raise()`，否则可能被任务栏压住。
- **跨线程回调只能排队投递**到拥有表面的线程（`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`）。
- **纯函数进 `src/app/qml/util/` 的 JS 库，不留在页面或控件里**：`.pragma library` 只接受参数、不碰 QML
  状态；同一段逻辑第二次出现时收进去，原实现删掉。带翻译的句子和领域映射留在自己的模块。

前四条背后的实测数字与既知边界见 `docs/QML.md` §2 / §3 / §4 / §5。
