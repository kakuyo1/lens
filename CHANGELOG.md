# Changelog

## 1.0.4 - 2026-11-23

Chrome's built-in PDF viewer scans again: a page that answers 0×0 while the viewer lays it out is kept and retried, and a viewer's words skip the occlusion hit-test that marked all of them covered.

### Fixed

- **A PDF page that is still laying out is waited for.** When Chrome's PDF viewer answers a 0×0 page box while it builds its document, the track keeps the page and re-reads its box for up to two seconds instead of dropping it; the lines then start without the reader changing the foreground. A page that stays empty past the wait is still refused and searched again.
- **A PDF viewer's words skip the sight pass.** A virtual document named "PDF document…" marks the page as a viewer. Its hit-test answers for the page, not the word, so every word used to be read as covered and its line was hidden. The page now draws as it reads, the same no-mechanics answer SumatraPDF already uses (docs/adr/0193).

### 中文

Chrome 内置 PDF 视图重新可以扫描：页面在布局中返回 0×0 时会被等待；PDF 视图的词跳过遮挡命中测试，不再整页被误判为被盖住。

#### 修复

- **布局中的 PDF 页面会被等待。** Chrome PDF 视图在布局期间返回 0×0 页面框时，轨道保留页面并最多重读两秒，不再需要切一次前台才能开始画线；超过等待仍是空框的页面才按原逻辑拒绝并重搜。
- **PDF 视图的词跳过遮挡判定。** 名为 “PDF document…” 的虚拟文档会把页面识别为 PDF 视图。它的命中测试返回的是页面而不是词的容器，原先把每个词都判成被盖住、线被清掉；现在按 SumatraPDF 已有的“无遮挡机制”路径随读随画（docs/adr/0193）。

## 1.0.3 - 2026-11-23

Three first-run corrections: auto scan starts on, a shipped pet starts smaller, and an installed build no longer carries the repository's development settings into a new profile.

### Changed

- **Auto scan starts on.** The master switch's default is now on; the four app switches keep their on defaults, and `Defaults` restores the master to on. The scan still reads only the text the supported app exposes, without OCR or an online service.
- **Shipped pets start at 25%.** The seven shipped pets draw at ×1.75 until the reader moves the size slider; an installed package keeps its declared scale as its reader's default. The review sign scales with the pet.

### Fixed

- **An installed build no longer copies the repository's `settings.local.json`.** Only a build tree carries that development document into a new profile. On a machine with the repository, a fresh install used to skip onboarding and inherit the developer's API key and word marks.

### 中文

三处首次运行的修正：自动扫描默认开启，随包宠物默认更小，安装版不再把仓库里的开发配置带进新配置。

#### 变更

- **自动扫描默认开启。** 总开关的默认值改为开；四个应用开关仍默认开，`Defaults` 也把总开关恢复为开。扫描仍然只读受支持应用提供的文字，不经过 OCR，不调用在线服务。
- **随包宠物默认 25%。** 七只随包宠物在读者拖动大小滑杆之前按 ×1.75 绘制；外部宠物包仍以包声明的 scale 作为首次默认。牌子随宠物等比缩放。

#### 修复

- **安装版不再复制仓库里的 `settings.local.json`。** 只有构建树会把这份开发配置带进新配置。在这台有仓库的机器上，全新安装原本会跳过首次引导，并继承开发者的 API key 与判定。

## 1.0.2 - 2026-11-23

A small release: the auto-scan page says less, the picture strip says one more thing, and the pet's review sign stays above its own window.

### Changed

- **The auto-scan page's note is one line.** It now says only that the scan marks words above your level in supported apps; the separate line listing Chrome, Notepad++, Sumatra PDF and PotPlayer is gone, because the switches below it already name them.
- **The picture strip names the VPN its pictures may need.** When pictures load, or when the network fails, the strip says at its foot that they come from Wikimedia Commons and a VPN may be needed to load them. A lookup that matched no pictures, and the settings preview's bundled samples, do not show the note.

### Fixed

- **The pet's review sign and its card follow the pet's re-assert.** The pet raises itself above late topmost windows once a second; the sign and its open card now raise with it, so the pet's canvas no longer buries them and leaves only the board's left sliver hoverable.

### 中文

这是个小版本：自动扫描页的说明收成一行，图片条补一行 VPN 注记，宠物的复习牌重新跟住宠物窗口。

#### 变更

- **自动扫描页的说明收成一行。** 现在只说 “在支持的软件里标出超出你档位的词”；下方那条列出 Chrome、Notepad++、Sumatra PDF 与 PotPlayer 的文字已删除，因为下面的开关已经写明了这些名字。
- **图片条标注可能需要的 VPN。** 有图时，或网络失败时，图片条底部说明图片来自 Wikimedia Commons，加载可能需要 VPN 服务；“没有匹配的图片”和设置页的样例图条不显示这行。

#### 修复

- **宠物的复习牌与小卡跟随宠物的再置顶。** 宠物每秒把自己抬回置顶带顶部；牌子与展开的小卡现在随之再抬一次，不再被宠物画布整块压住、只剩木板左缘可悬停。

## 1.0.1 - 2026-11-21

The first stable release. An installation of 1.0.0-alpha or 1.0.0-beta -- both report themselves as version 1.0.0 -- receives this release as an automatic update, because 1.0.1 compares greater than 1.0.0. Every entry below landed after the beta's tag, so the installed beta has none of them yet, including the handful the beta notes announced ahead of their commits. Please report what you find as an issue at https://github.com/kakuyo1/lens/issues, with the steps that led to it and your Lens version.

### Added

- **PotPlayer joins the auto scan.** The scan reads the subtitles a local video plays in PotPlayer without OCR: the playback position and status come from the player's own window messages, and the line text comes from an external SRT/ASS/SSA/VTT file beside the video or, failing that, an embedded text track read by a native Matroska walk. The words are laid out over the real video viewport with the caption's own styling, and the settings catalog's video row is a real switch (`autoScanPotplayer`) instead of a "Coming soon" placeholder (docs/adr/0208).
- **SumatraPDF joins the auto scan.** Its pages read through UI Automation like Chrome's: a zoom or reflow that changes the word boxes triggers a re-read, a page that awaits layout waits instead of ending the step, and the word units step past trailing punctuation (docs/adr/0197-0200). SumatraPDF 3.7 or later is required.
- A **career statistics page** under About counts the whole behavior log, per feature and per month (docs/adr/0194).
- The scan gains an **exclude list**: names and abbreviations the reader names in settings are kept off the page (docs/adr/0192).
- The word list is rebuilt from **OEWN 2022 and ECDICT**, and the scan keeps its own pool of the words it draws lines for (docs/adr/0186).

