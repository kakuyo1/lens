# 0131 Windows OCR 等待有签名的打包通道

- **状态**：accepted。限定 1.0 的交付形态；不改变 ADR 0130 的本地优先与明确选择目标。
- **背景**：Windows.Media.Ocr 的桌面支持以 package identity 为条件；Lens 当前由 Inno Setup 安装，另有便携 ZIP。2026-10-28 的探针在一台未打包的 Windows 11 上能识别本机已安装的 `zh-Hans`，但没有包标识，也没有英语识别器。某台机器上偶然可运行，不等于获得官方支持，也不能保证目标机器的语言包与版本。
- **决定**：1.0 不引入包标识，不实现也不启用 Windows 适配器；自动策略继续走 Tesseract，明确选择 Windows 仍报 `deployment-unsupported`，不回退。将来若要让 Windows OCR 进入交付，采用**签名稀疏 MSIX（external location）**给现有 Inno Setup 程序包标识，而不是把整个程序改为 MSIX。启用前必须跑完部署矩阵，结果记入[稀疏包矩阵](../metrics/windows-ocr-sparse-package-2026-10-28.md)。
- **后果**：Windows 选项在当前 UI 中只是诊断项，不承诺可用；Tesseract 继续是开箱默认。走稀疏包需要另做证书信任、侧载、安装、升级和卸载，并承担签名成本；完整 MSIX 与打包伴生包因替换或追加安装形态而被排除。适配器、语言枚举与错误映射在矩阵通过前不写，避免用未验证的 WinRT 路径冒充已交付能力。
- **2026-10-28 矩阵**：用独立探针在本机机器级自签名信任下完成了包注册、package identity、`zh-Hans-CN` Windows OCR、v2 升级和卸载；英语 OCR 能力因 Windows Update 下载停滞未安装，生产证书与 Inno Setup 注册仍未实现。结果见[稀疏包矩阵](../metrics/windows-ocr-sparse-package-2026-10-28.md)。1.0 不启用 Windows OCR 的决定不变。
