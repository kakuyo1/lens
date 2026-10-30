# 测试覆盖率基线（2026-10-30）

这一份盖两条线：`src/` 下的 C++ 行，与 QML 模块里被求值的表达式位置。两条线的工具、口径、分母构造
方式都不同，**读数不可比，也不许平均**。要读的是 “数的是什么”，见
[`what-coverage-counts-2026-10-05.md`](what-coverage-counts-2026-10-05.md)；要补哪一块，见
[`coverage-plan-2026-10-03.md`](coverage-plan-2026-10-03.md)。

本报告取代 [`test-coverage-baseline-2026-10-05.md`](test-coverage-baseline-2026-10-05.md)：那一份保留作历史，不改数字。
两份的分母不可直接比：10-05 的 C++ 是 26 个文件、1,860 条可执行行，QML 是 852 个位置；本轮 C++ 是
199 个文件、18,767 条可执行行，QML 是 5,737 个位置。C++ 的目标集也变了：10-05 含
`lens_gtest_integration`，本轮按副作用安全规范只跑四个无头目标。**所以不要把 77.7% 读成 “比 75.7% 涨了
2 个点”，也不要把 82.7% 读成 “比 86.6% 跌了 3.9 个点”。**

本报告只回答 “哪些东西被测试跑过”，不回答 “跑过的结果对不对”。本轮 `lens_qtest_surfaces` 有 1 条红例
（见 §3.1），覆盖率仍按实际执行计入；这不是一份全绿基线的证明。该例已在采集后修复并单例复验为绿，但本报告
保留采集时的读数。

## 1、报告信息

| 项目 | C++ 一条线 | QML 一条线 |
| --- | --- | --- |
| 分析范围 | `src/` 下被四个测试目标链进去的源文件与头文件 | `src/app/qml/` 下的模块文件 |
| 工具 | `OpenCppCoverage 0.9.9.0` | `qmlprofiler`（Qt 6.9.0）+ `tree-sitter-qmljs` |
| 采集方式 | 对 `build-ninja` 已有的 Debug 二进制逐个插桩；unit / perf / components 各一份，surfaces 按 38 个 case 文件分两片、每片 2 并发；最后合并成一份 Cobertura XML | components 与 38 个 surface case 文件各起一份 profiler，2 并发；取 trace 里出现过的 `{文件, 行}`，除以解析树里的可求值位置数 |
| 单位 | 机器码行 | 绑定 / signal handler / 函数的位置 |
| 原始数据 | `test/records/coverage/coverage.xml` | `test/records/qmlcov/qml-coverage.json` |
| 构建树 | `build-ninja`（Debug）；HEAD `735c75d`，测量时工作树 clean | 同左 |

本轮 QML 侧为了控制单次运行时长，把 surface 文件分成两片跑，最后用同一个
`scripts/quality/qml-coverage.js` 合并；与 `scripts/quality/qml-coverage.sh` 的并集口径相同。

## 2、执行摘要

C++ 行覆盖 **77.7%（14,581/18,767，199 个文件）**；QML 执行覆盖 **82.7%（4,742/5,737）**。

C++ 的 18,767 是可执行行，不是 `src/` 的 46,862 行物理 C++：声明、宏、没有可执行行的头文件，以及只被
`lens.exe` 引用而不进测试二进制的文件，都不在其中。QML 的 5,737 是可求值位置，不是行；一个绑定只要
对象创建就求值一次，“两条分支都走过没有” 它答不上来。

四个 C++ 目标共 **1,644 例：1,595 绿、48 skip、1 红**。48 个 skip 中，46 个是快照采集（components 5、
surfaces 41），1 个是 surface fixture（`PanelsCritique::test_anEmptyFilterSaysSo`），1 个是单位套件的外部包
用例；红例是
`Settings::test_theClipboardSegmentPicksThePolicyAndFollowsItBack`。本次运行与 CI 都没有设置
`LENS_QA_SNAPSHOT_DIR`，快照用例只 skip，不比较图像。

## 3、C++ 一条线

### 3.1 目标与结果

| 目标 | 用例 | 结果 |
| --- | ---: | --- |
| `lens_gtest_unit` | 1,085 | 1,084 绿、1 skip |
| `lens_gtest_perf` | 1 | 绿 |
| `lens_qtest_components` | 134 | 129 绿、5 skip |
| `lens_qtest_surfaces` | 424 | 381 绿、1 红、42 skip |
| `lens_gtest_integration` | — | 未跑：驱动真实鼠标与剪贴板 |
| `lens_gtest_smoke` | — | 未跑：需要 key 与真实调用费用 |
| `lens_qtest_pet` | — | 不在 `coverage.sh` 的目标列表 |

