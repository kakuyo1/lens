# 0120 一轮同步由一个同步轮 module 拥有，传输从端口注入

- **状态**：accepted（2026-10-26）
- **背景**：ADR 0057 把两个判定抽成纯函数，但一轮同步的顺序与状态机仍在 `SyncDuty` 的十个处理函数里：401 → 刷新 → 从 listing 续跑、退避重试、同步中改动后的重跑、apply 守卫、口令只活一轮、指纹只在合并落地后取；`sync` 分区的约二十处键也在 duty 里各自拼写。`GistClient` 与 `GithubAuth` 以 nullptr 网络管理器构造，测试插不进 fake，整轮只能靠真实网络与事件循环才跑得出来。读码还发现一条与 `sync_duty.h` 的 “口令在运行结束时清掉” 相悖的路径：中途刷新令牌失败时，auth 的失败处理只把 phase 复位、报告错误，不调 `finish()` 或 `endFailedRun()`——口令留在内存，运行期间请求的下一次同步被丢掉，自动轮也不会重试。
- **决定**：新增 `src/core/domain/sync_round.{h,cpp}`，一个无 Qt、无定时器、无窗口的 `SyncRound`，拥有整轮。
  - 事件进：`begin(Start)` 收口令与是否读者所问；`removeCloud()` 收支删除；`requestRun()` 收一次运行中到来的请求；`forget()` 收登出。`Transport` port 的回复进，每一步要么发出下一个 `Request`，要么把合并结果写进 stores 与分区，要么结束。
  - 值出：`Request` / `Answer` 只带普通值；`Hooks` 把变化、错误码、结束、重试延迟、重跑与已应用告知 adapter。计时器仍归 `SyncDuty`，它只把 `retry(ms)` 装进自己的计时器。
  - `SyncPartition`（`core/domain/sync_partition`）是 `sync` 分区的类型化视图：token、refreshToken、login、deviceId、gistId、key / salt / iterations、settingsSnapshot、lastSyncedAt 的拼写只在一个文件里；deviceId 的铸造可注入，测试拿到稳定的 id。
  - `SyncTransport`（`app/duties/sync_transport`）实现 port：把 `Request` 映射到 `GistClient` / `GithubAuth`，把 Qt 信号折回 `Answer`。同一时刻只放一个请求；登出后被放弃的回复按生成号丢弃，遗留在队列里的请求按顺序等，不会张冠李戴。
  - 登录流程留在 `SyncDuty`：它不是一轮同步，只多了一个 `Naming` 阶段读登录名；auth 的失败处理只在 `CodePending` 时生效，刷新失败的路径归 module。登录读名失败只报告并结束登录，不再借刷新续跑一次同步；读完名字后由指纹决定要不要补一次同步。
  - 云端格式不变（0042）：`lens-sync` 描述、`lens-sync.json` 文件名、信封与合并规则都不动。0057 的 `openCloudCopy` / `sealUpload` / `shouldRenewSignIn` 成为 module 的内部零件。
- **后果**：
  - `sync_round_test` 10 个值用例，无网络、无事件循环：先存密钥再读错误、apply 后取指纹、401 续签一次并续跑、刷新失败结束本轮并尊重运行中的改动、退避 30 s → 1 min → 2 min 且成功清零、手动轮不重试、安静轮不上传、删除后忘记身份且留着登录、遗留回复被忽略、无钥匙要求口令。
  - `sync_transport_test` 3 个用例把真实的 `GistClient` / `GithubAuth` 驱动在剧本化的网络管理器上（复用 update 测试抽出的 `ScriptedManager`）：列表 → 读 → 写的 URL 与文件内容，401 走 token endpoint 续签，刷新被拒时运行结束且没有下一次请求。`sync_duty_test` 1 个用例把 adapter 的接线走一遍：`syncNow()` 起轮、状态读到 syncing、结束后回到 signedIn、时间戳写入、`remoteApplied` 只发一次。
  - 读码发现的缺陷随之修复：刷新失败现在走 `fail()`，口令随本轮清掉、运行结束、按手动 / 自动决定是否退避，运行中到达的改动仍排下一次。删除失败按 “读者所问” 报告，不再沿用上一次运行的 manual 标记去重试一次普通同步。
  - `SyncDuty` 由 829 行降到 436 行：十个处理函数与 `sync` 分区键的拼写消失；对 QML 的口径不变（七个属性与六个 `Q_INVOKABLE` 不动）。
  - 被排除的方案：把编排留在 duty、只补测试；把 Qt 或计时器放进 module；只在 port 层做 fake 而不测真实客户端（两者都要，端口让值测试成立，剧本化管理器让 URL 与状态映射成立）。
- **ADR 关系**：0057 的两个判定、0042 的云端格式、0013 的 duty 边界；0114 / 0116 / 0117 / 0118 的 “纯 module + adapter” 在同步这里再落一处。
