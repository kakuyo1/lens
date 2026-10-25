---
target: 批次⑥ 更新卡 · 最终版
total_score: 34
max_score: 40
p0_count: 0
p1_count: 0
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\UpdateCard.qml"
target_fingerprint: "sha256:8bbfabee71f21a160e96061893f612353e10250adf82f6e1cb232e50f6ad3c08"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\UpdateCard.qml"
timestamp: 2026-10-25T00-23-11Z
slug: src-app-qml-panels-updatecard-qml
final: true
final_commit: 6d85341
code_commit: 5dbe839
---

# 更新卡 · 最终 critique

**方法**：最终源码逐条对照 + 战役内真机快照（`%TEMP%\lens-crit4\update-card-light.png`、`update-card-dark.png`、`%TEMP%\lens-critique\update-card-light.png`、`update-card-dark.png`）+ `tst_updatecard.qml` 的末次全量。最终版没有重新拍摄下载 / 校验 / 安装的运行态；这些按源码与 qtest 记，并在末尾列为未采样。

**范围**：`UpdateCard.qml`、`UpdateDuty` 的下载状态、`ShadowCard`、UI-SPEC §4.0、ADR 0107。评分只对这张卡，不把关于页的「检查更新」入口算进来。

## 1. 最终读数

**34 / 40（Good）**。趋势：32 → **34**（这个 slug 从第 2 轮新评起步，没有再早的分数）。

最后 +2 来自两件结构修复：View 与 Cancel 退出墨底家族，校验中不再抽走主胶囊和进度块。剩下 6 分不是工艺欠账，而是这张卡的三条硬契约：窗口不收焦点、校验中不给 Cancel、失败句只说「失败」不交代文件去向。

| # | 启发式 | 分 | 最终依据 |
|---|---|---|---|
| 1 | 系统状态可见 | 4 | 状态行 + 字节 + 百分比 + ETA 全部由 duty 驱动；校验中保留进度块与禁用的 Checking…；就绪态写「Checksum verified.」 |
| 2 | 贴近真实世界 | 4 | 「1:02:33 left」是时钟不是单位词；代理失败直接说中国大陆通常需要代理 |
| 3 | 用户控制 | 3 | 关闭 / 跳过 / 查看 / 下载 / 安装都有；但校验中没有 Cancel，正是文件已在磁盘上的时刻 |
| 4 | 一致性与标准 | 3 | View / Cancel 改次级，墨底只剩 Download / Install；按钮高 26px 与设置页 27px 胶囊家族仍差 1px |
| 5 | 错误预防 | 3 | checksum 门是真正的预防；Cancel 不再是最响的按钮；Install 仍是单击执行，没有二次确认 |
| 6 | 识别而非回忆 | 4 | 所有状态与动作都画在卡上，不需要记；失败句是固定键值句，不让传输层原话漏出 |
| 7 | 灵活与效率 | 2 | 键盘只有 Escape；无焦点契约下没有 Tab、没有 Return，指针是唯一动作路径 |
| 8 | 美学与极简 | 4 | 校验中不再两次 reflow；就绪态只掉一次进度块，卡片高度稳定；排版紧 |
| 9 | 错误恢复 | 3 | 三种固定失败句与 danger 色；checksum 失败说文件已删，通用失败不说残余字节 |
| 10 | 帮助与文档 | 4 | 代理提示就在卡里，不指向文档；校验 / 安装 / 跳过各自说清 |

**认知负荷**：通过五项，失败三项——控制样式与后果（Cancel 已降级，但 Install 仍是单 click）、布局稳定（已从两次 reflow 降到一次）、输入路径镜像（五个控件，键盘只有一个键）。

## 1.1 本面优先项

按全局 C 顺序（见 `FINAL-00-overview.md` §4）；键盘遍历按 ADR 0107 保持不做。

| 序 | 候选 | 项 | 级别 |
|---|---|---|---|
| 1 | C9 | 校验取消与失败去向 | P2 / P3 |
| 2 | C17 | 按钮宽度与文案 | P3 |