`lens_gtest_integration` 的 29 个用例覆盖真实鼠标、真实剪贴板与桌面窗口路径，本轮按 E2E 副作用规范没有执行；
`lens_gtest_smoke` 需要 `settings.local.json` 里的 key 且会花钱，也没有执行。`lens_qtest_pet` 是窗口无关的
C++ QTest（25 个 test slot），但不在 `coverage.sh` 的默认目标列表，也不在 CI 的任何 workflow 里；这是本轮
没有覆盖到的一块。

单位套件的 skip 是 `PetCatalog.ExternalPackagesMadeOutsideTheRepositoryAreAcceptedOrRefusedAsWritten`，
它要 `LENS_PET_PACKAGES` 指向仓库外的宠物包。components 与 surfaces 的 47 个 skip 中，46 个来自快照用例，
另 1 个是 `PanelsCritique` 的 fixture 条件；快照消息写明要 `LENS_QA_SNAPSHOT_DIR`
（`scripts/qa/qml-snapshot.ps1`）。这些 skip 不计入绿例，也不等于视觉已验证。

**红例**：`Settings::test_theClipboardSegmentPicksThePolicyAndFollowsItBack`
（`test/qtest/surfaces/tst_settings.qml:171`）报 “the settings panel has no clipboard segment”。原因是设置页的
“Popup & clipboard” link 被新内容推到折叠线以下，测试没有先滚动就点击。覆盖率采集时它是红的；随后用
`Util.reveal(link)` 修好，单例复验通过（3 passed、0 failed）。本报告的覆盖率数字仍来自修复前的
那次运行。

### 3.2 按目录

| 模块 | 文件数 | 覆盖行 | 可执行行 | 行覆盖率 |
| --- | ---: | ---: | ---: | ---: |
| `src/app` | 104 | 7,132 | 9,914 | 71.9% |
| `src/capture` | 13 | 786 | 1,100 | 71.5% |
| `src/core` | 36 | 3,118 | 3,318 | 94.0% |
| `src/llm` | 7 | 1,223 | 1,426 | 85.8% |
| `src/ocr` | 12 | 991 | 1,150 | 86.2% |
| `src/pictures` | 4 | 322 | 425 | 75.8% |
| `src/speech` | 9 | 262 | 431 | 60.8% |
| `src/sync` | 5 | 263 | 435 | 60.5% |
| `src/update` | 3 | 325 | 361 | 90.0% |
| `src/util` | 6 | 159 | 207 | 76.8% |
| 合计 | 199 | 14,581 | 18,767 | 77.7% |

`src/` 现在有 263 个 C++ 源文件与头文件，其中 199 个有可执行行并进入报告；其余 64 个是声明与宏为主，
或只被 `lens.exe` 引用，**不是 0%**。`app/main.cpp`、`llm/qt_log.cpp` 仍然不出现，原因同上一份报告。

### 3.3 低覆盖文件

下表路径相对 `src/`；只列低于 61% 且可执行行不少于 30 的文件，完整清单在
`test/records/coverage/coverage.xml`。

| 文件 | 覆盖行 | 可执行行 | 行覆盖率 |
| --- | ---: | ---: | ---: |
| `app/glass/glass_capture.cpp` | 0 | 237 | 0.0% |
| `app/platform/local_voice.cpp` | 0 | 68 | 0.0% |
| `ocr/qt_http_transport.cpp` | 0 | 56 | 0.0% |
| `app/glass/glass_capability.cpp` | 0 | 40 | 0.0% |
| `capture/foreground_window.cpp` | 0 | 38 | 0.0% |
| `app/glass/glass_duplication.cpp` | 7 | 221 | 3.2% |
| `app/platform/selection_text_grabber.cpp` | 14 | 229 | 6.1% |
| `pictures/picture_sources/commons_source.cpp` | 5 | 71 | 7.0% |
| `app/platform/autostart.cpp` | 4 | 38 | 10.5% |
| `speech/voice_downloader.cpp` | 10 | 89 | 11.2% |
| `app/platform/mouse_selection_hook.cpp` | 11 | 92 | 12.0% |
| `util/profile.cpp` | 11 | 58 | 19.0% |
| `app/glass/glass_desktop.cpp` | 8 | 35 | 22.9% |
| `capture/ocr_engine.cpp` | 53 | 180 | 29.4% |
| `app/duties/speech_duty.cpp` | 111 | 285 | 38.9% |
| `app/duties/sync_duty.cpp` | 89 | 207 | 43.0% |
| `app/platform/global_hotkey.cpp` | 41 | 90 | 45.6% |
| `sync/github_auth.cpp` | 110 | 210 | 52.4% |
| `app/duties/explanation_duty.cpp` | 263 | 446 | 59.0% |
| `app/glass/glass_backdrop.cpp` | 230 | 383 | 60.1% |
| `app/duties/picture_duty.cpp` | 83 | 137 | 60.6% |

