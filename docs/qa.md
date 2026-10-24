# UI 表面验证

> 权威：当前真机验证方法。测试目标、何时跑测试与 QTest 的覆盖范围见 `TEST.md` §2；本文只讲真机驱动、拍照、快照与像素判读。

## 驱动顺序

1. **开发面（首选）**。状态、触发、可见性与等待先用 `scripts/qa/lens-dev.ps1` 的 `--dev-script`（或
   `--dev-pipe`）回答：`surface show|hide` 打开和收起表面，`surface route` 走面板自己的导航函数进分类与
   子页（ADR 0103），`surface scroll` 把表面的滚动体钉到内容位置（ADR 0105），
   `setting set` 走真实 setter，`state` 读
   可见窗口与表面状态，`wait visible|hidden|painted|<n>s` 等一等。它不移动指针、不抓屏，答案是一条条
   JSON，脚本退出后仍留在 `-Out` 里。宠物也有两把真实 setter：`setting set petEnabled on|off` 与
   `setting set pet <id>`，`state` 的 `pet` 报 `id` / `enabled` / `shown` / `action`，所以进场、换宠与
   告辞可以用 “关 → 等 → 开 → 等” 的剧本读阶段，不需要鼠标。开发面只存在于
   `LENS_ENABLE_DEV_SURFACE=ON` 的构建；`ui-run.ps1` 的 release 树默认没有它，要驱动就先建一棵带
   开关的树（`docs/adr/0068`）。宠物的一轮开关剧本见 `scripts/qa/scenarios/pet-ceremony.txt`。
2. **离屏 QTest**。开发面回答不了的摆放、关闭规则、面板数字与派生值，先由 `lens_qtest_surfaces` /
   `lens_qtest_components` 断言（`TEST.md` §2），不建真实窗口，也不碰桌面。
3. **真实鼠标与截图（最后手段）**。只有两类问题需要它：像素本身（卡片对没对齐、阴影糊不糊、图标
   画出来没有、字体与颜色），以及必须经过真实低层钩子、命中测试或剪贴板的输入路径。用之前先写清楚
   属于哪一类，别用截图代替状态断言，也别为了 “看一眼” 移动读者正在用的指针。

表面没有主窗口，全部靠触发才出现，所以像素与真实输入仍要靠真机。能脱离屏幕断言的那部分已经搬进
`lens_qtest_surfaces`（`TEST.md` §2）：摆放、关闭规则、面板上的数字、托盘菜单那几个派生值，都不再需要快门。
剩下的仍是这里的事——卡片对没对齐、阴影糊不糊、图标画出来没有。`scripts/qa/ui-*.ps1` 是四个这样的工具，
都是 PowerShell，都先把自己设成 per-monitor-v2 感知——不设的话截到的是 Windows 已经拉伸过的位图，
糊与偏移都会被量成假的。

- `ui-run.ps1`：起应用。`lens.exe` 比 `src/` 旧就先重建 release 树，然后脱离当前 shell 启动，报出 PID、
  `logs/lens.log` 与有没有出现 `Lens is up`。已在运行就拒绝再起（`-Restart` 先停）。要拍照必须这样起，
  并带上 `LENS_QA_VISIBLE_IN_CAPTURE=1`：玻璃主题下全部表面对抓屏隐藏（ADR 0038 / 0041），不设它，
  `ui-capture.ps1` 拍到的是一张空桌面。
- `ui-capture.ps1`：截屏。`-Virtual` 截整个虚拟桌面，`-Crop x,y,w,h` 裁一块，`-Zoom` 最近邻放大，
  `-Profile` 打出穿过中间那一行的灰度值。清晰还是糊、卡片对没对齐，靠这行数字定案，光看图看不出来。
  虚拟桌面那一档不是可选项：表面不只落在主屏上，而 `GetSystemMetrics(0/1)` 只知道主屏。
- `ui-input.ps1`：驱动指针，这四个里只有它会动指针。`click` 点一下，`drag` 等步进拖，`dragfast`
  相对位移连发，`reveal` 把指针停到主屏最后一行、把自动隐藏的任务栏勾出来。低层鼠标钩子看得见注入
  事件，所以拖选、点外部、拖面板都能这样触发。等步进那一档看着没问题，密度超过手速的 `dragfast`
  才拖出过发散——两者都要跑。
