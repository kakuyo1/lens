# 交接：玻璃主题的渲染问题与阶段 2

日期：2026-10-13。分支：`design/glass-prototypes`。给下一个 agent。
本文件放在仓库根目录，与本次提交一并入库。

## 0. 先做这些

1. `git status`、`git log -3`，确认分支和提交状态。
2. 读 §4（渲染问题）和 §6（待决定）。未经用户批准，不改渲染方案，不开可见窗口，不做推送以外的远程操作。
3. 跑一遍 `tst_phase2e`，确认玻璃相关测试通过，再动代码。

## 1. 先读这些

| 路径 | 用途 |
|---|---|
| `docs/adr/0034-glass-backdrop-from-screen-capture.md` | 抓屏背景与 MultiEffect、着色器的取舍（§4 的方案会改写它） |
| `docs/adr/0035-glass-settings-capture-and-text-tone.md` | 参数存根键、抓屏时机、文字色、qsb 版本 |
| `docs/adr/0036-glass-only-in-settings-window.md` | 玻璃只作用于设置窗口；双色表；阶段 2 的上限 |
| `UI.md` | 顶部的「第一阶段」段、3.1c、3.1d、4.4，以及「可拖动」条目 |
| `src/app/qml/components/TokenTable.qml`、`Tokens.qml`、`SettingsTokens.qml` | 双色表的实现与入口 |
| `src/app/glass_backdrop.cpp`、`glass_math.cpp` | 抓屏与图像提供器，位移图 |
| `src/app/qml/components/GlassSurface.qml` | 渲染管线，本次问题的核心 |
| `src/app/qml/SettingsPopup.qml` | `captureBackdrop`、`pictureTheme`/`pictureRect`、窗口拖动 |
| `test/qtest/surfaces/tst_phase2e.qml` | 玻璃相关断言 |
| `$TEMP` 下的旧交接：`handoff-glass-phase1-fallback-2026-10-12.md`、`handoff-glass-cpp-qml-2026-10-12.md`、`handoff-glass-theme-2026-10-12.md` | 阶段 1 的来历。第一份的大部分已完成。`glass-recipe.md` 已过时，不要复用。 |

## 2. 当前状态

- 本次提交前，分支领先 `origin` 5 个提交（最新为 `459f90f`）。本次提交内容：阶段 1 的玻璃作用域（双色表）、滑条与拖动修复、本交接文件。提交后推送，不开 PR（用户指示）。
- 测试（诊断代码移除后）：
  - QML lint：0 警告。
  - `lens_qtest_components`：84 通过，0 失败，3 跳过。
  - `lens_qtest_surfaces`：167 通过，0 失败，15 跳过。
  - `tst_phase2e`：17 通过。
  - `lens_gtest_unit`：本轮未跑，上次为 290/290。
- 可能仍在运行的 Lens：旧的诊断构建（上次 PID 25004）。源码已清理，要用新代码需重新构建 release 并重启。关闭用户正在用的实例前先问。
- 应用日志：仓库根的 `logs/lens.log`（已忽略入库）。
- 用户的真实设置文件（`%APPDATA%\Lens\settings.json`，含 API key）：不读、不复制、不改。测试只使用临时副本。

## 3. 本轮做了什么

### 3.1 阶段 1：玻璃只作用于设置窗口（ADR 0036）

- 色表拆成两个单例，共用 `TokenTable.qml`：
  - `Tokens`：所有非设置表面使用，玻璃关闭，玻璃主题按晴朗白昼渲染。
  - `SettingsTokens`：设置窗口使用，玻璃开启。
- 设置页与设置专属组件改读 `SettingsTokens`（约 180 处）。共用组件 `ShadowCard`、`Segment`、`Switch`、`SwitchRow` 新增 `tokens` 属性，默认 `Tokens`，设置里显式传入 `SettingsTokens`。
- 非设置表面圆角 18 px；液体主题的 22 px 只属于设置卡片。
- 可见窗口检查（已批准）：设置卡片观感用户确认可以；气泡为不透明白卡、深色字，符合预期。

### 3.2 滑条拖不动、参数不实时（已修）

- 原因：设置卡片的整窗 `DragHandler`（`movable`）从子项 `MouseArea` 抢走按下。Qt 的默认 `grabPermissions` 包含 `CanTakeOverFromItems`。
- 修复：`StepSlider` 的 `MouseArea` 设 `preventStealing: true`。
- 已排除：把滑条改成 `DragHandler`（`dragThreshold: 0`）。它与卡片拖动互相抢占，窗口仍会被带动。不要重试这个方向。
- 测试：`test_glassSliderDragSetsTheValueWithoutDraggingTheWindow`、`test_glassSliderDragLeavesTheSettingsWindowOpaque`。
- 未验证：真实鼠标拖动。离屏测试无法驱动真实光标。

