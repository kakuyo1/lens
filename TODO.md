# TODO by agents

> 只写还没做的。每条说清还剩什么、从哪下手；做完直接删，不记日期与过程。

## 义项图的真机复核

代码已落地（`IMAGES.md`、`docs/adr/0079`），但卡片上的图片条还没在真机上看过一眼：入口与三态（加载骨架 / 就绪 / 失败那一行 `Open image search`）、1 / 2 / 3 张图、暗色与玻璃主题、高 DPI、贴近屏幕底部时卡片仍整体收进工作区。要看它需要一份真带 `imageable` 义项的解释（或手工往解释缓存里种一条 `imageable` / `imageQuery` 的条目），然后点开入口。顺带看一眼：三条义项各一枚入口在 270 px 卡内是否拥挤；C++ 侧已按真实 Commons 响应验过筛选与取值（12 条候选留 3 条），未验的是画面。

## 桌面宠物的真机复核

**待机（`docs/adr/0048`）**

- `wag` / `sniff` / `scratch` 三条动作都靠锚点位移、表情与一层标记区分，人眼确认读得出是三个不同动作。
- 停手后确认不再固定三分钟入睡；每次打盹都自己醒。

**闲逛与躲藏（`docs/adr/0076`）**

- 七只各走一轮，看步态与窗口路径是否协调。
- 贴边躲藏、拖出、拖不够缩回、单击露出的部分引出（ADR 0095）；拖到边缘松手主动缩入；指针靠近露条时眼睛跟转、探身加深；多屏与任务栏在左右时的工作区边缘。
- 躲藏中触发复习：木牌立在露出的内侧、不被边缘裁掉，回答后继续躲；提问期间宠物窗口不再内移。
- 行走时每 20 ms 移动窗口的性能与闪烁。

## 木牌提问牌的真机复核

提问牌已从手举纸牌改成立在脚边的木牌（`docs/adr/0088`）。还没在真机上看过：木牌与短柱在桌面背景上的阴影与木质观感、默认与小/大倍率下的字和像素边、悬停小卡在上下边缘的夹取、贴近屏幕左右与底部、躲藏时立在露出的内侧，以及没有 `ask` 的第三方包也出牌。静态离屏快照见 `docs/qa.md` 的 `qml-snapshot.ps1`（`ReviewSign::test_captureTheClosedSign`、`Pet::test_captureTheSignPreview`）。

## `lens_qtest_pet` 的旧狗脚点断言

该目标不在 `TEST.md` 的目标表里，`aPreviewedSceneExposesItsSheetsAndAlignment` 仍按旧狗的脚点（57，84）与场景对位 -1.5 断言，实际脚点是（51，84）。先确认狗在共享场景里是否仍对位，再改测试或场景；本次木牌改动没有碰空闲身体或场景对齐。

## 表面反复开合后的残留增长

每 10 次开合约 +10 MB、+38 个句柄，稳定但不归零。候选是 QML 图片缓存的 serial 条目与字体缓存，待定位；方法见 `docs/metrics/resource-footprint/glass-memory-2026-10-18.md` 第 9 节。

## 玻璃卡片首次显示时透明

设置窗首次显示后 `opacity` 为 0，约 1.7 秒后由 `components/GlassHost.qml` 的 `captureTimer` 恢复；托盘约 0.66 秒。先确认 `keepOutOfCapture` 是否因窗口句柄尚未创建而返回假，再查定时器为何晚约 1.4 秒，并在真机上看一眼观感。

## 定位气泡 hover 抖动

真机日志出现过悬停状态在 79 ms 内翻两次，导致倒计时重启与反馈按钮反复收放；受控复现失败。再遇到时核对 `HoverHandler` 的光标位置与卡片屏幕矩形，确认是光标出界还是窗口可见性变化。

## 玻璃主题的 CPU 占用

只走 GDI 的屏（标准档、Windows 10、两个快速源都被拒的机器）在桌面静止、设置面板常开时约 10% 单核，主要是每秒 15 次的 GDI 读取。能复制或能被 Windows Graphics Capture 读取的屏已不再付这笔（ADR 0087）；剩下的按顺序试：空闲时暂停、按负载退帧、缩小读取区域。读数与判色见 `docs/adr/0040`、`docs/adr/0041`。

## QML profiler 完整场景入口

`scripts/profiling/qml-profile.ps1` 在 `-Scenario full`（默认）时不设置 `LENS_QML_PROFILE_SCENARIO`，应用不会启动自动退出的完整场景；单独传 `-Scenario` 又缺值。统一脚本、应用与复现文档的约定。

## 档位词书数据

档位要落地，需要 8 份词表：B1–B2 / C1–C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / 雅思 / TOEFL。现在 `data/wordlist.txt` 是纯词频表，不含考试标签，只能按词频阈值近似，会误判：CET-6 词书含大量非高频词，词频前列也混着 CET-4 之外的专业词。确定来源（自备 txt / 公开词表）后，把 known-set 预置从 “词频阈值” 改为 “词书集合”。见 `docs/history/PHASE1.md` 4.1。

## 不规则表把 offer 归给了 off

`data/irregulars.tsv` 有 `offer → off`，命中即裁定，`offer` 的请求、缓存、气泡标题与已会标记都按 `off` 处理。核对 WordNet 的 `verb.exc` / `noun.exc` / `adj.exc` / `adv.exc`：源里没有就删掉这一行并重跑 `scripts/data/gen_irregulars.py`；源里有，就把 `docs/history/PHASE1.md` 4.1 的反例改掉。

## 更新源镜像

检查更新只走 GitHub Releases，地址在 `data/update.json`，国内需代理。实测国内可达的镜像（版本接口与发布页两处），定下后只改 `data/update.json`，并在 `SECURITY.md` 写明出站地址。

## 账号与同步的扩展

- **其他登录方式**：ADR 0042 只定 GitHub 设备登录。评估微软账号（OneDrive）、谷歌账号、手机号或邮箱验证码；后两者需要服务端发码，属于托管后端，要另开 ADR，并实测国内短信与邮件的可达性与费用。
- **托管后端**：先确认 gist 方案是否满足需求；若不满足，再选托管免费层或自建，核对额度与费用，另开 ADR。

## 自动扫描的 OCR 替换方案

先实测 Tesseract `--psm 3` / `--psm 11` 与 `--dpi 96` 对 96 DPI 小字的影响，再决定是否放大后识别。中期候选：

- **Windows.Media.Ocr**：系统自带、离线、带词框，不必起子进程；依赖系统语言包，走 WinRT，与 ADR 0014 冲突，采用前另开 ADR。
- **UI Automation TextPattern**：Chrome / Edge / Word / Acrobat 可直接读文字与词框；代价是首次启用 Chromium 无障碍模式的性能开销，以及 `SECURITY.md` 要写清读取边界。

评估口径：96 / 125 / 150% DPI 下的词框精度、整窗耗时、中文 `traineddata` 依赖、打包体积、隐私边界。切换点是给 `capture::OcrEngine` 加词框能力，扫描侧不感知具体引擎。

# TODO by user

## 气泡在多词错峰弹出时的对齐与去重策略

## 自定义安装页面

## level 加上自定义词表支持

## 卸载时可选是否删除所有用户信息
