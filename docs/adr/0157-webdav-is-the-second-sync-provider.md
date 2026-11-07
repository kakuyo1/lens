# 0157 WebDAV 是同步的第二个 provider

- **状态**：accepted（实现随 provider 缝之后的第 2 步落地）
- **背景**：云同步唯一后端是 GitHub Gist（ADR 0042），国内读者直连 api.github.com 困难。ADR 0156 已立好 provider 缝：`SyncBackend` 接口、`sync` 段的 `provider` 键、工厂回退。现在选第二个 provider。
- **决定**：
  - **选通用 WebDAV，先于 S3 兼容与 OneDrive**。一个实现覆盖国内大多数场景：坚果云（官方 WebDAV + 应用密码）、Alist（聚合阿里云盘等国内网盘）、Nextcloud 与 NAS 自建。加密在域层完成（ADR 0042 的信封），后端只搬运密文，不需要读者信任存储方。
  - **单文件信封**：`<base>/Lens/lens-sync.json`，`base` 是读者填的服务器地址（一个 collection 的 URL）。首次同步先 `MKCOL` 建 `Lens/`（幂等），再写文件。
  - **请求映射**：List→ `PROPFIND`（文件在则回 id，不在回空）；Read→ `GET`（404 视为云副本不存在，content 空）；Create/Update→ `PUT`；Delete→ `DELETE`；Refresh 不该发生（Basic auth 无刷新），收到即回 `RefusedRefresh` 结束这轮。错密码的 401 走 `Unauthorised`；页面错误串在实现时对齐现有码表。
  - **凭据**：服务器地址、用户名、应用密码，存 settings.json 的 `sync` 段（设备本地、明文 JSON，与 GitHub token 同边界，ADR 0024 的写单位不变）。WebDAV 服务器只见密文信封的大小与时间，见不到内容。
  - **登录流**：无设备码。`SyncBackend::startLogin` 用已存凭据做一次可达性校验；`userCode`/`verificationUri` 恒为空，`signInPhase` 基本只在 Idle。
  - **并发**：沿用 Gist 的语义——last-writer-wins、读时合并（sync_merge），不上 ETag/If-Match；Gist 也没上，两台设备同时写的窗口由下一轮合并收敛。
- **后果**：
  - `WebdavBackend` 实现同一接口、走同一工厂分支，SyncDuty 不动；provider 选择 UI、`setProvider` 与 i18n 随第 2 步。
  - 坚果云的配额与频率限制数字实现时核对官方文档，不写死进代码；信封 KB 级、轮次低频，免费额度预期足够。
  - 被排除的方案：
    - S3 兼容（OSS/COS/R2）先行：SigV4 签名代码量大，读者要开 bucket 与 AK/SK，上手成本高；Alist 已能把 WebDAV 桥接到这些存储。
    - OneDrive 先行：Graph API 要注册应用并过微软审核，独立应用慢赢。
    - 国内 Git 托管的 gist 等价 API：没有稳定公开 API（未逐家核实，不押注）。
    - ETag/If-Match 防并发覆盖：Gist 语义本就没有，加了反而两套 provider 行为不一致。
