# 0177 DPAPI 封存设置文档里的同步凭据

- **状态**：accepted
- **背景**：`settings.json` 明文存着 GitHub 的 token/refreshToken 与 WebDAV 的 appPassword。文档在读者 profile 内（ADR 是 `settingsPath()` 的边界），但一份备份、一块同步盘、同一台机器的另一个账户、或一份拷出机器的文档，拿到的都是可直接使用的凭据。SECURITY.md 一直如实写着 “plain JSON that Lens does not encrypt”；云副本的封套已有 passphrase→PBKDF2→AES-GCM（docs/adr/0042），那套 BCrypt 基建就是现成的落点。
- **决定**：
  - `vault` 增 `dpapiSeal`/`dpapiOpen`（CryptProtectData/CryptUnprotectData，`CRYPTPROTECT_UI_FORBIDDEN`），lens_core 静态导入 crypt32，不添新依赖；与封套的失败模型一致——只带错误码，不带明文。
  - 三个凭据键（`token`、`refreshToken`、`appPassword`）在文档里写为 `dpapi:` 前缀接 `base64(blob)`，读端开启。裸值是升级前的旧文档，读作明文，下一次写入重封。
  - 启动时 `main` 在职责起来之前一次性 `sealBareSecrets()`，把旧文档的裸值当场封掉。不在 getter 里迁移写盘：KnownStore 的 section 每写即 save，读路径加写等于凭空的落盘与竞态。
  - 封不开的值读作空：页面读作未登录，正是这个状态下的诚实答案，另配一行 LENS_WARN。
  - `key`（云副本密钥）不在本次范围：它牵动 passphrase 的产品语义（换机转移、忘记 passphrase 的含义），单独立项；SECURITY.md 对它仍然成立：能读文档就能解密云副本。
- **后果**：
  - 边界是文件静态时的保护，防的是备份、其他账户、拷出去的文档；同账户下运行的任何程序照样能开启——这是 DPAPI 的模型，不声张更多。
  - 文档随 profile 迁去另一个账户后凭据读作空，读者重新登录一次；密文本身无恙。
  - 不用 Windows 凭据管理器：凭据便离开文档一份，导出/重置/同步快照要跟两处。
