# 0156 同步的 provider 缝：backend 接口与设备本地的 provider 键

- **状态**：accepted
- **背景**：云同步唯一后端是 GitHub Gist（ADR 0042），国内读者直连 api.github.com 困难；既定路线先立缝、再落 WebDAV（坚果云/Alist/NAS）。同步轮（SyncRound）早已只认 `core::sync::Transport` 纯虚接口，与 provider 无关；写死 GitHub 的是适配层——SyncDuty 的成员初始化直接构造 GistClient/GithubAuth/SyncTransport，设备码登录流与错误映射也在它体内。
- **决定**：
  - 新增 `SyncBackend`（app 层抽象 QObject）：id、configured、transport，外加登录流（signInPhase/userCode/verificationUri/startLogin/cancelLogin/abortSignIn），信号 changed/failed/completed；失败进信号前已映射成页面错误串。
  - GitHub 的全部 provider 逻辑整体搬进 `GithubBackend`；SyncDuty 只剩轮、定时器、partition 与页面属性，不认识任何 provider。
  - settings 的 `sync` 段新增 `provider` 键，缺省 `github`，设备本地、不进同步内容；键名仍只在 sync_partition.cpp 拼写。
  - 工厂 `makeSyncBackend` 遇不认识的值告警并回退 github。
- **后果**：
  - 本步行为零变化：QML、错误码、请求序列不变，无新字符串。
  - 旧文档零迁移：provider 键缺席即 github；值拼错不报错、只有一条 WARN——选择静默降级而不是阻断同步。
  - 第 2 步的 WebDAV 实现同一接口：登录流是 “服务器 + 应用密码” 表单，无设备码，userCode/verificationUri 返回空。
  - 被排除的方案：
    - 只把 transport 做成工厂、登录流留在 SyncDuty：第 2 步还得回头改 SyncDuty 的 state/startLogin/unconfigured 判断，缝只留一半。
    - provider 存 `data/sync.json`：那是随包发布的公共数据，放的是 OAuth client id，读者个人的后端选择不该进去。
