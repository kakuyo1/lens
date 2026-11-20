# 视觉快照基线报告

## 1、报告信息

| 项目 | 内容 |
| --- | --- |
| 分析范围 | `test/qtest` 下 29 个调用 `saveSnapshot` 的 QML 用例文件 |
| 采集日期 | 2026-11-21 |
| 参考机 | 本仓库开发机，Windows 11 26200，Debug 树 `build-ninja` |
| Qt | 6.9.0，MSVC 2022（`B:/qtt/6.9.0/msvc2022_64`） |
| 渲染器 | 软件后端，`QT_QUICK_BACKEND=software` |
| 字体 | `LENS_QA_FONT_MODE=yahei`（Microsoft YaHei，setup.cpp 必需字族之一） |
| golden | `test/snapshots/golden/`，7 张，约 155 KB |
| 容差 | 单通道 16；不同像素超过图像 5% 判失败 |

本报告记录首批视觉回归基线的口径：有哪些快照用例、选了哪一批、为什么、本机重复采集的结果，以及 CI 接线后
**能证明什么、不能证明什么**。容差的决定（参考机 golden + 5%）已在计划 D2 锁定，这里只给实现与证据。

## 2、结论

首批四个用例、七张图：气泡卡两主题、词面板与删除确认各一张浅色、设置页目录与模型服务页两张深色、混排排版一张。
三次独立采集加一次 CI 形状的合并进程采集，七张全部 0 个不同像素，退出码 0。离屏在软件后端出图，软件后端
固定后同一份二进制逐字节可重复；`grabImage` 在软件后端可用。

计划里的 “4 主题 × 3 表面” 只铺到 2 主题 × 3 表面：快照用例本身只写 `light` 与 `dark`，`frosted` /
`liquid` 根本没有快照用例（玻璃在软件后端也不栅格化），托盘菜单也没有快照用例。表面覆盖是气泡卡、设置页、
词表面（外加它上面的删除确认），再加一组 `lens_qtest_components` 的混排排版；比 4 × 3 少 4 个主题格与
托盘菜单这一整面。

本机能证明的到此为止。**跨机器（CI 运行器）没有证明**：Windows Server 2022 的 DirectWrite、YaHei 与
Segoe UI Variable 的文件版本都可能与本机不同，5% 容差能否吸收只能看推送后的第一次 CI 运行。计划要求的
“10 次连续绿” 也不可能在本轮完成。

## 3、快照用例清单

共 29 个文件、78 处 `saveSnapshot` 调用；循环写出的帧（主题、尺寸、宠物、网格、GIF）比调用数更多，
`tst_readme_gifs` 的帧名还带毫秒。主题列里
“两主题” 指用例内循环 `light` / `dark`；“夹具” 指沿用测试 settings 夹具的 `dark`。稳定列是本轮实测：
“三次一致” 为三次独立采集逐字节相同，“红” 为同一用例自身重复采集出现差异，“未试” 为没进本轮试验。

### 3.1 组件（`lens_qtest_components`）

| 文件 | 用例 | 图 | 主题 / 状态 | 稳定 |
| --- | --- | --- | --- | --- |
| `tst_iconbutton` | `test_theFocusedButtonPhotographs` | `focus-ring-icon-button` | light，键盘焦点环 | 未试 |
| `tst_settingsupdate` | `test_captureTheUpdateBlock` | `settings-update-light`、`-dark` | 两主题，更新区块 | 未试 |
| `tst_switch` | `test_theFocusedSwitchPhotographs` | `focus-ring-switch` | light，焦点环 | 未试 |
| `tst_typography` | `test_captureChineseAndLatinTypography` | `typography` | 无主题切换，中英混排与统计行 | 三次一致（入选） |
| `tst_updatecard` | `test_captureTheCard` | `update-card-light`、`-dark` | 两主题，更新卡 | 未试 |

### 3.2 表面（`lens_qtest_surfaces`）