## 2. 专属性判定

**Authored。** 中国大陆代理失败句、时钟 ETA 绕开单位词翻译、checksum 的信任弧（校验 →「Checksum verified.」→ 安装）都不属于通用更新器。状态行是 duty 的纯函数，失败句是三条固定键值句，传输层的字符串永远上不了这张卡；这让本地化闭得住，也让错误文案不会随网络层漂移。

## 3. 最终切片里真正改掉的

1. **墨底只留给 Download / Install**：`UpdateCard.qml:330-337` 的 `secondary` 在 downloading 与 verifying 为 true；View 与 Cancel 走 `panel2` + `line` 描边（`:380-400` 附近），Download / Install 才用 `ink`。Cancel 不再用最强样式催读者中断。
2. **校验中不再两次 reflow**：`primaryAction("verifying")` 返回「Checking...」（`:85-86`），主胶囊保留但 `enabled: false`、透明度 0.45；进度块 `visible` 扩展到 downloading || verifying（`:265`），校验中不抽走。卡片从「下载中 → 校验中」不再先矮后高，只剩「校验中 → 就绪」掉一次进度块。
3. **Return 没有硬塞进去**：最终版评估过应用级 Return 映射主操作，但在无焦点窗口下它要么是死键，要么和别的表面抢键；于是删掉，契约保持「指针 + Escape」，UI-SPEC 4.0 与 ADR 0107 写明。这是一次正确的否决，不是漏做。
4. **测试钉住状态与动作**：`tst_updatecard.qml` 的 `test_thePrimaryButtonFollowsTheDownloadState`、`test_thePrimaryIntentFollowsTheStateAndNothingRunsWhileChecking`、`test_viewIsSecondaryRatherThanInk`、`test_escapeTakesTheCardDownWithoutAskingForAnything` 覆盖主按钮文案、意图、次级样式与 Escape。

## 4. 问题账（已关闭）

| 来源 | 问题 | 结局 |
|---|---|---|
| 第 2 轮 P2 | 下载中 Cancel 戴墨底，最不想要的动作样式最响 | Cancel 改次级、全透明度；View 同步改次级 |
| 第 2 轮 P2 | verifying 主胶囊消失、进度块抽走，卡片两次 reflow | 保留禁用的 Checking… 与进度块；qtest 钉意图 |
| 第 2 轮 P2 | 无焦点窗口没有键盘路径 | 不硬塞 Return；契约明确为指针 + Escape，ADR 0107 记录；按钮仍有读屏名称与按压动作 |
| 第 1 轮 P2 | 下载中 Cancel 靠 0.58 假装禁用 | 改次级样式，不再假禁用 |
| 第 2 轮 P2 | 就绪态没说 checksum 已验 | 「Checksum verified. The installer is ready to run.」 |
| 第 2 轮 P3 | 失败句不交代文件去向 | 部分：checksum 说已删；通用失败仍不说残余字节 |

## 5. 仍然开着的问题