### Changed

- The scan holds a **page-language gate**: a page that reads as non-English keeps its lines down until it reads English again, and looking up a word no longer takes the lines of the page being explained down (docs/adr/0187). The gate reads per page and resets when the page is dropped (docs/adr/0190).
- The scan **reads each word's box once** and checks its page by climbing to the window root instead of enumerating every document, about 29% fewer UI Automation round-trips and about 45x cheaper page checks, so the lines arrive faster; the missed reads now say why they were missed, and an anchor that retires off-screen no longer churns the scroll (docs/adr/0188, 0189).
- **One D3D11 device is shared across every glass surface**, frame-locked, instead of one per surface (docs/adr/0191); the pet renderers step off that lock, the words card is sized in C++ so its list builds only the viewport it shows, and a frame-lock wait over 100 ms logs the waiter and the holder by name.
- **A stalled interface leaves a minidump** beside the crash dumps, with a `-hang` stem under `logs/dumps`, so a frozen window can be reported like a crash.
- A **theme switch now crosses the value tables**, so tokens that move with the theme -- motion, the card material -- follow it instead of waiting for a restart (docs/adr/0196).
- **Mochi is the default companion**, the "Extensions" category becomes "Pets", and the pet ships enabled (docs/adr/0195).
- The settings rows that preview a **font, size or language draw whole** instead of re-laying on hover: 85-96 ms down to 12-17 ms measured (UI-SPEC.md 4.4).
- **The typing pace moved one notch faster all the way up** (UI-SPEC.md 4.3), the switch knob stretches through its travel as it lands (UI-SPEC.md 4.17), and the WebDAV form stays in place, dimmed and inert, for the length of a sync round (UI-SPEC.md 4.4).
- **Every clear and delete asks once, in red**: one confirm-dialog shape replaces the mixed confirmations.
- The onboarding **tray lesson is its own fourth step** (docs/adr/0185).

### Fixed

- **The glass's stripes are gone**: every quality tier draws the same dense 13-tap kernel sampled at half its sigma, so the tap grid no longer shows as fine stripes along either axis (docs/adr/0201, superseding docs/adr/0111).
- **A verdict now covers every form of its family.** The popup, the review pool and the exports resolve a word through its lemmas transitively, so marking "mastered" no longer silently misses inflected forms.
- The scan's **track join moved off the GUI thread**, and the UI Automation connection and transaction timeouts are pinned, fixing the whole-application freeze a slow COM call used to cause.
- The pencil's **Alt signpost tucks into the word's top-right corner** instead of floating up and to the right (UI-SPEC.md 4.20).
- The settings **dropdown lists and previews are owned by their panel**, so the topmost panel can no longer be raised over them.
- **A press on the standing hover-review card keeps the panel up** (UI-SPEC.md 4.8).
- The sight **hit-test keeps dashed lines off the controls that cover their words** (docs/adr/0193).
- The pet re-exposes its window through a **frameSwapped watchdog** when the render loop parks it.
- **The installer and the portable zip carry the MSVC runtime now** (docs/adr/0207): the alpha and the beta shipped no CRT DLLs, so a machine without the VC++ redist could not start Lens at all.
- The payload carries a **complete third-party notice set**: Qt (LGPL-3.0), nlohmann/json and spdlog (MIT) join the existing Tesseract and voice notices.

### 中文

这是第一个稳定版。装着 1.0.0-alpha 或 1.0.0-beta(两者都自报版本号 1.0.0)的读者会作为自动更新收到本版，因为 1.0.1 比 1.0.0 大。下面每一条都在 beta 打标签之后落地，已装的 beta 一条都没有，包括 beta 说明提前预告过的那几条。遇到问题请到 https://github.com/kakuyo1/lens/issues 提交 issue，并附上复现步骤和 Lens 版本号。

#### 新增

- **PotPlayer 加入自动扫描。** 扫描无 OCR 地读取本地视频在 PotPlayer 里播放的字幕：播放位置与状态来自播放器自己的窗口消息，行文本来自视频旁边的外挂 SRT/ASS/SSA/VTT 文件，没有时退而读内嵌文本轨（原生 Matroska 解析）。词按真实视频视口排布并带字幕自己的样式，设置目录里的视频一行不再是“待上线”占位，而是真正的开关（`autoScanPotplayer`）（docs/adr/0208）。
- **SumatraPDF 加入自动扫描。** 它的页面像 Chrome 一样走 UI Automation：改变词框的缩放或重排触发重读，等待布局的页面原地等待而不是当作读完，词单元跳过尾部标点（docs/adr/0197-0200）。需要 SumatraPDF 3.7 及以上。
- 关于页下新增**生涯统计页**，按功能、按月份统计整个行为日志（docs/adr/0194）。
- 扫描获得**排除名单**：读者在设置里点名的名字与缩写不再上页（docs/adr/0192）。
- 词表改由 **OEWN 2022 与 ECDICT** 重建，扫描同时维护一份自己画线用的词池（docs/adr/0186）。

#### 变更

