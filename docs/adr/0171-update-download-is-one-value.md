# 0171 更新下载的状态收成一个值

- **状态**：accepted
- **背景**：UpdateDuty 的下载状态原先由十来个并列成员描述：阶段之外还有摘要清单的四个旗标（是否读到、是否可下载、待答的按键、清单属于哪个版本），以及传输所属版本、校验摘要、失败种类和三组读数。阶段与清单分开维护，二者可以互相矛盾：`cancelDownload()` 清掉 `expectedDigest_` 却保留清单已读且可下载的旗标，取消后再按一次下载会直接开始传输，文件到达时因摘要为空被判成 `checksum` 失败。Stage 2 前半已把阶段收成枚举并让 `resetDownload()` 成为唯一转移点，这一项收掉剩下的旗标。
- **决定**：
  - `update_pure.h` 增 `Download` 值：`DownloadStage{Idle, Downloading, Verifying, Ready, Failed}`、失败种类、传输所属版本、`DigestList{Unasked, Asked, Known}`（版本、摘要、待答的按键）与传输读数（字节、百分比、ETA）。摘要清单是版本的事实而不是某次传输的事实，所以取消只归还传输的版本与半成品文件，清单和摘要保留。
  - `downloadMap()` 是卡片读的唯一投影：`state` 由 `downloadToken()` 铸造，`available` 只在清单已答且摘要非空时为真。duty 的 `download()` 只返回它的结果。
  - duty 保留资源与接线（`QFile`、`QCryptographicHash`、`QElapsedTimer`、`cancelling_`），`resetDownload(DownloadStage)` 仍是阶段与读数的唯一转移点；调用点改为读写 `download_`。
- **后果**：取消后再按下载复用卡片已读到的摘要，不再固定以 `checksum` 失败（`ASecondPressAfterACancelUsesTheOfferTheCardAlreadyRead` 钉住）；投影有独立用例（`TheCardIsOneProjectionOfOneValue`）。其余行为逐字保持，UpdateDuty 原有 28 例为门禁，现有 29 例全绿。`download()` 的键与字符串协议不变，QML 零改动。
