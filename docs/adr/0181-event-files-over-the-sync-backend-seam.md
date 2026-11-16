# 0181 事件文件走同步后端的接缝

- **状态**：accepted（2026-11-16）
- **背景**：`0180` 的事件通道自己建了 `GistClient` 与 `WebDavClient`，自带 provider 分支、
  三份凭据快照与九个按 call id 过滤的应答回调，把 `0156`、`0157` 已经立好的
  `SyncBackend` / `Transport` 接缝又实现了一遍。加第三个 provider 要改两处，两份实现
  会各自漂移；事件通道的测试也只能靠脚本化的 HTTP 应答，够不到主同步那套已经跑通的
  适配器。
- **决定**：
  - 在 `core/domain/file_transport.h` 定一个与 `Transport` 并列的接口 `FileTransport`：
    一次一个文件级请求（`List` / `Put` / `Delete` / `Get`），回一个 `FileAnswer`。
    信封文档的读写仍走 `Transport`，两侧各学各的。
  - `0170` 的调用纪律——一次一个请求、call id 身份、同步拒绝的窗口——抽成
    `CallQueue<Req, Ans>` 模板，`TransportQueue` 与 `FileTransportQueue` 共用同一份实现。
  - `SyncBackend` 多两个纯虚：`fileTransport()` 给出本 provider 的文件适配器，
    `eventFilesDieWithTheCopy()` 回答删除云端副本时事件文件是否随之消失（gist 里消失，
    WebDAV 的兄弟文件不消失）。按 provider 名判断的分支收进各后端自己。
  - `TelemetrySyncDuty` 不再持有 client：每个请求向 `sync_.backend().fileTransport()`
    现取现用，凭据由适配器在 dispatch 时从 `sync` 对象读，缺凭据回 `Unconfigured`，
    通道静默收尾——与重构前 “凭据不全就不碰网络” 一致。
  - `SyncDuty::backend()` 暴露后端，注释写明 provider 会重建、按请求取而不要缓存整个
    run。`providerChanged` 时把在飞的 run 记失败收尾，避免旧后端销毁后请求永无应答。
- **后果**：
  - gist 的文件请求与主同步共用同一个 `GistClient`，同时在飞的 HTTP 请求仍是两个
    （两条队列各一个），与重构前两个独立 client 时相同；跨队列错位的应答由 call id
    判不出来，直接丢弃。
  - “未配置” 的判断从 run 开头挪到了第一个 `List` 请求被拒：本机先 flush、读完月份文件
    才静默退出，多花几次本地 IO，换来少一份 provider 分支。
  - `SyncBackend` 的接口变宽（两个新纯虚）。只有真实适配器实现它，测试经
    `makeSyncBackend` 构造，没有额外 fake 要跟着改。
  - 被排除的选项：再给事件通道单配一套 backend（第三处 provider 重复，正是要消除的）；
    把文件动词塞进 `Transport` 的 `Request kinds`（信封与文件的失败语义、重试与并发
    节奏不同，混进一个接口会让两侧调用点各学一半）。
