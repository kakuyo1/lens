# 覆盖率薄弱目录提升计划（2026-11-15）

## 1、从哪里出发

读数出自 Coverage workflow 的合并报告（CI run 43645604218，`ci/tidy-and-asan` 合并前的 HEAD，即现在的
main；五个目标，含 pet），原始数据为该 run 的 `coverage-cpp` artifact。门禁（总体 75%、新增行 85%）已在
同一 run 验证通过，本计划的所有读数都在门禁口径内：与 [`test-coverage-baseline-2026-10-30.md`](test-coverage-baseline-2026-10-30.md)
相比，分母变了（pet 接入、代码增长），不要跨报告比百分比。

| 目录 | 覆盖行 | 可执行行 | 行覆盖率 |
| --- | ---: | ---: | ---: |
| `src/app` | 8,662 | 12,557 | 69.0% |
| `src/capture` | 1,270 | 1,597 | 79.5% |
| `src/core` | 3,674 | 3,861 | 95.2% |
| `src/llm` | 1,297 | 1,552 | 83.6% |
| `src/ocr` | 921 | 1,032 | 89.2% |
| `src/pictures` | 322 | 425 | 75.8% |
| **`src/speech`** | **262** | **431** | **60.8%** |
| **`src/sync`** | **395** | **598** | **66.1%** |
| `src/update` | 346 | 383 | 90.3% |
| `src/util` | 296 | 347 | 85.3% |
| 合计 | 17,445 | 22,783 | 76.6% |

最弱的两块是 **speech（60.8%）**与 **sync（66.1%）**。它们的低读数不是真机路径造成的（§2 的豁免清单
不包含这两个目录的任何一个文件），而是整块的网络客户端没有测试：`VoiceDownloader`、`GistClient`、
`GithubAuth` 的流程层。这是本轮的主目标。app（69.0%）里可补的部分列在 §6，作为第二梯队。

## 2、读数前先认两条

1. **本地扫描不是口径。**本机跑一遍 `coverage.sh` 得到的读数比 CI 低约 5 个点（app 本地 59.5% vs CI
   69.0%）——surfaces 的两片采集只有 CI 是完整的。逐文件分析一律用 CI 的 `coverage-cpp` artifact，
   不要用 `test/records/coverage/` 里的本地文件。
2. **app 的低读数里有一整块真机豁免。**下面这些文件的主要入口在 `lens_gtest_integration` 或真实桌面
   上，无头目标到不了，不算缺口（合计约 1,430 行）：

   | 文件 | 缺行 | | 文件 | 缺行 |
   | --- | ---: | --- | ---: |
   | `app/platform/page_reader_uia.cpp` | 272 | | `app/platform/hover_word_lookup.cpp` | 121 |
   | `app/glass/glass_capture.cpp` | 237 | | `app/platform/mouse_selection_hook.cpp` | 81 |
   | `app/platform/selection_text_grabber.cpp` | 215 | | `app/platform/local_voice.cpp` | 68 |
   | `app/glass/glass_duplication.cpp` | 214 | | `app/platform/alt_key_hook.cpp` | 50 |
   | `app/platform/page_reader_scintilla.cpp` | 193 | | `app/platform/global_hotkey.cpp` | 49 |
   | `app/glass/glass_backdrop.cpp` | 153 | | `app/platform/autostart.cpp` | 34 |
   | `app/glass/glass_capability.cpp` | 40 | | `app/platform/uia_text_range_read.h` | 29 |
   | `app/glass/glass_desktop.cpp` | 27 | | | |

## 3、接缝：动工前先备齐

现有测试已经有完整的网络桩 [`test/googletest/unit/scripted_manager.h`](../../../test/googletest/unit/scripted_manager.h)
（`ScriptedManager`/`ScriptedReply`：队列化应答、自定义动词、abort 即取消、无应答桩、请求记录），webdav
与 transport 的测试都在用它。缺口只有三处：

| 接缝 | 是什么 | 改动 |
| --- | --- | --- |
| `ScriptedReply` 的 `Link` 头 | `GistClient::listPage()` 从应答头读 `rel="next"` 分页；桩现在只能设 `Location` | `queue()`/构造多收一个 `QByteArray link`，`setRawHeader("Link", …)`，约 6 行 |
| `ScriptedReply` 发 `downloadProgress` | youdao/commons 的超限中断挂在 `downloadProgress` 上；桩现在只发 `readyRead`+`finished` | `deliver()` 里补一条 `emit downloadProgress(…)`，约 3 行；不做则对应分支留残 |
| `VoiceDownloader` 的两个注入缝 | manager 与规格目录都是自取的（`new QNetworkAccessManager`、`voiceSpec(model)`），测试既喂不了假应答也造不出能过 `verifyFile()` 校验的文件 | 构造函数加两个带默认值的重载参数：`QNetworkAccessManager*` 与 `std::function<VoiceSpec(VoiceModel)>`（默认取真目录）。与 `GithubAuth`/`GistClient`/`YoudaoSource` 的注入方式一致 |

