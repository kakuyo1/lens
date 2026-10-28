# OCR 安装产物检查（2026-10-28）

> 目标：检查 release 安装产物里的随包 Tesseract、语言数据、截图和密钥边界，并跑一次安装/升级/卸载。
> 构建：`scripts/release/build-installer.bat`，版本 1.9.0，release 树 `build-ninja-release`。产物只落在构建树，没有发布。

## 1 产物

| 文件 | 大小 |
|---|---:|
| `build-ninja-release/installer/Lens-1.9.0-setup.exe` | 66 MB |
| `build-ninja-release/installer/Lens-1.9.0-portable.zip` | 96 MB |
| `payload/ocr/` | Tesseract 5.4.0.20240606、51 个 DLL、`tessdata/eng.traineddata`、LICENSE 与 `THIRD_PARTY_NOTICES.txt` |

本机原有 Lens 1.7.1（同一 AppId，`%LOCALAPPDATA%\Programs\Lens`），读者 profile `%APPDATA%\Lens\settings.json` 存在。经明确许可后处理了这次安装；profile 全程没有读取，也没有删除。

## 2 随包 Tesseract

在 `payload/ocr` 与安装后的 `%LOCALAPPDATA%\Programs\Lens\ocr` 内分别直接运行随包程序，不设置 `TESSDATA_PREFIX`，不借用 `C:\Users\admin\Tools\Tesseract-OCR`：

| 检查 | 结果 |
|---|---|
| `tesseract.exe --list-langs` | 只有 `eng` |
| `chi_sim.traineddata` | 不存在（按设计） |
| `--psm 3 -c tessedit_create_tsv=1` 对合成英语图 | 成功，首行是 12 列 TSV 表头 `level page_num block_num par_num line_num word_num left top width height conf text` |
| License / NOTICE | `payload/ocr/LICENSE`、`payload/ocr/THIRD_PARTY_NOTICES.txt` 存在 |

这台机器的开发 Tesseract 有 `chi_sim`，但 staging 只复制 `eng.traineddata`；安装产物没有把开发机器语言包带进去。

## 3 截图与密钥边界

对 portable 解包目录（2529 个条目）检查：

- 没有 `settings.json`、`settings.local.json`、`*.log`、`*screenshot*`、`*snapshot*` 或 `chi_sim*`。
- `data/` 只有产品资源（图标、宠物、场景），其中 `ui-settings.svg` 是图标名，不是设置文档。
- 对 `.json` / `.txt` / `.ini` / `.conf` / `.js` / `.qml` 文本资源扫描 `sk-...`、`OCR-API-KEY=...`、`"apiKey": "..."`，没有密钥样值。
- 密钥文件仍只在读者 profile 的 `%APPDATA%\Lens\settings.json`；安装产物不携带它。安装器的 `[Files]` 也没有该文件。
- 没有测试记录目录 `test/records`、OCR 快照或语料图片。

二进制里出现 `OCR-API-KEY` 这个**字段名**是预期的（程序要读它）；检查针对的是值，不是字段名。

## 4 安装、升级与卸载

均使用静默参数 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`，同一 AppId 原地升级。

| 步骤 | 结果 |
|---|---|
| 1.7.1 → 1.9.0 静默升级 | 成功；HKCU 卸载项 `DisplayVersion=1.9.0`，安装位置不变；profile 保留 |
| 安装后 OCR 运行时 | 安装目录下 `ocr/tesseract.exe` 存在，`--list-langs` 只有 `eng`，TSV 输出正常 |
| 静默卸载（Lens 正在运行） | **不完整**：注册项移除，但程序文件残留；原因是 `lens.exe` 仍在运行 |
| 停止 Lens 后重装、再卸载 | 注册项与文件被移除；profile 保留；**空插件目录残留** |
| 最终重新安装 1.9.0 | 成功；HKCU 版本 1.9.0；profile 保留；应用已停止，Run 项指向有效 exe |

安装矩阵发现两个残留问题：

- 静默安装后观察到 `lens.exe` 正在运行；HKCU 的 `Run\Lens` autostart 项存在（应用自己的设置写入，不是安装器）。卸载时若不停掉它，Inno Setup 会留下程序文件。
- 卸载器移除了文件和卸载注册项，但没有删除 HKCU `Run\Lens`，并留下 `iconengines/`、`imageformats/`、`platforms/`、`qml/`、`styles/`、`tls/` 等空目录。空目录已在最终重装前手工清理；autostart 项在最终安装中重新指向有效 `lens.exe`。

## 5 未验证

- 开始菜单快捷方式在卸载后的单独检查；`[Icons]` 创建的入口没有逐项核对。
- 安装后观察到 `lens.exe` 进程启动（静默安装后的运行/autostart），但没有做界面、首次运行复制 `settings.local.json` 与日志写入安装目录 `logs/` 的验证；最终只验证了安装/卸载文件、注册项、profile 与进程状态。
- setup 日志没有按预期路径落盘，静默安装日志未作为证据。
- 真实在线请求、Windows 稀疏包和真机视觉检查不在本报告内，分别见 [OCR 基线](ocr-baseline-2026-10-28.md)与[稀疏包矩阵](windows-ocr-sparse-package-2026-10-28.md)。