| 文件 | 用例 | 图 | 主题 / 状态 | 稳定 |
| --- | --- | --- | --- | --- |
| `tst_bubble` | `test_captureTheBubble`、`…WithoutItsCountdown`、`…WithTheSenseEntry` | `bubble`、`bubble-review`、`bubble-sense-entry` | 夹具，答完后三种卡 | 未试 |
| `tst_bubble_etymology` | `test_snapshotBothThemes` | `bubble-etymology-light`、`-dark` | 两主题，词源块 | 未试 |
| `tst_bubble_example` | `test_snapshotBothThemes` | `bubble-example-light`、`-dark` | 两主题，例句块 | 未试 |
| `tst_bubble_senses` | `test_snapshotBothThemes`、`…PictureEntry`、`…PictureStripFailure` | `bubble-senses-*`、`bubble-pictures-*`、`bubble-pictures-failed-*` | 两主题，多义项 / 图片入口 / 失败条 | senses 两主题三次一致（入选）；图片两例未试，图标在软件后端不画 |
| `tst_bubble_waiting` | `test_captureTheWaitingCard` | `bubble-waiting-light/-dark`、`bubble-midway-light/-dark` | 两主题，等待与流式 | 未试 |
| `tst_capture` | `test_snapshotTheCapturePage`、`…OcrSettingsPages`、`…RapidOcrSection`、`…OnlineOcrPage`、`…UploadConsentCard` | `settings-capture`、`settings-ocr`、`settings-ocr-rapidocr`、`settings-ocr-online`、`upload-consent` | 夹具，设置页各页 | 前两张三次一致；`settings-capture` / `settings-ocr` 与 `tst_settings_wave3` 同名，不签入；其余未试 |
| `tst_explanation_settings` | `test_snapshotPageContents` | `explanation-{learning,model,connection}-{light,dark}` | 两主题，三张解释设置页 | 六张里五张三次一致，`learning-dark` 红；整个用例不入选 |
| `tst_font` | `test_captureTheFontPage`、`…Preview` | `font-page`、`font-preview` | 夹具，字体页与预览 | 未试 |
| `tst_main` | `test_selectionBarRequiresTheConfiguredLetterCount` | `selection-bar-valid` | 夹具，行动条 | 未试 |
| `tst_notice` | `test_captureTheNotice` | `notice-light`、`notice-dark` | 两主题，通知卡 | 四轮里 `notice-dark` 红一轮（复用旧卡跨主题），不入选 |
| `tst_panels` | `test_captureTheRemovalQuestion` | `words-list-light`、`words-removal-light`、`words-removal-dark` | 两主题，词列表与删除确认 | 三张六次一致；dark 帧停在配色过渡中段（白卡浅字），记录的不是稳定状态，不入库 |
| `tst_panels_year` | `test_theYearWordsPanelProjectsTheControllerDaysAndPalette` | `year-words-light/palette/dark/closed` | 夹具（“light” 名其实在暗色下拍），调色板开合 | 未试 |
| `tst_pet` | `test_captureTheSignPreview`、`…AcrossSizes`、`test_captureAccessoryRows`、`test_snapshotSettingsPage` | `pet-actions-row`、`pet-preview-sign`、`pet-preview-sign-size-{0,10,20,100}`、`pet-accessories`、`pet-settings-<id>-{light,dark}` | 两主题（设置页）/ 宠物美术 | 未试 |
| `tst_pet_postcard` | `test_capturePetPostcard` | `pet-postcard-light`、`-dark` | 两主题，明信片 | 未试 |
| `tst_pet_scene` | `test_captureEveryScene`、`…SceneEntry`、`…ImageSourcePetActions` | `pet-scene-{swing,skate,dog_frisbee,dog_burying}`、`pet-action-angry`、`pet-entry-bath-{air,land}`、`pet-sample-*` | 动画帧，`pet-sample-*` 需外部宠物包 | 未试 |
| `tst_readme_gifs` | 六个 clip 用例 | `bubble-*`、`pet-*`、`year-*` 帧序列、`pet-ui` | light，逐帧 | 天然不可重复：帧名带 `Date.now()` 毫秒 |
| `tst_review` | `test_captureTheWordHint` | `word-hint` | 夹具 | 未试 |
| `tst_reviewsign` | 四个 capture | `review-sign-closed`、`review-sign-closed-size-0`、`review-card-open`、`review-answer-bubble` | 夹具，宠物招牌开合 | 未试 |
| `tst_selectionbar_fit` | `test_theDrawersFitTheirLabelsAndTheWindowInEveryForm` | `<style>-<scenario>-<language>` 网格 | 夹具，多种样式与语言 | 未试；同样写 `LENS_QA_SNAPSHOT_DIR` |
| `tst_settings_career` | `test_theCareerStatisticsPageCountsAndClears` | `career-stats-light`、`-mid`、`-zero` | light，两个滚动位置与清零 | 未试 |
| `tst_settings_confirm` | `test_captureTheConfirmation` | `settings-confirm-light`、`-dark` | 两主题，确认对话框 | light 三次一致，dark 红；用例不入选 |
| `tst_settings_preview` | `test_capturePreviewCard` | `settings-preview-light`、`-dark`、`-senses-en` | 两主题，悬停预览卡 | light / dark 红；用例不入选 |
| `tst_settings_wave3` | `test_captureTheCatalogAndDetailStates`、`test_captureSettingsInBothThemes` | 12 张：`settings-{catalog,learning,general-light,general-dark,ocr,capture,clipboard,about,model,connection,light,dark}` | 夹具目录与分页；后一例切主题 | catalog / learning / about / clipboard / model / connection 等三次一致（catalog、model 入选）；`settings-general-*` 与 `settings-light/dark` 红 |
| `tst_vocab_preview` | `test_captureVocabPreviewCard` | `vocab-preview-light`、`-dark` | 两主题，等级预览卡 | 红，不入选 |