顺手项：`github_auth_test.cpp` 文件内的 `CannedReply`/`CannedManager` 与 `scripted_manager.h` 重复，
迁移后删除本地副本。

## 4、speech：60.8% → 目标 ≥ 90%

全部进 `lens_gtest_unit`，全部离线（`ScriptedManager` 喂应答，`QTemporaryDir` 当盘）。

| 文件 | 现状 | 用例 | 预计恢复 |
| --- | --- | --- | ---: |
| `speech/voice_downloader.cpp` | 10/89 = 11.2%，除构造外全空 | 新建 `voice_downloader_test.cpp`。测试规格里放两个小文件，sha256 用 `QCryptographicHash` 自算（`verifyFile` 校验的就是它）。覆盖：整链成功（`.part` 写入 → 校验 → 改名 → `finished(true)`、`progressed` 报数）；body 尺寸超限中断；HTTP 错误 → `finished(false)` 且不留 `.part`；校验失败同上；改名失败；`cancel()` 走 abort；进行中第二次 `start()` 被忽略；目录建不出 → 未发请求即 `finished(false)` | ~75 |
| `speech/youdao_source.cpp` | 5/35 = 14.3% | 新建 `youdao_source_test.cpp`：`urlFor` 的编码与 `type` 参数（含带空格/撇号的词）；200 mp3 → `sink(bytes, true)`；500 带 120 字节 body（词典条目拒绝的真实形状）→ `sink({}, false)`；空 body、网络错误同拒绝；重定向策略为 Manual（读桩记录的 `redirectPolicy`） | ~28 |
| `speech/google_source.cpp` | 15/38 = 39.5% | 现有 `google_source_test.cpp` 补 `fetch()` 的错误路径（非 200、空 body、超限），形状与 youdao 相同 | ~20 |
| `speech/audio_cache.cpp` | 84/114 = 73.7% | 新建 `audio_cache_test.cpp`：store/load 往返；非法 key（长度、非 hex）与空 root 拒绝；空 bytes 不落盘；0 字节文件 load 即删；命中会摸新 last_write（`prune` 不逐它）；prune 驱逐最旧——给构造函数加 `maxBytes` 默认参数缝，别在测试里写 60 MB；`bytesOnDisk()`/`clear()` | ~28 |
| `speech/online_sources.cpp` | 6/11 = 54.5% | 随 youdao/google 用例把 `pick()` 的分支走全 | ~5 |

残留：`audio_cache.cpp` 62–64（打开失败即删）在 Windows 上要构造独占锁，不值得；youdao/commons 的
`downloadProgress` 超限两行，视 §3 的桩扩展做不做。

## 5、sync：66.1% → 目标 ≥ 90%

全部进 `lens_gtest_unit`。

| 文件 | 现状 | 用例 | 预计恢复 |
| --- | --- | --- | ---: |
| `sync/gist_client.cpp` | 145/214 = 67.8%，客户端层一条没测（只测了纯解析器） | 新建 `gist_client_test.cpp`，按方法逐个过：`getGist` 成功 / 401→Unauthorised / 404→NotFound / 无应答→Proxy / 坏体→Generic；truncated 文件 → raw 补取（队列两答）→ 内容回填、truncated 清零；raw 地址不在白名单 → 拒绝；`createGist` 201→created（断言 POST 体）/ 非 201→failed；`updateGist`（PATCH 动词）/ `deleteGist`（204、404 都算 deleted；500→failed）；`getUser`（login 空→failed）；`listGists` 带 `Link` 头翻页（连翻两页、页帽 5、next 不在白名单即停）、非数组体→failed；应答超 10 MB 上限被丢弃 | ~60 |
| `sync/github_auth.cpp` | 115/219 = 52.5%，只有 refresh 一半有流程测试 | `github_auth_test.cpp` 追加流程段（改用 `scripted_manager.h`）：`requestCode` 成功→`codeReady` 且轮询计时器已起（断言 POST 体带 client_id+scope）；非 200→Generic；无应答→Proxy；空 clientId 即败；坏体→Generic。`poll()`：pending→再轮询→granted→`granted(token)` 且计时器停；slow_down→发出第二次轮询；expired→`failed(Expired)`；denied→`failed(Denied)`；未知→Generic；无应答→Proxy。过期：规格给 `expires_in:1`，~1.2 s 后 `failed(Expired)`。`cancel()`：无应答桩期间取消（断言不再有回调）。`refresh()`：空参即败（callId 非 0）；无应答→Proxy；未知状态→Generic | ~70 |
| `sync/webdav_client.cpp` | 115/138 = 83.3%，失败分支全空 | 现有 `webdav_client_test.cpp` 补：四个方法对非 http(s) 地址一律 `failed(Generic)`；`propfind` 404/409→`found(false)`、401→Unauthorised；`get` 409→NotFound；`put` 的 409→MKCOL→重试矩阵（MKCOL 201/405→重试成功、MKCOL 500→failed、重试仍 409→failed）；`del` 500→failed；`addressFor` 规范化（无 scheme 补 https、尾斜杠、空 server）；应答超上限被丢弃 | ~20 |

