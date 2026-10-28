# 0133 离线 OCR 只交付 Tesseract 与 RapidOCR

- **状态**：accepted。取代已删除的 ADR 0131。
- **背景**：Windows OCR 要让普通读者使用，需要签名稀疏 MSIX 提供 package identity；正式 CA 证书、Inno Setup 注册和适配器都还没有落地。2026-10-28 的临时证书复验确认包标识下英语可用，但 100% 中英混排样本在两个识别器下都没有命中中文短语，纯中文也有误读。继续维护这条未交付路径，成本高于收益。
- **决定**：删除 Windows OCR 的适配器门禁、设置项、状态、测试探针与相关文档。离线 OCR 的选项固定为三项：`auto`（Tesseract）、`tesseract`、`rapidocr`；RapidOCR 仍只用于明确选择，自动策略始终走 Tesseract。在线 OCR 不受影响，继续独立于本地方案。
- **后果**：旧的 `ocrLocalEngine=windows` 在启动时迁移为 `auto`；不再需要代码签名证书、稀疏包或包标识相关的工作。以后若重新引入 Windows OCR，必须先用新的 ADR 重新评估语言、混排、签名和安装形态，不能直接恢复旧代码。
