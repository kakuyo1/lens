# Windows OCR 稀疏包部署矩阵（2026-10-28）

> 目的：验证 [ADR 0131](../adr/0131-windows-ocr-waits-for-a-signed-package.md) 选择的签名稀疏 MSIX + external location 方案是否能给 Win32 程序包标识，并让 Windows.Media.Ocr 的部署门可开。
> 本机原本已有 Lens 1.7.1；矩阵使用独立的 `LensOcrMatrixProbe`，没有用 Lens 本体做包注册实验。安装产物另见[安装产物检查](ocr-installer-2026-10-28.md)。

## 1 探针与未打包基线

探针是 C++/WinRT 控制台程序，读取同目录合成图，输出包标识、OCR 识别器语言、`MaxImageDimension`、各语言的行/词/字符数与结果；不读桌面、不联网、不打印识别文本。应用清单含 `msix` 元素，包清单是 identity-only sparse package（`uap10:AllowExternalContent=true`、`mediumIL`、`win32App`、`runFullTrust`、`AppListEntry=default`）。

未打包基线：

| 项目 | 结果 |
|---|---|
| 系统 | Windows 11，10.0.26200 |
| 包标识 | `<none>` |
| 识别器语言 | 1 个：`zh-Hans-CN` |
| `MaxImageDimension` | 10000 |
| 合成中文图 | 3 行、85 词、169 字符，结果 `ok` |

这与 [OCR 基线](ocr-baseline-2026-10-28.md)的探针一致：Windows.Media.Ocr 在未打包进程中可运行，但没有官方桌面支持所需的 package identity。

## 2 签名、证书信任与注册

工具：Windows SDK 10.0.26100 的 `MakeAppx.exe`、`SignTool.exe`、`mt.exe`。包版本 v1 `1.0.0.0`、v2 `1.0.0.1`；证书为自签名代码签名证书 `CN=Lens OCR Matrix Test`。

第一次尝试只把证书放进 `CurrentUser\TrustedPeople` 与 `CurrentUser\Root`，`Add-AppxPackage` 仍报 `0x800B0109 CERT_E_UNTRUSTEDROOT`；机器级 `certutil -addstore Root` 在非管理员下返回 `0x80070005`。

改用管理员权限后：

| 步骤 | 结果 |
|---|---|
| 证书导入 `LocalMachine\TrustedPeople` 与 `LocalMachine\Root` | 成功 |
| `Add-AppxPackage -Path v1 -ExternalLocation probe` | **成功** |
| 探针直接运行（非 shell 激活） | `package=LensOcrMatrixProbe_1.0.0.0_neutral__3q8yezdnvyyf8` |
| v2 `Add-AppxPackage` | 成功，版本升到 `1.0.0.1`，包族名不变 |
| `Remove-AppxPackage` | 成功；外部目录保留，符合 sparse package 语义 |
| 证书与 PFX 清理 | 机器/用户存储证书删除，PFX/CER 删除 |

签名本身也独立验证：`SignTool sign /fd SHA256` 成功，`signtool verify /pa` 通过。Windows 11 的这套构建不接受仅存在于当前用户存储的自签名证书；机器级信任是本机可注册的必要条件。

## 3 包内 Windows OCR

`zh-Hans-CN` 在 v1 与 v2 包标识下都识别成功：

| 版本 | 包标识 | 识别结果 |
|---|---|---|
| v1 `1.0.0.0` | `LensOcrMatrixProbe_1.0.0.0_neutral__3q8yezdnvyyf8` | 3 行、85 词、169 字符，结果 `ok` |
| v2 `1.0.0.1` | `LensOcrMatrixProbe_1.0.0.1_neutral__3q8yezdnvyyf8` | 3 行、85 词、169 字符，结果 `ok` |

这是本轮第一次在真实 package identity 下跑 Windows.Media.Ocr；未打包基线虽然也能识别中文，但不满足官方支持边界。

## 4 语言包矩阵

| 矩阵臂 | 结果 |
|---|---|
| 未打包 + `zh-Hans-CN` | 已跑：可识别，但无包标识 |
| 稀疏包 + `zh-Hans-CN` | **已跑通**：包标识 + 3 行/85 词 |
| 稀疏包 + 英语识别器 | **未验证**：`Language.OCR~~~en-US~0.0.1.0` 可被发现，但 `Add-WindowsCapability` 停在 Windows Update 下载 50%，20 分钟无进展；中止后状态仍为 `NotPresent` |
| 稀疏包 + 中英双语 | **未验证**：依赖英语能力 |
| 安装 → 升级 → 卸载 | **已跑通**：v1 → v2 → 移除；外部目录保留 |

语言能力安装没有完成，也没有把 en-US 写入系统；`Get-WindowsCapability` 在清理阶段返回 `NotPresent`。因此英语/双语的语言条件仍未验证，不能据此启用 Windows 后端。

## 5 清理与边界

- 包已移除；`Get-AppxPackage LensOcrMatrixProbe` 为空。
- 测试证书已从 `LocalMachine\TrustedPeople`、`LocalMachine\Root`、`CurrentUser\TrustedPeople`、`CurrentUser\My` 删除；PFX/CER 已删除。
- 探针外部目录保留在私有临时目录；sparse 包卸载不删除外部文件，符合预期。
- 现有 Lens 1.7.1 没有被这个矩阵安装、升级或卸载；它随后由安装产物矩阵升级到 1.9.0，见[安装产物检查](ocr-installer-2026-10-28.md)。
- 没有安装或删除系统语言包；中止的 en-US OCR 能力没有留下已安装状态。

## 6 结论

稀疏 MSIX 的**签名、机器级证书信任、包注册、package identity、中文 Windows OCR、升级和卸载**已在本机通过；**英语/双语**因 Windows Update 停滞未验证。这个结果把部署形态是否可行从未知变成了部分已验证，但还不足以启用 Windows 后端：

- 生产发布仍需要机器范围受信任的正式签名证书，不能要求读者导入自签名根证书。
- Inno Setup 注册稀疏包、证书分发、升级/卸载时的包注销还没有产品化。
- 英语 OCR 语言包未验证，Windows 适配器也未实现。

ADR 0131 的 1.0 不启用 Windows OCR 仍然成立；后续先把正式证书/安装器注册和英语语言条件补齐，再谈适配器。