- `ui-tray-rects.ps1`：打任务栏与通知区的窗口矩形，是 QML 侧位置的比对基准。托盘图标报的坐标是
  设备无关像素、自动隐藏的任务栏其矩形落在主屏下方，两件事都是拿它查出来的。

另有一个不驱动也不拍照的：`mouse-stall-probe.ps1` 在应用启动的那几秒里连发 `mouse_event` 并给每次调用
计时，报告慢过阈值的调用。它回答的是 “鼠标被抢走了吗”，不是 “界面画对了吗”——回归对象是钩子跑在自己线程
上那条约束（`docs/QML.md` §5），一次运行三四十秒。

还有一条既不驱动、也不显示窗口的路：`qml-snapshot.ps1` 用 Qt 的 `offscreen` 平台跑 QTest，把真实 QML 场景
渲进内存图再写成 PNG。它不显示窗口、不动鼠标、不抓桌面，所以不受桌面遮挡影响，也不需要人工在屏幕前。注意：
offscreen 不会栅格化 `MultiEffect`，因此图标与阴影在这类快照中可能为空；需要核对真实图标、阴影和窗口合成时，必须
在未设置 `QT_QPA_PLATFORM` 的真实平台重跑同一用例。方法
分两处：`setup.cpp` 用 `QFontDatabase::addApplicationFont` 逐个载入 Windows 字体文件，再把输出目录经
`lensQaSnapshotDir` 交给每个引擎——offscreen 平台本身不带字体，不载就是满屏方框；用例调用 `testutil.js` 的
`saveSnapshot(testCase, item, name)`，内部走 `TestCase.grabImage(item)` 再 `save()`。脚本设好
`QT_QPA_PLATFORM=offscreen` 与 `LENS_QA_SNAPSHOT_DIR` 后驱动目标，默认写到 `test/records/snapshots/`。它验的
是渲染结果本身——字形、字重、对齐、配色；摆放与交互仍归 `TEST.md` §2 的 `lens_qtest_surfaces` 和上面那四件套。

一轮的顺序：先用开发面（`lens-dev.ps1` 的剧本或管道）到达要验的状态；开发面回答不了的像素问题，才接着
用 `ui-tray-rects.ps1` 量基准、`ui-run.ps1` 起应用、`ui-input.ps1` 驱动、`ui-capture.ps1` 截图，必要时
`-Profile` 或放大看像素。为看清而临时加进 QML 的东西（计时器、`console.log`）**提交前
必须删干净**——`.githooks/pre-commit` 的第三项会拦下 `src/` 下带这两个串的暂存文件，手工确认是
`grep -rn PROBE src/`。观察点本身由应用自己的日志承担：`logs/lens.log` 每行都刷，可以 `tail -f`。release 树的默认级别是 info，QML 的 `console.log` 属于 debug，要设 `LENS_LOG_LEVEL=debug` 才进日志；C++ 侧用 `LENS_INFO` 即可。离屏的 QTest 没有真实的任务栏、多屏与缩放，只能验证换算公式，屏幕上的位置要在真机截图上核对。

状态与资源问题归开发面，像素问题归 `ui-capture`。开发面（`docs/adr/0068`）只在编译开关 `LENS_ENABLE_DEV_SURFACE` 打开时存在：`--dev-script` 与 `--dev-pipe` 能直接显示、隐藏表面，读出窗口是否可见、幕布发布了几次、生效主题；量内存与线程用 `scripts/qa/lens-measure.ps1`，它把 `state` 作为一列记进 CSV。开发面不动指针、不抓桌面，所以只回答 “现在是什么状态”，不回答 “画对了没有”。摆放、阴影、图标、对齐仍用 `ui-capture` 看；鼠标事件的路径（钩子、命中测试）仍用 `ui-input` 在真机上驱动，因为开发面的 `selection` 跳过了钩子与剪贴板，不能代替它。开发构建的数字只与同构建的数字比，报告首行记 `version` 的应答。

驱动时有两个坑，都是撞了六七次调用才摸出来的：

- **焦点与拖拽必须落在同一个 PowerShell 进程里**。拆成两次调用，中间那次的控制台会抢走前台焦点，注入的
  Ctrl+C 于是送去了控制台而不是被拖选的应用——取文拿到的是控制台里的东西，或者什么都没有。
- **取词只认发生变化的剪贴板**。重选同一段文本之前要先用 `Set-Clipboard` 换掉剪贴板内容，否则钩子读回来
  的还是上一次的字符串，日志只会说 `did not change within 250 ms`，看着像钩子没触发。