## 4、首批选择与差距

入选四个用例、七张：

| 用例 | 图 | 选它的理由 |
| --- | --- | --- |
| `Typography::test_captureChineseAndLatinTypography` | `typography` | 中英混排、字重、等宽数字；无主题切换，三次一致 |
| `BubbleSenses::test_snapshotBothThemes` | `bubble-senses-light/dark` | 气泡卡最完整的一屏：标题、音标、三义项、中英混排；每轮新建卡，不跨主题 |
| `Panels::test_captureTheRemovalQuestion` | `words-list-light`、`words-removal-light` | 词表面的列表与删除确认，浅色；六次一致。`words-removal-dark` 虽稳定但停在过渡中段，不入库 |
| `SettingsWave3::test_captureTheCatalogAndDetailStates` | `settings-catalog`、`settings-model` | 设置目录与一页详情，无主题切换；目录里其余帧不入库 |

与 “4 主题 × 3 表面” 的差距：主题只有 `light` / `dark` 两格，玻璃两主题没有用例；表面有气泡卡、设置页、
词表面三面，托盘菜单没有快照用例。组件目标另有一张排版图，算加项不算表面。

## 5、渲染器与确定性

`offscreen` 平台在 Qt 6.9 上默认就解析到软件场景图后端，日志原句是
`qt.scenegraph.general: Loading backend software`。本轮把它显式固定为 `QT_QUICK_BACKEND=software`：
后端有名有姓，换 Qt 或换机器时差异来自哪一层能直接看出来；硬件 / RHI 路径不可用于无头 CI，抗锯齿也与软件
后端不同。`grabImage` 在软件后端照常出图，三次采集逐字节一致。

字体是同一类变量，且是更危险的一层。本机默认（`LENS_QA_FONT_MODE` 未设）会优先用可选字体 Noto Sans SC；
CI 的运行器通常没有这个文件。实测：其余条件不变，只把字体切到 `yahei-light` 再采集同一批十张，九张越过
容差，其中八张尺寸都变了（例如气泡卡 322×375 → 322×383、词列表 522×471 → 513×469），只有
`settings-catalog` 以 4.08% 通过。布局随字宽走，5% 救不回来。因此快照采集固定
`LENS_QA_FONT_MODE=yahei`：Microsoft YaHei 是 `setup.cpp` 已经要求存在的字族（缺失会 `qFatal`，CI 现有
QML 用例绿即证明存在），同一字族在两台机器上才有可比性。代价是 golden 记录的字面不是本机默认的 Noto，
换字体渲染的回归不在这条门里。

## 6、三次重复结果

每次采集都按用例各起一个进程（整套 surfaces 只在发版跑），采集目录与 golden 逐像素比较。最终七张
golden 的结果：

| 轮次 | 采集耗时 | 结果 |
| --- | --- | --- |
| A（生成 golden 的同一轮） | 98 s | 7/7 通过，全部 0 像素 |
| B | 94 s | 7/7 通过，全部 0 像素 |
| C | 92 s | 7/7 通过，全部 0 像素 |
| 合并进程（CI 形状，3 个表面用例一进程） | 33 s | 6/6 通过，全部 0 像素 |

三轮都按五个用例各起一个进程采集，后来撤出的 `Notice` 与 `words-removal-dark` 也算在耗时里；七张
golden 是其中最终留下的部分。
合并进程那一行只跑三个表面用例，是 CI `qml-surfaces` 的实际形状，另加 `qml-components` 的一条单独进程
（排版 0 像素）。

像素口径：气泡卡 123,326；词列表 240,597；删除确认（浅色）106,875；设置页 229,824；排版 55,380。比较本身每份
不到一秒。采集耗时的大头是五个进程各自的启动（词表、扫描池与协议加载约 1 s，Qt 与应用单例另计），
`Totals` 里的用例时间只有 0.2–25 s。

`tst_settings_wave3` 的 `settings-general-light/dark` 在 A/B/C 里互不相同，`tst_notice` 的
`notice-dark` 在 C 轮红（一半像素不同：深色标题还停在浅色墨里）。两者都是固定等待截到配色过渡中段；
前者不入库，后者整个用例撤出首批。`words-removal-dark` 是第三种情况：六次采集逐字节一致，但那一帧本身
停在过渡中段（白卡、浅色字），稳定地记录了读者看不到的状态，也不入库。这三件事是 “三次重复” 与
“只收读者看得到的稳定状态” 在首批选择上真正起作用的证据。