读这张表要先分两类：

- **桌面与真机路径**：`glass_capture`、`glass_capability`、`glass_desktop`、`selection_text_grabber`、
  `mouse_selection_hook`、`autostart`、`local_voice`、`foreground_window`。它们的主要入口在
  `lens_gtest_integration` 或真实桌面上，本轮没跑 integration，所以读数低是目标集造成的，不是这一轮
  新弄坏的。
- **网络与后台路径**：`commons_source`、`voice_downloader`、`ocr_engine`、`speech_duty`、`sync_duty`、
  `github_auth`、`explanation_duty`、`picture_duty`。其中一部分有桩提供契约覆盖，但完整分支仍空；这是
  真正的补测候选。
- `util/profile.cpp` 的 19.0% 是构建配置：`LENS_ENABLE_PROFILE` 在这棵树上关着。

### 3.4 与 10-05 的口径差

10-05 的目标表是 `lens_gtest_unit` 59、`lens_gtest_perf` 1、`lens_gtest_integration` 17、
`lens_qtest_components` 51、`lens_qtest_surfaces` 71，合计 199 例；**59 只是 unit 一个目标，不是覆盖率语料
的全量**。本轮四个目标合计 1,644 例；unit 从 59 长到 1,085（约 18 倍），旧报告五个目标合计 199，本轮
四个目标 1,644（约 8 倍），与源码增长同量级。

旧报告的 26 个文件路径已经按职责重组（`core/domain`、`core/document`、`app/platform` 等），逐文件对号没有
意义；旧报告的 75.7% 还包含 integration，本轮不含。**本报告不把 75.7% 当作当前覆盖率，也不以它为比较
基线。**

## 4、QML 一条线

### 4.1 总数

**4,742/5,737 = 82.7%**，由 `lens_qtest_components` 与 `lens_qtest_surfaces` 两个目标共同跑出。事件计数：
`Binding` 33,925、`HandlingSignal` 1,319、`Javascript` 44,869、其他 0；字面量绑定 2,426 个两侧都不算。

10-05 的 QML 是 738/852 = 86.6%，当时表里 22 个文件、`Binding` 2,586、`Javascript` 3,432；本轮模块有
102 个文件、5,737 个位置。分母与文件数都长了一个量级，**82.7% 与 86.6% 不能直接比**。

本轮 QML 覆盖率仍然包含那条 `tst_settings` 红例的 trace；红例之前的执行已经计入，但这仍然不是一份全绿
套件的证明。修复发生在采集之后，报告保留采集时的 QML 读数。46 个快照用例 skip，QML 侧没有对它们做视觉评估。

三条读这份表的注意事项：

- **未执行的位置里有一部分是 `qsTr()` 标签**，无头跑不渲染，那不是缺口。
- **这个数看不见分支。**一个绑定只要对象被创建就会求值一次，“两条分支都测过没有” 它答不上来。
- **它依赖构建树是新的。**QML 编进二进制，改了 `.qml` 没重新链接测试目标，量到的就是旧模块。

### 4.2 按目录

| 目录 | 文件数 | 执行 | 可求值位置 | 覆盖率 |
| --- | ---: | ---: | ---: | ---: |
| `(root)` | 3 | 89 | 113 | 78.8% |
| `bubble` | 10 | 490 | 547 | 89.6% |
| `capture` | 2 | 100 | 111 | 90.1% |
| `components` | 26 | 704 | 834 | 84.4% |
| `dev` | 1 | 0 | 1 | 0.0% |
| `panels` | 12 | 902 | 1,033 | 87.3% |
| `pet` | 9 | 389 | 423 | 92.0% |
| `selection` | 7 | 627 | 701 | 89.4% |
| `settings` | 23 | 1,370 | 1,896 | 72.3% |
| `theme` | 9 | 71 | 78 | 91.0% |
| 合计 | 102 | 4,742 | 5,737 | 82.7% |

`settings` 是最大的一块，也是最低的一块；它下面有 23 个文件、1,896 个可求值位置。

### 4.3 低覆盖文件

下表路径相对 `src/app/qml`；完整清单在 `test/records/qmlcov/qml-coverage.json`。

