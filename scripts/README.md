# 脚本目录

脚本按用途分组。命令默认从仓库根目录执行，机器相关路径统一由 `scripts/build/paths.ps1` 读取 `config/paths.json`。

## 目录

| 目录 | 用途 |
| --- | --- |
| `build/` | Debug、RelWithDebInfo 构建和机器路径读取 |
| `data/` | 词形数据生成与样例集独立校验 |
| `pet/` | 桌宠美术流水线：形状配方、网格写入、配饰对位、素材同步与审查图（见其 README 与 `docs/pet-art-pipeline.md`） |
| `profiling/` | QML profiler、QML 结构指标和 trace 解析 |
| `qa/` | 起应用、QTest 单条/整套、桌面驱动、截图、QML 快照和鼠标停顿探针；`qa/pet/` 是桌面宠物原型的逐动作对比与交互检查（见其 README） |
| `quality/` | QML lint、分层边界检查、C++ / QML 覆盖率和 AST 指标；`zhlintrc.json` 是 CI 上排版检查用的仓库内配置 |
| `release/` | 安装包构建和 GitHub Release 发布 |

## 构建与验证

```bat
scripts\build\build.bat
scripts\build\build-release.bat
```

```sh
sh scripts/quality/qml-lint.sh
sh scripts/quality/layer-check.sh         # app/core 的分组边界，pre-commit 与 CI 也会跑
sh scripts/qa/readme-html-shots.sh       # 用无头 Chrome 重画 README 的 HTML 营销图
python scripts/qa/readme-html-compose.py # 裁图、合成语言切换 GIF、学习循环拼图与主题拼图
```

```powershell
powershell -File scripts/qa/ui-run.ps1        # 起 release 树的应用：旧了就重建，见 docs/qa.md
powershell -File scripts/qa/qtest.ps1 -Target lens_qtest_components   # 整套；加 -Case <用例> 只跑一条，见 TEST.md 第 2 节
powershell -File scripts/qa/readme-gifs.ps1   # 离屏渲染 QML 动图与设置页截图（旧 README 管线，当前 README 不引用）
```

旧的 `scripts/qa/readme-shots.sh` 与 `readme-gifs.ps1` 仍保留，但它们写 `docs/readme-images/`，当前 README 不再引用那里的图片；新素材写到 `docs/readme/`。
整条 HTML 素材管线需要 Pillow（`pip install Pillow`）：截图脚本末尾用它裁社交预览，合成脚本用它做 GIF 和拼图。`readme-html-shots.sh` 会在常见安装位置找 Chrome，不启动应用。
改 README 的图时先跑上面两条，再检查 `docs/readme/` 下的产物。

具体测试目标见 [`TEST.md`](../TEST.md)；profiling 与覆盖率口径见 [`docs/metrics/profiling-and-coverage.md`](../docs/metrics/profiling-and-coverage.md)；桌面验证方法见 [`docs/qa.md`](../docs/qa.md)。

## 发布

版本号只在根 `CMakeLists.txt` 的 `project(Lens VERSION ...)` 中维护。提交版本变更和
`CHANGELOG.md` 后，可单独生成安装包：

```bat
scripts\release\build-installer.bat
```

```powershell
powershell -File scripts/release/publish-release.ps1 -Version 1.1.0 -WhatIf
powershell -File scripts/release/publish-release.ps1 -Version 1.1.0
```

发布脚本要求干净工作树、CMake 版本与 CHANGELOG 对应，然后重新构建安装包并核对 release QML lint。
它推送当前分支，创建并推送指向当前提交的注释 tag `v<version>`，用该版本的 CHANGELOG 段落发布
两个产物：`Lens-<version>-setup.exe` 和免安装的 `Lens-<version>-portable.zip`；两个包的摘要都写进
同一个 `SHA256SUMS`，更新通道按文件名取自己那一条（ADR 0047）。
`-WhatIf` 只做前置核对，`-Draft` 创建草稿。

先完成 `gh auth login`，按 `TEST.md` 完成测试，并人工验证安装后的应用。若 tag 推送后发布失败，脚本保留 tag；
核对 tag 指向与本地产物后，可直接用 `gh release create --verify-tag` 恢复发布。具体安装和版本取舍见 ADR 0011。
