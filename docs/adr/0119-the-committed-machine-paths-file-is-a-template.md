# 0119 签入的机器路径文件叫 paths.default.json

- **状态**：accepted（2026-10-26）
- **背景**：ADR 0006 把机器路径分成两层：一个签入的基文件带本机值，`config/paths.local.json`（gitignore）逐键覆盖。基文件一直叫 `paths.json`，于是它在文件名上读起来像**生效的配置**，而不是模板；模板这层意思只写在 `_comment`、`config/README.md` 和脚本的注释里。2026-10-26 一次手跑的 gtest 正是照着旧 recipe 直接解析了 `paths.json`，拿到陈旧的 `B:/qtt` 根，而不是 `paths.local.json` 里的 `C:/Qt`。路径工具后来补了 `show-paths.ps1` 与 `gtest.ps1`，但文件名还在邀请同一个错误：任何新读者、或任何打开 `config/` 的人，先看到的都是 “paths.json”。
- **决定**：把签入的基文件改名 `config/paths.default.json`；`config/paths.local.json` 不变，仍是逐键覆盖的本机层。两层仍只有一个读者：`scripts/build/paths.ps1` 的 `Get-LensPaths` 先读 default 再读 local。同步更新的读取方与描述方：`CMakeLists.txt`（`string(JSON)` 的基文件）、`.githooks/pre-commit`（`lens_path` / `lens_path_list` 的存在检查与绝对路径豁免）、`scripts/quality/format-edited-cpp.sh`、`scripts/qa/pet/chrome.mjs`、`show-paths.ps1` 的来源列、各 PowerShell 与 shell 脚本的注释与错误文案、CI 注释、`config/README.md`、`AGENTS.md`、`TEST.md`、`scripts/README.md` 与 ADR 0006 的引用。
  - 文件名是唯一新增的语义：`default` 一词把 “可被覆盖” 写在调用现场，脚本和文档不必再解释 `paths.json` 不是最终值。
  - `paths.default.json` 的 `_comment` 同时改写为 “committed defaults”，并把 `${HOME}` 的展开说明留在同一处。
- **后果**：
  - 旧名在树里消失；配置的字节形状、键名、合并顺序与所有运行时行为不变，CMake 重新 configure 后编译定义照旧。
  - 代价是一次跨文件的机械改名：CMake、钩子、CI、PowerShell / shell 脚本、Node 脚本、测试夹具里的样例、文档与豁免名单都要同步；漏掉一处读取方会在下一次构建或提交时暴露。`show-paths.ps1` 的来源列会显示 `paths.default.json`，本机覆盖仍显示 `paths.local.json`。
  - 被排除的方案：只靠注释和文档提醒，已经失败过一次；加一个 `_template: true` 标记，读取方还得额外检查它，文件名依旧误导；把本机层改名而不动基文件，无法解决 “基文件看起来像生效配置” 的错觉。
- **ADR 关系**：0006 的分层、单一读者与豁免理由全部不变，只换了签入文件的名字并补了状态行；0117 的 `PetThrow`、0118 的 `PetHide` 与机器路径无关；`a802245` 的 `gtest.ps1` / `show-paths.ps1` 继续是 agent 的唯一入口。