- 扫描有了**页面语言门**：读作非英语的页面整页收线，直到重新读作英语；查一个词不再把正在解释那一页的线收走（docs/adr/0187）。语言门按页读取，页面被丢弃时复位（docs/adr/0190）。
- 扫描**每个词的框只读一次**，页面检查改为爬到窗口根而不是枚举每个文档——UI Automation 往返少约 29%，页面检查便宜约 45 倍，虚线因此到得更快；漏读现在会说明原因，锚点因滚出屏幕退役不再引发滚动抖动（docs/adr/0188、0189）。
- **所有玻璃表面共享一个 D3D11 设备**，按帧加锁，不再各自持有一个（docs/adr/0191）；宠物渲染器让出该锁，词卡改在 C++ 里定尺寸、列表只构建它视口内的行，帧锁等待超过 100 ms 会按名记下等待者与持有者。
- **卡死的界面留下 minidump**，在 `logs/dumps` 里带 `-hang` 词干，冻结的窗口可以像崩溃一样被报告。
- **主题切换贯通值表**，随主题移动的令牌——动效、卡片材质——即时跟随，不再等重启（docs/adr/0196）。
- **Mochi 成为默认陪伴**，“扩展”分类更名为“宠物”，宠物默认开启（docs/adr/0195）。
- 设置里预览**字体、字号或语言的行整行绘制**，不再悬停重排：实测 85-96 ms 降到 12-17 ms（UI-SPEC.md 4.4）。
- **打字速度整体加快一档**（UI-SPEC.md 4.3），开关滑块在落定途中拉伸（UI-SPEC.md 4.17），WebDAV 表单在同步期间留在原地、灰化且不可编辑（UI-SPEC.md 4.4）。
- **每次清空与删除都用红色问一次**：统一的确认弹窗替代了过去各说各话的确认。
- 首次引导的**托盘一课单独成为第四步**（docs/adr/0185）。

#### 修复

- **玻璃的条纹消失了**：所有画质档画同一套 13 抽样点的密采样核、在 σ 的一半处采样，抽点网格不再沿两轴显出细条纹（docs/adr/0201，取代 docs/adr/0111）。
- **判定现在覆盖词族的每一种形式。** 弹窗、复习池与导出经词根传递解析，标记“掌握”不再悄悄漏掉屈折形式。
- 扫描的**轨道汇合挪离 GUI 线程**，UI Automation 的连接与事务超时被钉死，修复慢 COM 调用造成的整应用冻结。
- 铅笔的 **Alt 路标收进词的右上角**，不再悬在右上方（UI-SPEC.md 4.20）。
- 设置的**下拉列表与预览归属其面板**，最上层面板不再能盖住它们。
- **按在立着的悬停复习卡上时，面板不再落下**（UI-SPEC.md 4.8）。
- 视线的**命中测试让虚线避开遮住它们的控件**（docs/adr/0193）。
- 宠物渲染循环停驻窗口时，**frameSwapped 看门狗**会重新显露它。
- **安装包与便携版现在随附 MSVC 运行库**（docs/adr/0207）：alpha 与 beta 一个 CRT DLL 都没带，没有 VC++ 运行库的机器根本启动不了 Lens。
- payload 带上**完整的第三方声明**：Qt（LGPL-3.0）、nlohmann/json 与 spdlog（MIT）加入既有的 Tesseract 与音色声明。

## 1.0.0-beta - 2026-11-16

This is the beta release of Lens, the second release on the numbering that began at 1.0.0-alpha. It is more settled than the alpha, but it is still a beta: some surfaces are rough and results can still be wrong. Please report what you find as an issue at https://github.com/kakuyo1/lens/issues, with the steps that led to it and your Lens version.

### Added

- Lens keeps a local **behavior record** for future weekly, yearly and feature-use reports: one plain JSONL file per month under `%APPDATA%\Lens\events\`, retained for 12 months. Events carry the plaintext lemma of the word involved and, while the new **Record the app you read in** setting is on, the foreground process name; no sentence, definition, URL or clipboard text is recorded and no third party receives it. After a successful main sync, each month is encrypted with the sync passphrase's derived key and uploaded as its own file to the same gist or WebDAV directory, without changing the main sync document. The About page shows the local size and clears it, queueing the cloud copies for deletion (docs/adr/0180).
- When Lens **crashes** it now leaves more than the log: an in-process handler writes a minidump, a text summary (version, time, exception code, faulting address, module) and a marker into the log directory's `dumps` folder, keeping the newest five. Right away the handler spawns **lens_reporter.exe**, a small native dialog in the interface language that says what happened and offers the same two actions -- **Open dump folder** and **Report issue**, the latter opening a GitHub issue prefilled with the summary's facts; the dump itself travels only when you attach it, and nothing is uploaded on its own (docs/adr/0174, docs/adr/0175). On the next launch a notice card repeats the news with the same two pills.
- The desktop pet sometimes turns a stroll into a **taskbar trip**: when the screen has a fixed bottom taskbar (auto-hide off), it leaps in a small arc toward the roomier side of the screen, bounces on the taskbar's top edge, pauses, walks at a slow steady pace almost to that side's end and back to where it bounced, then hops back up to where it started on its walk cycle. Other pet behaviour is held until the trip ends; a drag or a click cuts it short. Each pet's chance is the optional `behavior.taskbarChance`, 0.005 by default (docs/adr/0155).
- The statistics popup opens a **Week profile** panel: this week's lookups from Monday to today, with the new-word and repeat shares, the parts of speech, lookups by hour, and the five most looked-up words. The counts come from the local history and cost nothing. **Interpret this week** is the one request: it sends a summary of those counts and the five words (no sentences, definitions or times) and bills like any other request. Its answer opens in a full-screen **weekly report card**, and the same button opens that card again without a request (docs/adr/0150, and docs/adr/0151 for the card).
- The **Week profile** panel adds one line of behaviour: the share of this week's word explanations made in a non-English language, and how many mother-tongue peeks were completed. Both are counted on the device from the history and the daily tallies, and neither enters the interpretation request. Switching the explanation language away from English also adds a note under that setting saying that the held-globe peek only exists under English; choosing English again takes the note away.
- The weekly **report card** shows this week's figures, the hours they fall on, and five observations. Each observation carries a figure drawn from the counts on the device, so the model writes no numbers (docs/adr/0151). Its **Story** button writes one short English story from up to fifteen of the week's looked-up words, chosen on the device; the words the story uses are underlined in the text. Only those words leave the machine, and the story is kept in memory for that week (docs/adr/0152).
- The words export's save dialog offers two learning-card formats, **CSV with explanations** and **Anki TSV with explanations**. Each row is a word from the list with its cached IPA, part of speech, English and Chinese explanations and example. A word whose explanation was never fetched keeps its row with those columns empty, and the export makes no request. The CSV has a header row and a UTF-8 byte-order mark so a spreadsheet reads the Chinese; the Anki file opens with `#separator:tab` and has no header, for Anki's text import.

- Auto scan moved out of "Capture & popups" into its own catalog row: a master switch, a short note, and a "Supported apps" group. Chrome and Notepad++ each have a switch (`autoScanChrome`, `autoScanNotepad`, both on by default); PDF reader and video subtitles are dimmed rows marked "Coming soon". The scan runs only when the master switch and the switch of the app in front are both on.

