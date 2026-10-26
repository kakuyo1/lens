# 0042 GitHub 设备登录，数据存进读者自己的 secret gist

- **状态**：accepted（2026-10-14）。2026-10-16 补：访问令牌 8 小时过期是常见默认，续期路径见 “令牌续期” 与后果两条。读者确认：口令加密；令牌放 `settings.json`；解释缓存不同步；提供 “退出登录” 与 “删除云端数据”。不改 `AGENTS.md`：令牌与 API key 同属 `settings.json`，不新增存放规则。`PRODUCT.md`、`SECURITY.md`、`UI.md`、`GLOSSARY.md` 与 README 的隐私段随实现一并更新。
- **背景**：读者想在几台设备之间共享设置与学习数据；应用要能分发给别人，项目方不想租服务器，也不想让自己的电脑长期在线。已会词与查词历史属于 `PRODUCT.md` 所说的隐私边界，同步不能扩大它的暴露面。国内用户必须走代理，这一点已接受。

  2026-10 核对的 GitHub 文档事实：设备流只需要 OAuth App 的 `client_id`，不需要 `client_secret`；`gist` scope 只授予 gist 的读写，`repo` 则授予全部仓库，所以不申请 `repo`；gist API 每个文件最多返回 1 MB，更大的要读 `raw_url`；文档只说明 gist 分公开与 secret 两种，没有写明 secret 的访问范围，因此按 “拿到 gist ID 的人可以读” 设计。

  **访问令牌的过期**（2026-10-16 更正）：原写 “访问令牌默认不过期，只有应用主动请求 `offline_access` 才会过期”，这是错的。GitHub 文档写的是：**访问令牌 8 小时后过期、刷新令牌 6 个月不用才过期**；触发条件除请求 `offline_access` 外，还包括 OAuth App 自己勾了 “Expire user access tokens” 这个开关，而新建的 OAuth App 默认就勾着。勾上后登录响应里多出 `refresh_token`、`expires_in`、`refresh_token_expires_in`，且**刷新令牌每用一次就轮换**（旧的访问令牌与用掉的刷新令牌都作废）。这个错误的事实正是下面 “令牌续期” 那条与后果里那个洞的来源。

  现有存储：`known` 对象是 “词 → 布尔”，没有修改时间，也没有删除记录；`daily` 是每台设备各自的日计数；`history` 每条带词与分钟时间。同步因此需要新的存储字段，见实现约定。

- **决定**：
  1. **登录**：GitHub OAuth App，走设备流。应用显示 8 位码，读者在 `github.com/login/device` 输入，应用轮询取令牌。不开本地回调服务，不需要 `client_secret`。`client_id` 写在 `data/sync.json`，留空则登录入口显示 “未配置”。GitHub 是唯一的登录方式，其余方式记在 `TODO.md`。
  2. **权限与令牌**：只申请 `gist`。令牌存进 `settings.json` 的 `sync` 对象，与 API key 处于同一信任边界。令牌不写进 gist，也不进入同步内容。
  3. **存储**：读者账号下的一个 secret gist，描述固定为 `lens-sync`，首次同步时创建，新设备登录后按描述找回。它没有服务端，也没有项目方的数据。
  4. **加密**：上传前在本机加密。口令由读者设置，每台设备输入一次；派生出的密钥存进 `settings.json`，口令本身不存。忘记口令则云端数据无法恢复，这一点在设置页明说。
  5. **同步内容**：已会与新词标记、查词历史、统计、设置白名单（见实现约定）。解释缓存不同步：它可以重新获取，而且体积大。
  6. **合并规则**：
     - 已会标记：每个词记修改时间；删除写墓碑（带时间）。两者比较时间，较新者胜出，旧数据不能让已删的词复活。
     - 查词历史：取并集。
     - 统计：每台设备一个 `daily` 桶，合并是各桶的并集。直接取大会在两台设备各自统计时丢数据，直接取和又会在重复同步时重复计数。
     - 设置：每个键记修改时间，取较新的。
  7. **同步时机**：启动后约 5 秒、修改后防抖约 10 秒、手动按钮各触发一次。离线时先写本地，恢复后再同步。
  8. **出站地址**：`github.com`（登录）与 `api.github.com`（同步）。沿用检查更新的出站路径与系统代理设置，`SECURITY.md` 写明。
  9. **退出与删除**：“退出登录” 删除本机的令牌与派生密钥，云端不动；“删除云端数据” 删除 gist，并清掉本机的 gist ID 与派生密钥，令牌保留。两者都不删除本机的学习数据。