### 3.3 拖动卡片时闪烁两次（已修）

- 原因：松手时抓屏一次；卡片移动后，300 ms 的 `moveSettle` 计时器再抓一次。每次抓屏都让窗口透明 120 ms。
- 修复：`SettingsPopup` 记录抓屏所覆盖的矩形（`pictureRect`）。当前矩形与之一致时不重抓。松手和 `moveSettle` 都经过这个判断。
- 另一处修复：只有主题变化才重抓（`pictureTheme`）。之前每次设置写入都会导致一次重抓，因此滑条调节也会闪。
- 测试：`test_movingTheSettingsWindowDimsItOnce`。去掉守卫时为 4 次变暗，加上守卫为 2 次。
- 用户机器上的诊断：四次拖动各产生一次抓屏，没有同一位置的重复。另有一次同位置抓屏（00:28:44），原因未查清。

### 3.4 模糊、饱和度、折射无效（未解决）

见 §4。

## 4. 渲染问题：证据与方案

### 4.1 用户观察

用户选 A：在雾面玻璃的设置卡片里，能看到清晰的桌面画面，拖动模糊、饱和度、折射时画面不变。

### 4.2 已确认

- 抓屏正确。用户机器上每次抓屏得到 540×665 的图（DPR 1.25，窗口 432×532 DIP），非空，约 1300 种颜色，alpha 全为 255。
- 提供器已注册：`AppController::create` 中 `addImageProvider("glass")`。
- 数值通路正确（离屏探针）：`Controller.setFrostedBlur(100)` 后，`GlassSurface.blur` 从 10 变为 100，两个 `QQuickMultiEffect` 的 `blur` 从 0.1 变为 1，`blurEnabled` 为真，`blurMax` 为 60。
- 结构缺陷（独立的 bug，必须修）：frosted 模式下，无遮罩的 `liquidBlur`（`MultiEffect`）始终可见，叠在 frosted 层之上，角落为直角。它没有 `visible` 绑定。

### 4.3 已排除

- 不是抓屏为空。
- 不是渲染后端被强制：代码与 shell 环境变量中都没有 `QSG_RHI_BACKEND` 或 `QT_QUICK_BACKEND`。CMake 中的 `--no-opengl-sw` 只用于部署。
- 不是 QtQuick.Effects 模块缺失：Qt 6.9.0 安装中 `qml/QtQuick/Effects` 与 `Qt6QuickEffects.dll` 都存在。（路径见 `config/paths.json` 的 `qtRoot`。）

### 4.4 未确认

- 为什么 `MultiEffect` 在用户的 Direct3D 11 机器上不生效。离屏渲染也不生效：纯 `MultiEffect` 在 blur 为 0 和 1 时像素完全相同，带遮罩的 frosted 结果是单色。离屏结果不能作为依据。
- 可能的原因：Qt 或 RHI 对 `MultiEffect` 的支持；场景搭建问题（例如透明 `Window`、`ShadowCard` 的 holder、opacity 链）。两者尚未区分。

### 4.5 方案

**方案 A（推荐，待用户批准）**：模糊、饱和度、圆角遮罩、流光折射全部在 C++ 中计算。`GlassBackdrop` 或提供器按参数输出已处理的图像，并按参数缓存。QML 只显示图像，叠加现有的填充与描边。

- 删除 `MultiEffect` 与 `src/app/shaders/liquid.frag`，并去掉 CMake 中对应的 `qt_add_shaders`。
- 模糊：分离式盒式模糊，三次近似高斯。半径 = 模糊百分比 × 60 px，与当前 `blurMax` 的口径一致。
- 饱和度：按亮度混合，与 CSS `saturate` 近似。
- 圆角：按 DPR 生成 alpha 遮罩。
- 折射：复用 `glass_math` 的位移图，每个像素采样模糊后的图。
- 测试：用 gtest 覆盖模糊、饱和度、折射的纯函数（确定性输入）。`tst_phase2e` 仍做数值通路断言。
- 性能：每次滑条移动都要重算。尚未测量，需在 release 构建上测，目标单次不超过 20 ms。
- 文档：改写 ADR 0034 的渲染部分，并新开 ADR 0037 记录取舍；更新 `UI.md` 的 3.1c、3.1d 实现说明。
- 需要用户同意，因为这改变了 ADR 0034 的决定。