- Auto scan reads **Notepad++ documents** as well as Chrome pages. The reader is picked by the window: a Scintilla editor is read through its own messages (`SCI_GETTEXTRANGEFULL` for the text, `SCI_POSITIONFROMPOINT` and `SCI_POINTX/YFROMPOSITION` for each word's box), every other window goes through UI Automation. The word units are split locally from the document's UTF-8 bytes, and an edit that changes the text drops the page and re-scans. `SCI_GETTEXTRANGE` (2162) is not used: Scintilla 5 removed it for documents over 2 GiB and sending it stops the editor.

- Auto scan **holds a non-English page whole**. The page text the names are read from now also judges the page's language: a CJK majority, or too few English function words among enough letter tokens, holds every line back until the page reads English again; a page too short to judge is left as it was. English prose and English technical pages both keep their lines (docs/adr/0187).

### Changed

- The settings document no longer holds the sync credentials in the clear. The GitHub token and its refresh token, and the WebDAV app password, are sealed to the reader's Windows account with DPAPI before they are written and open again on read. A document an older build wrote answers as before until the next write seals it, and the first launch under the new build seals the bare values once; a seal another account wrote reads as signed out (docs/adr/0177).
- The "Experimental" badge and the "not released yet, results are not reliable" note are gone from the settings page, all five READMEs and the marketing page; the free-text "Scan whitelist" field is gone too (`scanWhitelist` stays in the store for hover lookup only). PRODUCT.md's scan pipeline now describes the UIA/TrackCore reader that ships rather than the retired screenshot-fingerprint and Tesseract path.

- The auto-scan **stops drawing names it used to leak and simple inflections with them**。A word the page capitalizes mid-sentence now reads as a name（person，place）even when the page also spells it lower-case in a URL，a byline or a hashtag — the name set is also read from the page the scan actually tracks，so a page of frames no longer gates its words with another document's names。The word-form coverage that decides what the reader's level has mastered reaches past the old length guards（used beside use，nicer beside nice，oldest beside old），while the lemma a hand-picked word explains keeps the form the reader selected（docs/adr/0161）。
- A word the scan reads holds **one dashed line per page**：the first occurrence the track reads keeps the line and the rest of the page's occurrences go unmarked（UI-SPEC 4.20）。
- Verdicts are **keyed by the word's base form**. The pencil button, the bubble and the popup pills store "crumple" when the scan saw "crumpled", so one verdict covers every inflection both ways; the words popup and the exports show the base form, and marks written before this change re-key themselves once at startup, the old keys retiring with a sync tombstone (docs/adr/0162).
- Words carry **two reader verdicts** now. The known verdict means "seen, still learning": the auto-scan layer keeps drawing its line, restyled into a firmer ink dash, and the pet may still ask about the word. The new **mastered** verdict retires a word: no dashed line, no signposts, no review questions and no definition-chain dots; a hand-made selection still explains it either way. The verdict store holds one verdict per word (`known` on disk beside a new `mastered` key), syncs through the same newest-wins merge, and an older build reads a mastered word as known (docs/adr/0159, superseding docs/adr/0147's binary verdict and docs/adr/0158's file-as-source wordbook).
- The custom word list becomes a **one-time import**: the learning page's row is now "Mastered words" with an **Import a word list** button; the file's words are read once, lemmatised, de-duplicated and marked mastered, and the file is never watched afterwards, so a lost or edited one retires nothing. A path left over from an earlier build is imported once at startup and its key is dropped.
- The bubble's verdict row and the words popup's rows carry **three pills** (Known / Mastered / New), the popup gains a **Mastered** segment, and the plain, JSON and card exports follow it. A verdict made anywhere moves the scan's judgement and the pencil layer's lines at once, with no full re-scan. On the pencil layer, resting the pointer on a dashed word raises a single **Mastered** button over it; one click retires the word. The popup lists a marked word even when the history never saw it -- a word the pencil button marked takes a row with no time and a zero pop count, and the exports follow the same rows.
- The weekly report's AI observations now follow the **interface language**, not the explanation language, so they read in the same language as the panel that shows them.
- The bubble's **waiting card** is compact. The card a click raises now reserves room for the "Explaining…" line and three breathing bars instead of the whole answer's estimated height, so a word with every block switched on no longer opens as a tall, mostly empty card; the fields that fit land without moving the card, the rest grows it as they arrive, and the whole answer still settles to its own height once it lands (docs/adr/0146).
- Auto scan **reads each word's box once instead of twice**. The walk probed a word's box and then read the same rectangles again inside the word read, both across the process boundary; it now takes the box from the read itself. On the deterministic test arm that is about 29% fewer UI Automation round-trips to the first line and to a full page, with the same words and the same boxes. The scan also logs its real read timing now: a once-a-second step-cost line and the milliseconds to each page's first line (docs/adr/0188).
- Auto scan **checks the page without walking the whole tree**. The periodic check that the tracked page still belongs to its window now climbs from the page up to the window root instead of enumerating every document under it -- about 45x cheaper on a heavy page (measured ~180 ms down to ~4 ms a call). That frees roughly a third of the scan thread's time for reading words, so the lines crawl in faster; a tab switch or navigation still drops the page and searches again (docs/adr/0188).
- The **typing pace moved one notch faster all the way up**: the five speeds now write one character every 35, 25, 15, 10 and 5 ms, so what used to be the third level is the new slowest, and the default third level writes at 15 ms -- a little over twice the old pace (UI-SPEC.md 4.3).
- Switch toggles carry a little more weight now: **the knob stretches to about 1.4x through its travel and settles back as it lands**, all inside the one press duration, so a flip reads as one gesture instead of two properties moving in parallel (UI-SPEC.md 4.17).
- **The glass's stripes are gone in both directions**, and every quality tier now draws the same blur: thirteen taps a pass at a quarter of the prefilter texel, under a Gaussian sampled at half its sigma, so the kernel between taps is a curve rather than the straight chord that used to show the tap grid as fine stripes along both axes. The automatic and standard tiers come out about a quarter tighter at the same slider position than before, landing close to the blur the Smooth tier had; the Smooth tier reads as before, a shade softer (docs/adr/0201, superseding docs/adr/0111).

### Fixed
- **Pressing Test connection or Sync now on the Account & sync page no longer throws the page back to the top.** The WebDAV form used to step aside for the length of a round, collapsing the page under the reader so the scroll jumped to its start; it now stays in place, dimmed and inert, until the round ends (UI-SPEC.md 4.4).

- **Deleting the cloud copy on WebDAV reaches the server.** The sync round asked whether a GitHub-style token was stored before starting the delete, so a WebDAV account with its server, username and app password filled in returned silently and the envelope stayed on the server. The adapter now asks its own backend whether it is signed in, which is the provider-agnostic answer the round was meant to use.
- **Pressing a verdict pill no longer pulls a dragged bubble back to its anchor.** Every refresh of a standing card re-placed it from the selection -- the verdict pills, the arriving answer and the breakdown all share that path -- and the placement guards only knew about a drag still in progress, so the frame the reader had dragged the card to lasted no longer than the gesture. A drag now latches for the card's life: verdict presses and content updates keep the card where the reader put it, and a new selection still raises its card beside its own anchor.
- **The log and the crash dumps no longer land in the install folder.** An installed launch starts in the install directory, and the log directory was named relative to it, so a packaged build wrote `logs/` -- which can carry the words you looked up -- under Program Files, where it may be read-only and is shared by every account. An installed build now keeps the log and the dumps in `%APPDATA%\Lens\logs`; a development tree keeps the repository's `logs/`, so `tail -f logs/lens.log` and the QA runners read where they always have (docs/adr/0178).
- **Dragging a glass parameter no longer stalls the application.** The blur, saturation, refraction and rim sliders under either glass theme rebuilt the whole settings projection and rewrote the whole settings document on every stop, while every glass surface re-read it: about 590 ms per stop in a Debug build and 58 ms in an optimised one, measured offscreen. The six numbers now have a live projection of their own that only the glass reads, so a drag repaints the glass and nothing else, and the document reaches the disk once the gesture settles (docs/adr/0173).
- **A press on the blank room around the selection bar now puts it down.** The bar's window is larger than the card -- a shadow margin all round, and the side a drawer may open to -- and a press in that room counted as a press on the bar, so the bar stayed up. The bar now answers a press with its own shape: the body (for the tag, its card rather than the strip beside the string), and the drawer while it is out.
- The selection bar's **arrival no longer trips a QML binding loop**. With the tag form and a drawer out -- a sentence that offers a correction or the breakdown -- placing the bar flips the tag's string side while the window is reading the room the drawer needs, and the move's re-measure ran inside that read: Qt reported a binding loop on every selection. The re-measure now lands after the move, and the window keeps the same room for the drawer.
- The scan's dashed lines **stop at the word's letters**. UI Automation's word unit keeps the punctuation that follows -- the comma, the period, the dash -- and the line drew over it; the track now retracts the word's end by those characters before it measures the box.
- The auto-scan **stops locking onto a background tab's sliver**. When a tab switch or a navigation left the real page answering nothing for a beat, the page search took the only healthy document left: a parked tab's page, whose box collapses to a strip at the window's bottom edge. The track then drew that strip's words in its corner of the screen while the article went unwired, for as long as the tab stayed open -- restarting Lens was the only way out. A page now has to fill a quarter of the window's page area to be kept, both when it is first found and while it stands, and a page that shrinks to a sliver is dropped and searched for anew (docs/adr/0163).
- The words popup's **last row is no longer sliced**。The list's viewport cap was a 306 px number left over from an older row height and no longer a whole number of rows，so the row straddling the cap — the verdict rows the Mastered button appends at the list's end — lost its bottom few pixels。The cap now takes whole rows（nine），and UI-SPEC 4.8 states the cap in rows rather than pixels。
- The pencil layer's **Mastered button lands on the first click**。The scan used to tear its own layer down on every tick away from Chrome and on a single missed word read，taking the button down under the pointer — a click in that window fell through to the page。The scan now tears down once on leaving the page；a standing button rides out a lost word for about 400 ms（re-anchoring at once when the word returns）and freezes while it is held down，so a scan frame can neither move nor take the window a press lives on（docs/adr/0161）。
- The auto-scan's dashed lines **come back after a navigation**. Chrome rebuilds its page when a page loads, a tab is discarded or its renderer swaps, and the scan's page cache kept answering with the dead element, so the track read nothing and never searched for the page again until Lens restarted. A page that answers nothing is now dropped and searched for anew, fresh searches run at most a quarter second apart, and the track's once-a-second log line now covers the time spent acquiring, so a stop names its phase instead of going silent.
- The first-run wizard's step body scrolls inside its fixed 380×480 card. Longer translations, English and Spanish among them, no longer run under the footer; the header and buttons stay put, the wheel scrolls from anywhere on the card, and keyboard focus brings the control into view.
- The **DeepSeek flash alias is priced**. A reader who types `deepseek-v4-flash` -- the endpoint's alias for the flash model, and the name the field keeps -- saw every call cost 0, so the daily budget never moved; the price list now quotes the alias at the flash model's own rate.
- The **update check no longer calls a reachable release page unreachable**. The repository's only release is tagged `v1.0.0-alpha`, which the check refuses by design because it acts on plain three-number versions, and that refusal reached the settings page as "Cannot connect"; a pre-release tag now answers as up to date on a manual check and stays quiet on the automatic one.
- The pet's review sign's **Forgot/Remembered card follows the theme now**. It read the light table under both glass themes -- its tokens never turned the glass on -- so a frosted or liquid desktop held a daylight card. It reads the glass table and draws the glass like the bubble and the panels do, and the board itself stays pet art (UI-SPEC.md 4.13).
- Looking up a word **no longer takes the scan's lines down**. The scan paused whenever an explanation was in flight, so the bubble's own arrival -- and every Alt signpost prefetch -- cleared the whole page it was explaining, and the lines only came back on the next read. The scan reads the page through UIA on its own thread and shares nothing with the explanation channel, so only the daily budget holds it now (docs/adr/0187).

### Removed

- The **Custom** theme and its colour editor: the theme dropdown now offers the four built-in themes only. A settings document that still carries a packed `custom:` value falls back to the light theme until a theme is picked; nothing else about the theme setting changes.
- The selection bar's **fan** form: the style dropdown offers the four remaining forms only (docs/adr/0056). A settings document that still carries `fan` falls back to the classic bar until a style is picked; nothing else about the style setting changes.

### 中文

这是 Lens 的 beta 版本，也是 1.0.0-alpha 之后新版本号下的第二个版本。它比 alpha 稳定了许多，但仍处于 beta：部分界面仍显粗糙，结果也可能出错。遇到问题请到 https://github.com/kakuyo1/lens/issues 提交 issue，并附上复现步骤和 Lens 版本号。

#### 新增

- Lens 会保留一份本地**行为记录**，供日后的每周、年度与功能使用报告使用：`%APPDATA%\Lens\events\` 下每月一个纯 JSONL 文件，保留 12 个月。事件包含所涉单词的明文 lemma，以及新设置**记录阅读时使用的应用**打开时的前台进程名；不记录句子、释义、URL 或剪贴板文本，也没有第三方收到这些数据。主同步成功后，每个月的数据用同步口令派生的密钥加密，作为独立文件上传到同一个 gist 或 WebDAV 目录，不改动主同步文档。关于页会显示本地占用大小并可以清空，清空会把云端副本排入删除队列（docs/adr/0180）。
- Lens **崩溃**时留下的不再只有日志：进程内处理器会写出一个 minidump、一份文本摘要（版本、时间、异常码、出错地址、模块）和一个标记，放进日志目录的 `dumps` 文件夹，只保留最新的五份。处理器随即启动 **lens_reporter.exe**，一个以界面语言显示的小型原生对话框，说明发生了什么，并提供两个动作——**打开转储文件夹**和**报告问题**；后者打开一个预填了摘要事实的 GitHub issue。转储本身只在你主动附上时才传输，没有任何内容会被自动上传（docs/adr/0174、docs/adr/0175）。下次启动时，一张通知卡片会复述这条消息，并带上同样的两个按钮。
- 桌面宠物有时会把散步变成一次**任务栏旅行**：屏幕有固定的底部任务栏（自动隐藏关闭）时，它会沿一小段弧线朝屏幕更宽敞的一侧跃去，在任务栏上沿弹一下，停一停，以缓慢平稳的步伐走到那一侧接近尽头再走回弹跳处，然后跳回出发的位置继续走路循环。旅行结束前，其他宠物行为都会暂停；拖动或点击可以提前结束。每只宠物的概率取可选的 `behavior.taskbarChance`，默认 0.005（docs/adr/0155）。
- 统计弹窗新增**本周画像**面板：从周一到今天的本周查词，带新词与重复的占比、词性分布、按小时的查词量和查得最多的五个词。这些计数来自本地历史，不产生费用。**解读本周**是唯一一次请求：它发送这些计数的摘要和那五个词（不含句子、释义或时间），按普通请求计费。回答在整屏的**周报卡片**中打开，同一个按钮再次打开该卡片时不再发请求（docs/adr/0150，卡片见 docs/adr/0151）。
- **本周画像**面板多了一行行为数据：本周单词释义里非英语解释的占比，以及完成了几次母语偷看。两者都在设备上根据历史与每日统计得出，都不进入解读请求。把解释语言切离英语时，该设置下会出现一行提示：按住地球偷看只在英文解释下存在；切回英语后提示消失。
- 每周的**周报卡片**显示本周的数据、这些数据落在哪些小时，以及五条观察。每条观察都带一个取自设备计数的数字，模型不写任何数字（docs/adr/0151）。卡片上的**小故事**按钮用本周查过的词（最多十五个，由设备挑选）写一则简短的英文小故事；故事里用到的词在文本中带下划线。只有这些词离开本机，故事只在当周保存在内存中（docs/adr/0152）。
- 词表导出的保存对话框提供两种学习卡格式：**带释义的 CSV 文件**和**带释义的 Anki TSV 文件**。每行是词表中的一个词，带缓存的 IPA、词性、英文与中文释义和例句。从未取过释义的词也保留一行，这些列为空，导出不会发起请求。CSV 带表头和 UTF-8 字节序标记，表格软件能正确读出中文；Anki 文件以 `#separator:tab` 开头、没有表头，供 Anki 的文本导入使用。
- 自动扫描从“采集与弹窗”中移出，成为目录里单独的一行：一个总开关、一段短说明和一个“支持的软件”分组。Chrome 和 Notepad++ 各有一个开关（`autoScanChrome`、`autoScanNotepad`，均默认开启）；PDF 阅读器和视频字幕是标着“待上线”的置灰行。只有总开关与前台软件的开关同时打开时，扫描才会运行。
- 自动扫描也能读 **Notepad++ 文档**，不只 Chrome 页面。读法由窗口决定：Scintilla 编辑器通过它自己的消息读取（文本用 `SCI_GETTEXTRANGEFULL`，每个词的框用 `SCI_POSITIONFROMPOINT` 与 `SCI_POINTX/YFROMPOSITION`），其他窗口走 UI Automation。词的切分在本地按文档的 UTF-8 字节完成，改动文本的编辑会丢弃该页并重新扫描。不使用 `SCI_GETTEXTRANGE`（2162）：Scintilla 5 已为超过 2 GiB 的文档移除了它，发送它会令编辑器停止响应。
- 自动扫描**对非英语页整页不画线**。读名字集的那份页面文本现在也判页面语言：CJK 字符占多数、或字母词足够多但英语功能词占比不足时，整页虚线收起，直到页面重新读作英语；短到判不了的页面保持原样。英语散文和英语技术页都保留虚线（docs/adr/0187）。

#### 变更

- 设置文档不再以明文保存同步凭据。GitHub token 及其 refresh token、以及 WebDAV 应用密码，在写入前都会用 DPAPI 密封到读者的 Windows 账户，读取时再解开。旧版本写下的文档在下一次写入密封前仍按原样读取；新版本首次启动时会把裸值密封一次；由另一个账户写入的密封值读出来等同于未登录（docs/adr/0177）。
- 设置页、五份 README 和宣传页上的“实验性”徽章与“尚未发布，结果不可靠”的提示都已移除；自由文本的“扫描白名单”字段也一并移除（`scanWhitelist` 仍留在存储中，只供悬停查询使用）。PRODUCT.md 的扫描管线现在描述实际发布的 UIA/TrackCore 读取器，而不再是已退役的截图指纹与 Tesseract 路径。
- 自动扫描**不再画出过去会漏出的专名，依赖它们的简单屈折形式也不再被放过**。页面上在句中被大写的词现在按专名（人名、地名）处理，即使页面在 URL、署名或话题标签里也用了小写；专名集合也从扫描真正跟踪的页面读取，因此一个由框架组成的页面不会再拿另一个文档的专名来筛自己的词。用来判断读者所在水平已掌握哪些词形的词形覆盖越过了旧的长度门槛（used 挨着 use，nicer 挨着 nice，oldest 挨着 old），而手动选中的词取释义时仍保留读者选中的那个词形（docs/adr/0161）。
- 扫描读到的一个词**每页只保留一条虚线**：跟踪读到的第一次出现保留虚线，该页其余出现不再标注（UI-SPEC 4.20）。
- 判定**按词的原形归档**。扫描看到“crumpled”时，铅笔按钮、气泡和弹窗里的按钮存的是“crumple”，一条判定同时覆盖两个方向的每种屈折；词表弹窗和导出显示原形，本次改动之前写下的标记会在启动时重新归档一次，旧键通过一条同步墓碑退役（docs/adr/0162）。
- 单词现在带**两种读者判定**。“已会”表示“见过，还在学”：自动扫描层继续画它的虚线，只是改成更实的墨色短划，宠物仍可能来问这个词。新的**掌握**判定会让一个词退役：没有虚线，没有路标，没有复习提问，也没有释义链的圆点；手动选中仍然会照常解释它。判定存储每个词保留一条判定（磁盘上的 `known` 旁边新增 `mastered` 键），通过同样的最新优先合并同步，旧版本会把“掌握”的词读成“已会”（docs/adr/0159，取代 docs/adr/0147 的二元判定和 docs/adr/0158 的文件即词库方案）。
- 自定义词表变成**一次性导入**：学习页的那一行现在叫“已掌握词”，带一个**导入词表**按钮；文件里的词读取一次，经词根还原、去重后标记为掌握，此后不再监视该文件，文件丢失或被编辑都不会让词退役。旧版本遗留的路径会在启动时导入一次，随后删除该键。
- 气泡的判定行和词表弹窗的行都带**三个按钮**（已会 / 掌握 / 新词），弹窗新增**掌握**分段，纯文本、JSON 和卡片导出也随之跟进。在任何地方做出判定，都会立即移动扫描的判断和铅笔层的线，不需要完整重扫。在铅笔层上，把指针停在一个带虚线的词上会在它上方浮出一个**掌握**按钮；一次点击让该词退役。历史从未见过的词也会出现在弹窗里——铅笔按钮标记过的词占一行，没有时间、查词次数为零，导出遵循同样的行。
- 周报的 AI 观察现在跟随**界面语言**，而不是解释语言，因此与展示它的面板使用同一种语言。
- 气泡的**等待卡片**变紧凑了。点击抬起卡片时，只给“正在解释…”这一行和三条呼吸条留出空间，不再预留整个回答的估计高度，因此所有区块都打开的单词也不会再展开成一张又高又空的大卡片；能放下的字段原地落位，放不下的随到达而把卡片撑高，整个回答落地后仍会收回到自己的高度（docs/adr/0146）。
- 自动扫描**每个词的框只读一次，不再读两次**。走词原先先探一次词框、读词时内部又读同一组跨进程矩形；现在直接取读词返回的框。确定性测试臂上，到首线、到读完整页的 UI Automation 往返各少约 29%，词与框都不变。扫描现在也记录真实读取耗时：每秒一条步耗时行，以及每页首线出现的毫秒数（docs/adr/0188）。
- 自动扫描**检查页面不再整棵走树**。判断跟踪页是否仍属于其窗口的周期检查，现在从页元素向上爬到窗口根，而不是枚举其下所有文档——重页上约 45 倍便宜（实测每次 ~180 ms 降到 ~4 ms）。这腾出约三分之一的扫描线程时间用于读词，虚线爬升更快；切标签或导航仍会丢页并重新搜索（docs/adr/0188）。
- **打字速度整体加快一档**：五档现在每字 35 / 25 / 15 / 10 / 5 ms，原来的第 3 档成了新的第 1 档，默认的第 3 档因此以每字 15 ms 写出，约为过去的两倍快（UI-SPEC.md 4.3）。
- 开关的切换更有分量：**滑块在平移途中横向拉伸到约 1.4×，落定前收平**，全部在同一个 press 时长内完成，翻转读成一次带分量的动作，而不是两条并行的属性动画（UI-SPEC.md 4.17）。
- **玻璃的条纹在两个方向都消失了**，所有画质档现在画同一套模糊：每遍 13 个抽样点、点距为预滤波 mip texel 的 ¼，权重是"在 σ 的一半处采样"的高斯——点与点之间是曲线而不是直线弦，原先沿两轴显出的细条纹因此不再出现。同一滑杆位下，自动与标准档比过去紧约四分之一，接近流畅档原来的柔度；流畅档观感与之前相当、略柔一点（docs/adr/0201，取代 docs/adr/0111）。

#### 修复
- **账号与同步页按 “Test connection” 或 “Sync now” 不再把整页弹回顶部。** WebDAV 表单过去会在同步进行时整块退场，页面在读者脚下塌陷、滚动位置被夹回顶部；现在它留在原位，灰化且不可编辑，直到这一轮结束（UI-SPEC.md 4.4）。

- **WebDAV 上删除云端副本现在能到达服务器。** 同步回合在开始删除前先问是否保存了 GitHub 风格的 token，于是填好服务器、用户名和应用密码的 WebDAV 账户会静默返回，加密的同步副本仍留在服务器上。适配器现在直接问自己的后端是否已登录，这正是该回合本来要用的与提供方无关的答案。
- **按下判定按钮不再把拖过的气泡拉回锚点。** 站立卡片的每次刷新都会按选区重新摆位——判定按钮、到达的回答和拆解共用这条路径——而摆位的守卫只知道仍在进行中的拖动，于是读者把卡片拖到的位置连一个手势都撑不过。拖动现在会锁定到卡片结束：判定按钮和内容更新都会让卡片留在读者放置的位置，新的选区仍会在它自己的锚点旁抬起卡片。
- **日志与崩溃转储不再落进安装目录。** 安装版启动时的当前目录是安装目录，而日志目录按相对路径命名，于是打包版本会把可能包含你查过的词的 `logs/` 写到 Program Files 下，那里可能只读，而且由所有账户共享。安装版现在把日志与转储放在 `%APPDATA%\Lens\logs`；开发树仍用仓库的 `logs/`，所以 `tail -f logs/lens.log` 和 QA 运行器照旧读取（docs/adr/0178）。
- **拖动玻璃参数不再卡住应用。** 两种玻璃主题下的模糊、饱和度、折射和边缘滑块每停一下都会重建整个设置投影、重写整个设置文档，而每个玻璃表面都会重读它：离屏实测在 Debug 构建中每停约 590 ms，在优化构建中 58 ms。这六个数现在有只供玻璃读取的实时投影，拖动只重绘玻璃，不碰其他东西，手势停下后文档才落盘（docs/adr/0173）。
- **按在选区条周围的空白区域现在会把它放下。** 选区条的窗口比卡片大——四周是阴影边距，还有抽屉可能打开的一侧——按在这片区域会被算作按在条上，于是条一直立着。选区条现在按自身形状来回应按下：主体（对标签形式来说是它的卡片，而不是字符串旁边的条），以及打开时的抽屉。
- 选区条的**出现不再触发 QML 绑定循环**。在标签形式且抽屉打开时——提供纠错或拆解的句子——摆放选区条会翻转标签的字符串侧，而窗口正在读取抽屉所需的空间，移动的重新测量又在这条读取中运行：Qt 在每次选区时都报告绑定循环。重新测量现在落在移动之后，窗口为抽屉保留的空间不变。
- 扫描的虚线**停在单词的字母处**。UI Automation 的单词单位会带上后面跟着的标点——逗号、句号、破折号——虚线盖过了它们；轨道现在测量前先按这些字符收缩词的末端。
- 自动扫描**不再锁住后台标签页的窄条**。标签切换或导航让真正的页面一时什么都答不上来时，页面搜索会取走唯一还健康的文档：一个被停放的标签页，它的框塌成窗口底部的一条窄条。轨道随即在屏幕那个角落画这条窄条里的词，而文章一直没有接上，只要标签页还开着就只能重启 Lens。现在一个页面必须占满窗口页面区域的四分之一才会被保留，初次找到时和持续跟踪时都如此；缩成窄条的页面会被丢弃并重新搜索（docs/adr/0163）。
- 词表弹窗的**最后一行不再被切掉**。列表视口的上限是 306 px，来自更早的行高，已不再是整行数，于是跨在上限上的那一行——掌握按钮追加在列表末尾的判定行——底部几个像素被切掉。上限现在按整行计算（九行），UI-SPEC 4.8 也以行而不是像素说明这个上限。
- 铅笔层的**掌握按钮第一次点击就能落下**。扫描过去每离开 Chrome 一个 tick、以及单词读数单次失败时都会拆掉自己的层，把按钮从指针下撤走——这个窗口里的点击会落到页面上。扫描现在只在离开页面时拆一次；立着的按钮能扛过一次约 400 ms 的读词失败（词回来时立即重新锚定），被按住时冻结，因此扫描帧既不能移动它，也不能取走按下所在的窗口（docs/adr/0161）。
- 自动扫描的虚线**在导航后回来了**。Chrome 在页面加载、标签页被丢弃或渲染进程交换时会重建页面，而扫描的页面缓存一直用已经死掉的元素作答，轨道什么也读不到，也不再重新搜索页面，直到 Lens 重启。现在答不上来的页面会被丢弃并重新搜索，新的搜索最多每四分之一秒一次，轨道每秒一条的日志行现在覆盖获取所花的时间，停止时会说出所处阶段，而不是沉默。
- 首次引导向导的步骤正文在固定的 380×480 卡片内滚动。更长的译文——英语和西班牙语都在其中——不再钻到页脚下面；标题和按钮保持不动，滚轮从卡片任何位置都能滚动，键盘焦点会把控件带入视野。
- **DeepSeek flash 别名有价格了。** 输入 `deepseek-v4-flash` 的读者——这是端点给 flash 模型的别名，也是字段保留的名字——过去每次调用都计 0 费，每日预算从不动；价格表现在按 flash 模型自身的费率报这个别名。
- **更新检查不再把可访问的发布页说成无法连接。** 仓库唯一的发布标签是 `v1.0.0-alpha`，检查按设计拒绝它，因为它只处理纯三段数字版本，而这个拒绝曾以“无法连接”到达设置页；现在手动检查遇到预发布标签会回答已是最新，自动检查则保持沉默。
- 宠物复习牌的**忘了/记得卡片现在跟随主题**。它在两种玻璃主题下都读浅色表——它的 token 从未打开玻璃——于是雾面或流光桌面上立着一张日光卡片。它现在读玻璃表并像气泡和面板那样绘制玻璃，牌子本身仍是宠物美术（UI-SPEC.md 4.13）。
- 查一个词**不再把扫描的虚线收走**。过去只要有解释在途，扫描就暂停，于是气泡自己的出现——以及每次 Alt 路标预取——把正在解释的整页线都清掉，要等下一次读取才回来。扫描走 UIA、在自己的线程上读页，与解释通道没有共享资源，现在只有每日预算能暂停它（docs/adr/0187）。

#### 移除

- **自定义**主题及其颜色编辑器：主题下拉现在只提供四个内置主题。仍带打包 `custom:` 值的设置文档在选到主题前回退到晴朗白昼；主题设置的其他行为不变。
- 选区条的**扇**形：样式下拉只提供剩下的四种形式（docs/adr/0056）。仍带 `fan` 的设置文档在选到样式前回退到经典长条；样式设置的其他行为不变。

## 1.0.0-alpha - 2026-11-05

This is an early alpha of Lens, and the version numbering starts over at 1.0.0-alpha. It may have many problems, including crashes, wrong results and rough edges on every surface. Please report what you find as an issue at https://github.com/kakuyo1/lens/issues, with the steps that led to it and your Lens version.

这是 Lens 的早期 alpha 初版，版本号从 1.0.0-alpha 重新开始。可能存在很多问题，包括崩溃、结果错误，以及各个界面上的粗糙之处。遇到问题请到 https://github.com/kakuyo1/lens/issues 提交 issue，并附上复现步骤和 Lens 版本号。