残留：slow_down 的 +5 s 精确间隔不断言（用 1 s interval 观察第二次轮询发出即可，精确间隔留残）。

## 6、第二梯队：app/duties 与 pictures

| 文件 | 现状 | 用例 | 预计恢复 |
| --- | --- | --- | ---: |
| `app/duties/github_backend.cpp` | 40/115 = 34.8% | 新建 `github_backend_test.cpp`：登录状态机全链——未配置不动作；`startLogin`→Pending；`codeReady`→`changed` + userCode/verificationUri 暴露；granted→token 入 partition、转 Finishing、发 `getUser`；`gotUser`→`completed` + login 落盘；`failed` 的文案映射（BadRefresh→signInExpired 且 `forgetTokens`）；getUser 失败的映射（Unauthorised→signInExpired+forget、NotFound→noGist）；`cancelLogin`/`abortSignIn` 归 Idle | ~65 |
| `app/duties/speech_duty.cpp` | 114/285 = 40.0% | 新建 `speech_duty_test.cpp`（`sync_duty_test` 的方式构造 duty）：`playOnline` 缓存命中不发请求、未命中走 source→store→`cacheBytesChanged`；过期请求号被丢弃；非法 headword 不动作；plan 无路可走不动作；`voiceStates()` 三态与下载百分比（配合 §3 的 downloader 缝，`downloadVoice`→ScriptedManager→`voicesChanged`）。**系统/本地引擎链不追**：`engineFor`/`speakSystem`/`speakLocal`/`ensurePlayer` 依赖 QTextToSpeech 的机器声库与 QMediaPlayer 的解码器，无头 CI 上要么不可用要么不确定，属 integration/人工 | ~80 |
| `app/duties/webdav_transport.cpp` | 46/67 = 68.7% | 现有 transport 测试补失败分支 | ~15 |
| `app/duties/sync_transport.cpp` | 73/94 = 77.7% | 同上 | ~15 |
| `pictures/picture_sources/commons_source.cpp` | 5/71 = 7.0% | 新建 `commons_source_test.cpp`：`searchUrl` 参数；空查询→`sink({}, false)`；HTTP 错→同；成功→`parseCommonsSearch` 结果透传；`fetch` 对非 upload 主机拒绝、200→bytes、错误/空→false | ~60 |

## 7、明确不追的

- §2 的真机豁免清单（约 1,430 行）：`lens_gtest_integration` 与人工的领地，本计划不动。
- `ocr/qt_http_transport.cpp`（56 行，0%）：ocr 目录整体 89.2%，这 56 行留给将来的 OCR 传输专项。
- `util/profile.cpp`：`LENS_ENABLE_PROFILE` 关闭时的构建配置，非缺口（基线文档已注明）。
- QML 侧：settings 各页偏低是上一轮已知事项，不与本计划混做。

## 8、执行顺序与验证

每阶段收尾都是同一套：`scripts/build/build.bat --target lens_gtest_unit` 窄跑新用例 →
`coverage.sh` 对单目标采一份读数 → 提交。新测试文件登记进 `test/googletest/CMakeLists.txt` 的
`lens_gtest_unit`；§3 的接缝全部保持默认行为不变（现构造签名不动，只加重载/默认参数），tidy 与
ASan 门禁会照常审过新代码。

1. **阶段 A（speech）**：§3 接缝 + §4 全部 → speech ≥ 90%。
2. **阶段 B（sync）**：§5 全部 → sync ≥ 90%。
3. **阶段 C（第二梯队）**：§6 全部 → app ≈ 73%，pictures ≈ 88%。

每阶段推送后看 Coverage workflow 的 gate（75%/85% 不动，只会更绿）。三个阶段全部落地后再讨论是否把
总体基线从 75% 抬到 78%——抬门禁是另一个决定，不搭这趟车。

## 9、预期总账

| 目录 | 现在 | 之后（保守/理想） |
| --- | ---: | ---: |
| `src/speech` | 60.8% | ≥ 90% / ~97% |
| `src/sync` | 66.1% | ≥ 90% / ~97% |
| `src/app` | 69.0% | ~72% / ~73% |
| `src/pictures` | 75.8% | ~85% / ~88% |
| **合计** | **76.6%** | **~78.4% / ~79.1%** |

算术：17,445 + 约 570 恢复行 ≈ 18,015/22,783 ≈ 79%。三个阶段预计各一次工作会话。