**方案 B（备选）**：先做一个最小的可见测试窗口，只放一个不透明的 `MultiEffect`，判断是 Qt 的问题还是场景搭建的问题。需要用户批准：说明窗口尺寸与时长（不超过 15 秒），只开一个。

不要再用离屏探针判断 `MultiEffect` 是否生效。

## 5. 阶段 2：其余表面

范围以 `UI.md` 的「表面结构」表为准，逐个对照各表面条目里的「玻璃主题」说明。待做的表面包括：托盘菜单、解释气泡、选区动作条、统计、花费、词汇、单词年度、更新卡片、通知卡片、词汇删除确认卡，以及桌宠。

### 5.1 设计前提（来自 ADR 0036 中记录的上限）

- 色表：每个玻璃表面需要自己的色表实例。阶段 2 改为每个窗口持有一份 `TokenTable`，由窗口根沿组件树传递，取代双单例。
- 抓屏：`GlassBackdrop` 目前只有一份图（`AppController` 的 `glass_` 成员）。阶段 2 需要每个表面各自的图，提供器按表面 id 取图。
- 抓屏流程沿用设置窗口的做法：抓前隐藏该表面（窗口透明 120 ms，与 `Mask.qml` 的 `settleMs` 同口径），抓后恢复。位置变化的重抓要像 `pictureRect` 那样判断，避免重复变暗。
- 桌宠窗口必须排除抓屏（`UI.md` 表面表中写明「排除抓屏」），用 `WDA_EXCLUDEFROMCAPTURE`。离屏下会告警，需真机验证。
- 共用组件：`ShadowCard`、`Segment`、`Switch`、`SwitchRow` 已带 `tokens` 属性。其他表面用到的共用组件（`MixedText`、`StatRow`、`MenuRow`、`Icon`、`Chip` 等）是否需要同样的注入，要逐个确认。
- 圆角与弹出层：按 `UI.md` 各表面条目执行（液体主题下为 22 px、13 px 等）。

### 5.2 建议顺序

1. 先完成 §4 的渲染方案。阶段 2 的每个表面都依赖它。
2. 设置窗口的渲染验收（真机）。
3. 托盘菜单，以及统计、花费、词汇（结构相近）。
4. 解释气泡与选区动作条（抓屏频率与位置最复杂，放后面）。
5. 单词年度、更新卡片、通知卡片、词汇删除确认卡。
6. 桌宠（最后，因为有排除抓屏和预览不套玻璃的约束）。

每一步的验证：qtest 数值断言、qml-lint、gtest，以及真机观感（需批准）。

## 6. 需要用户决定或批准

1. 渲染方案：A（C++ 计算，改写 ADR 0034/0035），还是 B（先做最小可见测试）。
2. 可见窗口测试：每次说明尺寸与时长（不超过 15 秒），只开一个。未批准不开。
3. 选区动作条与词汇删除确认卡在液体主题下，是保持 13/18 px，还是改为 22 px。这是阶段 1 交接中的待决项，未决。
4. 输入框的拖动：设置里的输入框（`TextInput`、路径框等）拖选文字时，是否也会带动窗口。它与滑条属于同类问题，尚未验证，也未修复。需要确认是否一并处理。
5. ja、es 的玻璃相关译文未经母语者核对，已标注「待译者核对」。
6. 合并 `main`：未决定。本次不开 PR。

## 7. 已知风险与边界

- 松手后仍有一次抓屏，窗口变暗 120 ms。这是有意保留的设计，记为已知风险。
- 抓屏画面不落盘、不写日志、不上传（ADR 0034）。诊断代码已移除；不要把画面或像素数据写进日志。
- 深色桌面上的浅字档仍可能过不了 AA（ADR 0035）。
- 液体主题的字体颜色参数只作用于深字档。
- 窗口跨两块屏幕时抓不到画面，玻璃退为透明（雾面仍有白底）。
- 离屏平台不支持窗口透明度（"This plugin does not support setting window opacity"），也不可靠地渲染 `MultiEffect`。离屏只能验证数值与结构。
- 真实鼠标拖动、观感、Direct3D 11 上的渲染，只能在真机上看。

## 8. 踩过的坑