| 文件 | 执行 | 可求值位置 | 覆盖率 |
| --- | ---: | ---: | ---: |
| `settings/previews/SelectionBarPreview.qml` | 0 | 148 | 0.0% |
| `settings/previews/NotePreview.qml` | 0 | 36 | 0.0% |
| `dev/ProfileScenario.qml` | 0 | 1 | 0.0% |
| `components/content/ChainText.qml` | 14 | 45 | 31.1% |
| `settings/SettingsCatalog.qml` | 9 | 27 | 33.3% |
| `components/content/PictureStrip.qml` | 67 | 104 | 64.4% |
| `settings/pages/SettingsFontPage.qml` | 20 | 31 | 64.5% |
| `settings/pages/SettingsSyncPage.qml` | 68 | 99 | 68.7% |
| `settings/pages/SettingsLearningPage.qml` | 124 | 176 | 70.5% |
| `Main.qml` | 53 | 75 | 70.7% |
| `settings/pages/SettingsOcrPage.qml` | 83 | 113 | 73.5% |
| `settings/pages/SettingsExtensionsPage.qml` | 81 | 109 | 74.3% |
| `settings/pages/SettingsClipboardPage.qml` | 29 | 39 | 74.4% |
| `settings/pages/SettingsGeneralPage.qml` | 120 | 161 | 74.5% |

两个 preview 文件与 `dev/ProfileScenario.qml` 是真正的无头缺口：当前用例没有实例化它们。`SettingsCatalog.qml`
只有 33.3%，`Main.qml` 从 10-05 的 25/30 长到 53/75，绝对执行位置变多、比例下降；这不是旧测试失效，而是
新代码没有被同一批用例覆盖。设置页整体偏低，和上面那条 `tst_settings` 红例是同一块区域，但红例本身不是
唯一原因。

## 5、没跑什么

- **`lens_gtest_integration`**：29 个用例，需要真实鼠标、剪贴板与桌面；没有跑。玻璃、选区、开机启动等
  文件的低读数主要来自这里。
- **`lens_gtest_smoke`**：2 个用例，需要 key 与真实调用费用；没有跑。
- **`lens_gtest_glass_probe`**：4 个采集探针，需要交互桌面；没有跑。
- **`lens_qtest_pet`**：25 个 test slot，窗口无关；它不在 `coverage.sh`，也不在 CI。这是一条便宜但尚未接入
  的 C++ 覆盖率缺口。
- **46 个快照用例**：本次运行里因没有 `LENS_QA_SNAPSHOT_DIR` skip。它们用
  `scripts/qa/qml-snapshot.ps1` 手动采集 PNG，不是视觉回归断言；本报告不把它们算作视觉覆盖。
- **1 个 surface fixture 条件**：`PanelsCritique::test_anEmptyFilterSaysSo` 因为 fixture 同时带 known 与
  new words 而 skip；它不计入绿例。
- **不设覆盖率阈值**：理由在 `.github/workflows/coverage.yml` 与
  [`what-coverage-counts-2026-10-05.md`](what-coverage-counts-2026-10-05.md)；同树读数会小幅摆动，本轮也没有
  做双跑稳定性复验。

## 6、复现命令

```bash
export QT_ROOT=/c/Qt/6.9.0/msvc2022_64   # Git Bash 的 Unix 路径，不要写成 C:/...
sh scripts/quality/coverage.sh test/records/coverage/base lens_gtest_unit lens_gtest_perf lens_qtest_components
sh scripts/quality/coverage.sh --cases 1/2 test/records/coverage/surfaces-1 lens_qtest_surfaces
sh scripts/quality/coverage.sh --cases 2/2 test/records/coverage/surfaces-2 lens_qtest_surfaces
sh scripts/quality/coverage-merge.sh test/records/coverage
sh scripts/quality/qml-coverage.sh
```

`coverage.sh` 的默认目标包含 `lens_gtest_integration`；上面显式点名四个无头目标，是为了避开真实鼠标与
剪贴板。`QT_ROOT` 在 Git Bash 里必须是 `/c/Qt/...`；写成 `C:/Qt/...` 会被 PATH 的冒号拆坏，测试进程会以
`0xC0000135`（DLL not found）退出。C++ 那条线要求 `OpenCppCoverage.exe` 在
`C:\Program Files\OpenCppCoverage\`；QML 那条要求 `node_modules` 与 Qt 的 `qmlprofiler`。

**稳定性**：本轮只跑了一次，没有做两次复跑；C++ 的并集是 14,581/18,767，QML 报的是落在
`test/records/qmlcov/qml-coverage.json` 里的那一次。QML trace 的事件计数可能随运行小幅变化，不应把这份
报告当作阈值。