- **实现约定**（实现与测试以此为准）：
  - **加密信封**：gist 文件 `lens-sync.json`，内容为 `{"format":1,"kdf":"pbkdf2-sha256","iterations":600000,"salt":"<base64>","nonce":"<base64>","ciphertext":"<base64>"}`。AES-256-GCM，12 字节随机 nonce，128 位标签附在密文末尾。派生密钥 32 字节，用 Windows 自带的 BCrypt（CNG）实现，不引入依赖。口令不对时 GCM 认证失败，报 “口令不对”，不回退。
  - **同步文档（明文，加密前）**，格式 1：
    ```
    {
      "format": 1,
      "known":   { "<lemma>": { "learned": true|false, "at": <ms> } },
      "deleted": { "<lemma>": <ms> },
      "history": [ { "word": "...", "minute": "YYYY-MM-DD HH:MM", "verdict": "..." } ],
      "daily":   { "<deviceId>": { "YYYY-MM-DD": { "pops", "learned", "fresh", "promptTokens", "completionTokens", "models" } } },
      "settings": { "<key>": { "value": <json>, "at": <ms> } }
    }
    ```
    `deviceId` 是每台设备首次启动生成的随机 UUID，存在 `settings.json` 的 `sync.deviceId`，不随云端同步。本机上传时只带自己的 `daily` 桶，远端桶原样保留。
  - **本地新字段**：KnownStore 新增 `knownAt`（词 → 毫秒）与 `knownDeleted`（词 → 毫秒），由 KnownStore 在 `save()` 中重写，与 ADR 0024 的七键同一条规则。旧数据没有时间的，视为 0。
  - **统计的显示**：StatsStore 把本机桶（仍写进 `daily`）与远端桶（写进 `syncDaily`）分开保存。界面显示两者之和；上传只取本机桶。
  - **设置白名单**：`level`、`explanationLang`（由 KnownStore 持有），`multiSense`、`etymology`、`example`、`uiLanguage`、`theme`、`bubbleDismissSeconds`、`bubblePlacement`、`selectionBarStyle`、`animationsEnabled`、`minimumWordLength`，以及外观的玻璃参数（`frosted*`、`liquid*`）与自定义颜色。明确不同步：`API-KEY`、`baseUrl`、`model*`、`selectionCapture`、`autoScan`、`scanWhitelist`、`ocrCapture`、`ocrHotkey`、`tesseract*`、`clipboardPolicy`、`pet*`、`dailyBudget`、`autoCheck`、`lastCheckDate`、`passthrough`、`scale`，以及自启。
  - **设置的修改时间**：不在每个设置 setter 里打点。每次同步把白名单的当前值与上次同步的快照比较，变了的键记为本次同步的时刻；没变的键沿用上次合并得到的时间。代价是修改时间可能比真实修改晚，至多差一个同步周期（约 10 秒的防抖），对合并结果没有可见影响。
  - **gist API**：`POST /gists`（secret）、`GET /gists`（分页，按描述找回）、`GET /gists/{id}`（文件 `truncated` 时读 `raw_url`）、`PATCH /gists/{id}`、`DELETE /gists/{id}`、`GET /user`（取登录名，用于设置页显示）。
  - **设备流**：`POST https://github.com/login/device/code`，`POST https://github.com/login/oauth/access_token`，`grant_type` 为 `urn:ietf:params:oauth:grant-type:device_code`。`slow_down` 时间隔加 5 秒；`authorization_pending` 继续轮询；`expired_token` 与 `access_denied` 结束登录。
  - **本地 `sync` 对象**：`deviceId`、`token`、`refreshToken`、`login`、`gistId`、`key`（派生密钥，base64）、`salt`、`iterations`、`lastSyncedAt`。任何一项都不写入日志。“退出登录” 把两个令牌与派生密钥一并删掉。
  - **令牌续期**：登录响应里的 `refresh_token` 存进 `sync` 对象的 `refreshToken`，续期拿到的新一对令牌覆盖旧的一对。gist API 回 401（`GistFailure::Unauthorised`）且手上有刷新令牌时，用 `grant_type=refresh_token` 换一对新令牌，存下这一对，再把这次同步（或删除）从断掉的那一步重来一次；**每次运行最多换一次**，所以一个被吊销的令牌只花一次请求。刷新令牌自己被拒（`bad_refresh_token`）时忘掉本地两个令牌，页面回到未登录态并提示读者重新登录 —— 设备码过期与应用不认这个登录是两回事，文案与出路都不同。应用不过期令牌时响应里没有刷新令牌，行为与从前一致。（2026-10-26 补：换令牌时应用自己那对凭据被拒（`incorrect_client_credentials`）走同一条路 —— 见后果里那条实测。）