## 7、容差与比较脚本

`scripts/qa/qml-snapshot-compare.ps1`，PowerShell + System.Drawing，无新依赖。规则：

- 任一 R/G/B/A 通道差值大于 16，记一个不同像素；不同像素超过图像 5% 判失败。16 吸收平台取整与抖动，
  5% 容纳一条抗锯齿接缝而不放过一行移动。
- 每个 golden 都必须有渲染图；缺渲染图、缺 golden 目录、尺寸变化都是失败，退出码 1，不静默跳过。
- 渲染目录里多出来的图忽略：整套跑会写 GIF 与手工采集的帧，golden 只是选定的批次。
- 表格式输出每例一行：`PASS name 0.00% (0 of N pixels)`；`-Update` 只用渲染图重写已存在的 golden，
  不扩大集合，CI 不传这个参数。

已做的负向验证：在一个副本上涂一块 200×100 的洋红（8.13%）判失败；涂 120×60（2.93%）与一点
（0.02%）判通过；删掉一张渲染图判失败；改尺寸判失败；`-Update` 只改写已有的七张，多出来的 `settings-about`
没有进目录。

## 8、CI 接线

`qml-components` 与 `qml-surfaces` 各加三步，不新增 job：

1. `Snapshot renders`：同一个目标、只点首批用例，带 `QT_QPA_PLATFORM=offscreen`、
   `QT_QUICK_BACKEND=software`、`LENS_QA_FONT_MODE=yahei` 与 `LENS_QA_SNAPSHOT_DIR=<runner.temp>`；
   surfaces 的三个用例跑在一个进程里（与第 6 节的合并进程同形）。原有整套步骤不设快照目录，快照用例照旧
   跳过，`tst_readme_gifs` 也不会往共享目录里写几百帧。
2. `Snapshot compare`：对 `test/snapshots/golden/<target>` 跑比较，输出同时写进渲染目录的
   `diff-summary.txt`。
3. `Upload snapshot renders`（`if: failure()`）：把渲染目录整体上传为 `snapshots-components` /
   `snapshots-surfaces`。artefact 里有该步写出的全部 PNG（含 `SettingsWave3` 未入库的八帧）和每例一行的
   摘要，跨机器差异可以直接看图判断。

`scripts/quality/ci-changes.sh` 的 qml 列表已加上 `test/snapshots/` 与两个快照脚本：改 golden 或改采集 /
比较脚本都会跑这两个 job。普通开发运行不设 `LENS_QA_SNAPSHOT_DIR`，不受影响。

## 9、证明了什么、没证明什么

证明了的：本机上软件后端 + YaHei 字面下，七张 golden 三次独立采集逐字节一致，合并进程的 CI 形状也一致；
比较门的阈值、缺失处理与 `-Update` 行为符合设计；字体不固定会让九成图越过容差，因此字体固定是前提而不是
装饰。

没证明的：

- **跨机器可比性**。golden 来自 Windows 11 26200 与这份 YaHei / Segoe UI Variable / DirectWrite；CI 是
  Windows Server 2022，字体文件版本与 DirectWrite 都可能不同。5% 容差够不够，第一次 CI 运行才知道。
- **计划验收的 “连续 10 次零假红”**。本轮只做到本机三次重复；10 次是门在 CI 上连续通过后才有的结论。
- **其余用例**。宠物、预览卡、字体页、行动条等未试，不代表稳定；`Panels` 与 `Notice` 用同一种
  “切主题 + 固定等待”，`Panels` 目前六次全绿，是下一批里最先该被复测的。

## 10、下一步

1. 推送后看第一次 CI：`qml-components` / `qml-surfaces` 的 `Snapshot compare` 是否绿。若红，先读
   `snapshots-*` artefact 里的图和 `diff-summary.txt`，区分 “整张尺寸变了（字体或布局）” 与
   “文字边缘大片浅差（DirectWrite 抗锯齿）” 以及 “某一区域变了（真实回归）” 三种情况。
2. 假红累计超过 2 次就按 D2 退回人工采集，并把这里第 9 节改成结论；不要靠放宽 5% 硬扛。
3. `Panels` 的浅色两帧靠同一种固定等待，下一批复测优先；`settings-general-*` 与 `settings-light/dark`
   要进 golden，先把用例的固定等待换成读 token 淡入状态，而不是再加毫秒。
4. 托盘菜单没有快照用例；要做这一面，得先有它的离屏用例，属于新测试而非本轮的收口。