- Bash 工具：不要用 `>/dev/null` 或 `2>/dev/null`（钩子会拦截），用 `|| true`，或把输出写到文件。不要用 heredoc 写文件（钩子会拦截），用 Write 工具。
- Qt 测试：`grabImage(item)` 直接返回 `QImage`。`Util.findAll` 不穿过 `Loader` 的 item，需要合并 `data` 与 `children` 遍历。`effectiveVisible` 在本 Qt 版本中未定义，用 `visible`。`MultiEffect` 是 C++ 类型，`toString()` 为 `QQuickMultiEffect(...)`，不带 `_QMLTYPE`。
- `DragHandler` 的默认 `grabPermissions` 为 `CanTakeOverFromItems | CanTakeOverFromHandlersOfDifferentType | ApprovesTakeOverByAnything`（Qt 文档）。两个 `DragHandler` 互抢会来回切换，不要用 `dragThreshold: 0` 绕过。
- qmllint 读的是构建树里的 qmldir 副本。改了 QML 或 CMake 后，先构建再跑 `qml-lint.sh`，否则会误报 `[unqualified]`。
- 新增 QML 单例：要在 CMake 中设置 `QT_QML_SINGLETON_TYPE` 并加入 `QML_FILES`，然后重新配置构建。
- qtest 的结果用 `-o "$TEMP/x.txt,txt"` 输出。必须设 `QT_FORCE_STDERR_LOGGING=1`，否则 `QDEBUG` 不输出。
- `-input <文件>` 可以只跑一个 QML 文件，用于临时探针。
- 启动 Lens：用 PowerShell 的 `Start-Process`，并在 PATH 前加上 `config/paths.json` 的 `qtBin` 目录。`cmd start` 会报 Access denied。
- 提交规则（`AGENTS.md`）：不加 AI 署名。`.githooks/commit-msg` 会拒绝 `Co-Authored-By`。不要用 `--no-verify`。
- zhlint：`AGENTS.md` 规定不用于临时交接文档。本文件不跑 zhlint。
- 机器路径（Qt、构建目录等）保存在 `config/paths.json`，文档与脚本中只引用键名，不写值。

## 9. 验证命令

路径约定：`<qtBin>` 指 `config/paths.json` 中 `qtBin` 的值。

- Debug 构建（`build-ninja`）：`cmd //c "scripts\build\build.bat"`。单个目标加 `--target <名称>`。
- Release 构建（`build-ninja-release`，运行用）：`cmd //c "scripts\build\build-release.bat"`。
- QML lint：`bash scripts/quality/qml-lint.sh`。基线为 0，需先构建。
- qtest（离屏，使用临时设置副本）：`QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 PATH="<qtBin>:$PATH" ./build-ninja/test/qtest/lens_qtest_surfaces.exe -o "$TEMP/x.txt,txt"`。只跑玻璃：加 `-input test/qtest/surfaces/tst_phase2e.qml`。
- gtest unit：`cmd //c "scripts\build\build.bat --target lens_gtest_unit"`，然后运行 `build-ninja/test/googletest/lens_gtest_unit.exe`（PATH 加 `<qtBin>`）。
- 翻译（改了用户可见字符串时）：见 `AGENTS.md` 的 Translations 一节。

## 10. 建议使用的 skills

- `e2e-side-effect-safety`：任何可见窗口、抓屏、真实 Lens 运行之前。
- `fresh-arch`：在阶段 2 的色表与抓屏结构、以及 §4 的渲染方案改动之前，从需求重新推导。
- `taste-skill`：设计或审计 UI 表面之前（`AGENTS.md` 要求）。
- `visual-qa`：渲染之后检查（可见检查获批之后）。
- `diagnosing-bugs`：渲染或抓屏异常时，尤其是 §4。
- `qt-qml`、`qt-qml-test`、`qt-qml-test-run`、`qt-qml-review`：QML 的实现、测试与审查。
- `cpp-oop-style`、`qt-cpp-review`、`qt-cmake-project`：采用 §4 方案 A（涉及 C++ 与 CMake 改动）时。
- `system-software-audit`：阶段 2 多表面抓屏之前，检查隐私、资源与频率。
- `domain-modeling`：改 `GLOSSARY.md` 或新增 ADR（如 0037）时。
- `code-review`：提交前，对照 `UI.md` 与编码规范。
- `git-workflow-and-versioning`：提交与推送。
- `writing-prompt`：编辑面向代理的文档（如本文件）时。
- `ponytail:ponytail`：写代码时，保持最小改动。

## 11. 引用（不在此重复）

- 提交历史：分支 `design/glass-prototypes` 上本次提交的 `git log`。
- 旧交接：`$TEMP` 下的三份（见 §1）。
- 探针与截图（不入库）：`$TEMP/glass-render-probe/`、`$TEMP/glass-visible-check/`。

## 12. 脱敏说明

本文件不含 API key、设置文件内容或个人信息。路径以仓库相对路径、`$TEMP` 和 `config/paths.json` 的键名表示。