| 优先级 | 问题 | 证据 | 为什么还没关 |
|---|---|---|---|
| P2 | 键盘只有 Escape，安装 / 下载 / 取消都不可键盘触达 | `UpdateCard.qml:28` 的 `WindowDoesNotAcceptFocus`；`:170-177` 只有 Escape Shortcut；按钮有 `Accessible` 名称与按压（`:355-357`、`:395-397`、`:434-436`）但没有 Tab | ADR 0107 把这写成契约；读屏以浏览模式可按，键盘专用读者不能。要打开就得先改「更新卡不打断阅读」的产品决定 |
| P2 | 校验中没有 Cancel | `primaryIntent("verifying")` 返回 `""`；`primary` 只显示禁用的 Checking…，Close 只收卡不取消 | 已裁 D4：加次级 Cancel，行宽固定；取消中止校验并删除已下载字节。实现前仍是开放 P2 |
| P3 | 通用失败句不交代残余字节 | `downloadText("failed")` 的默认句是「Download failed. Check your connection.」；checksum 句才说「It was deleted」 | 第 2 轮 P3；部分失败可能留下临时文件，读者不知道要不要自己清 |
| P3 | 按钮高 26px，设置页胶囊 27px | `UpdateCard.qml:322`、`:378`、`:415` 的 26px 对 UI-SPEC 4.4 胶囊 27px | 若更新卡是独立尺度，应在 §4.0 写明 26px；若是同族，应改 27px |
| P3 | Install 是单击执行，没有二次确认 | `installRequested()` 直连运行安装器 | 更新器的行业惯例；checksum 门承担了大部分防护，仍不是零风险 |
| P3 | Skip 的语义是「这个版本」，Close 是「现在」；两个出口的差别只靠文案 | `closeCard()` 与 `skipRequested()` 两条路径 | 第 1 轮开放问题；对赶时间的读者，Skip 仍是最安静的那一个 |
| P3 | 下载中与校验中的按钮宽度会随标签变化 | 「Cancel」→「Checking...」→「Install」都在同一位置，但宽度不同 | 行是右对齐的，变化不推走左侧；仍会在状态切换时轻微抖 |

## 6. 边界（不是待办）

- **不收焦点**：这是更新卡的核心契约。卡在读者工作流中突然出现，抢焦点会改变下一次注入复制键的去处；ADR 0107 只保证读屏地板，不保证 Tab。
- **不是模态**：卡可以拖，点外可关；它不是必须处理完才能继续工作的对话框。因此 Install 的单击执行沿用更新器惯例，不做二次确认。
- **checksum 门是真实防护**：下载文件的哈希不匹配就删除，并在句子里说出来；这比任何确认按钮都更接近「错误预防」的本意。
- **真实下载 / 校验 / 安装运行态**没有重新采样；进度条的宽度动画、校验中的禁用胶囊、就绪态的掉块，都只有源码与 qtest 证据。

## 7. 画像

- **Sam（辅助技术）**：每个按钮都有名称与按压动作，读屏能执行；但键盘进不来，Tab 与 Return 都不存在。他能「按」，不能「走到」。
- **Riley（赶时间）**：卡自己出现、自己下载，最响的按钮现在是 Download / Install，不再误导她去点 Cancel；校验中没有 Cancel，她只能等或关掉卡。
- **Alex（效率型）**：Escape 能收，拖动能挪；没有键盘快捷键执行主操作，必须回鼠标。
- **预算敏感者**：更新卡不涉及花费；代理失败句直接给出原因，省一次搜索。

## 8. 开放问题

1. 校验中要不要给一个次级 Cancel？已裁 D4：给——次级样式，行宽固定；取消中止校验并删除已下载字节。文件已在磁盘上时，读者不再只剩「等」与「收起卡」。
2. 通用失败句要不要统一说「部分文件已删除 / 保留」？checksum 分支已经说了，另外两类没有。
3. 更新卡真的不能收焦点吗？如果它出现在读者刚敲键盘的中途，抢焦点确实危险；但一次 Install 只能靠鼠标，是否值得为它开一个「激活后短暂收焦点」的例外？
4. 26px 与 27px 是两套尺度还是一处漂移？如果更新卡要自己的 26px，规格应该明说，而不是让评审每轮重新发现。

## 9. 证据与未采样

**证据**：`%TEMP%\lens-crit4\update-card-light.png`、`update-card-dark.png`；`%TEMP%\lens-critique\update-card-light.png`、`update-card-dark.png`；`tst_updatecard.qml` 的关闭 / Escape / View / Skip / 状态文案 / 主按钮 / 意图 / 次级样式 / 空卡。

**未采样**：下载中、校验中、就绪、失败四态的真实像素；进度条宽度动画；校验中的禁用胶囊；代理 / checksum / 通用失败三型的真机布局；真实读屏器会话。