- **后果**：
  - **存储格式要改**：`known` 增加时间与墓碑，`daily` 增加远端桶。这是本决定最大的改动，涉及 `KnownStore` 与 `StatsStore`。老数据没有新字段，按上面的约定读作默认值，并配迁移测试。
  - **GitHub 能看到的**：账号下存在一个 gist，它的大小、更新时间与访问它的 IP。内容是加密后的，GitHub 读不到。这在 `SECURITY.md` 写清。
  - **隐私边界变化**：`PRODUCT.md` 的隐私段与 ADR 0012 都说本地文件是唯一的数据边界。同步之后，加密副本也存在 GitHub。两处随本 ADR 一并改。派生密钥与令牌同在 `settings.json`，拿到这个文件的人能解开云端副本，这一点写在 `SECURITY.md`。
  - **分发**：每位用户用自己的 GitHub 账号授权，同意页会显示 `gist` 权限。项目方要在 GitHub 上注册 OAuth App 并打开设备流，把 `client_id` 写进 `data/sync.json`。这一步需要项目方账号操作，不由代码完成。
  - **代理**：国内用户登录和同步都需要代理，应用不额外配置代理。
  - **明确不做**：手机号或邮箱验证码、微软与谷歌登录、托管后端、Web 端，见 `TODO.md`。
  - **不同步的缓存**：换设备后解释要重新请求，会再花一次钱。这是为了体积与简单做的取舍。
  - **8 小时令牌是默认，不是例外**（2026-10-16 补）：勾了 “Expire user access tokens” 的 OAuth App，登录后 8 小时访问令牌失效。此前应用只存访问令牌、也没有续期路径，于是**每次同步都回 401，而页面把 401 说成 “登录码已过期”**，读者反复重登也只在 8 小时内有效 —— 这就是真机上那个 “Account & Sync 一直提示 the sign-in code is expired”。现在存刷新令牌并自动续期，读者不必每 8 小时重登；代价是 `settings.json` 里多一个同等级的密钥（`SECURITY.md` 已更新），以及 401 的时候多一次请求。
  - **续期只做一层**：只对 gist 的 401 续期，且每次运行最多一次，不做提前续期（不存 `expires_in` 换算出的到期时刻）。令牌被吊销时因此只花一次请求就落到 “请重新登录”，而不是反复重试；如果将来这个多出来的一次往返值得省，再存到期时刻提前换。
  - **OAuth App 的续期在真机上走不通**（2026-10-26 实测）：`POST /login/oauth/access_token` 的 `refresh_token` grant 要求 `client_secret`；GitHub 文档里 “设备流签发的令牌可以不传 secret” 那条写在 GitHub App 名下，OAuth App 不适用。实测：同一个 client id 对 `/login/device/code` 正常，带真实 refresh token 换令牌却回 `incorrect_client_credentials`。桌面应用无处安放 secret（放进 `data/sync.json` 等于公开），所以这条路在当前形态下是死的：**要么关掉 OAuth App 的 “Expire user access tokens”，让访问令牌不过期、续期永不触发；要么 8 小时重登一次**。选择前者，续期代码保留（将来换到能带 secret 的客户端，或 GitHub 给 OAuth App 开公共客户端）。这个错误码此前不被 `parseTokenPoll()` 识别，落到 “通用失败”，页面于是留着一个永远不成功的 “Sync now”；现在它与 `bad_refresh_token` 同归 “请重新登录”，日志里也带上状态码与 GitHub 自己的错误码。
