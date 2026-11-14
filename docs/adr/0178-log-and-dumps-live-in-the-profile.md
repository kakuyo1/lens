# 0178 日志与崩溃转储随安装版进 profile

- **状态**：accepted
- **背景**：`log::init` 的默认参是相对 `logs`，`main` 无参调用——CWD 在哪里，日志就在哪里。安装版的启动（Run 键、开始菜单）CWD 是安装目录：Program Files 可能只读，日志静默退成 stderr；目录多账户共享；而日志可以带着查词记录。SECURITY.md 一直如实声明 packaged launches 的日志可能写进安装目录。崩溃转储随日志目录（docs/adr/0174），同样落在安装目录。
- **决定**：
  - `app/paths` 增 `installedBuild()`（exe 旁有 `data/` ⇔ 安装版，`dataDirectory()` 的原判据上移为唯一所有者）与 `logDirectory(installedBuild)`：安装版 → `%APPDATA%\Lens\logs`，与 `settingsPath()` 同一 profile；开发树 → 相对 `logs/`，`tail -f logs/lens.log` 与 `ui-run.ps1` 的读取位置不变。
  - 目录在 QApplication 之前解析（崩溃处理器必须先装），走原生 SHGetKnownFolderPath；`Lens` 叶子与 `main` 的 applicationName 保持一致。解析失败回退相对名，`log::init` 自己会警告。
  - `log::init` 去掉 logDir 默认参：相对路径默认值就是这次 bug 的形状，调用方必须指名。
  - `lens.iss` 的 `{app}\logs` 卸载清理保留，清旧版本遗留；组键 WorkingDir 不动，只保证 CWD 确定。
- **后果**：
  - 卸载不再带走日志与转储——它们随 profile 存活，与 SECURITY.md 删除 `%APPDATA%\Lens` 即移除本地数据的既有指引一致；该文档相应改写。
  - 安装版的排查位置是 `%APPDATA%\Lens\logs`；崩溃通知打开转储文件夹的那枚胶囊从 `main` 传下去的同一目录，自动跟到新位置。
