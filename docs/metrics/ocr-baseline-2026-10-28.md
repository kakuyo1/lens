# OCR 基线与接入验证（2026-10-28）

> 代码基点：本轮实施前 `dbc7c81`，实施后见工作区。本文区分**真实引擎验证**、**替身测试**与**未验证**，不把编译成功或 fixture 通过当作能力证明。
> 语料：`test/ocr/corpus/`（21 张合成图，生成器 `scripts/data/make-ocr-corpus.py`，转写见 `manifest.json`）。

## 1 Tesseract 基线

运行：`powershell -File scripts/qa/ocr-bench.ps1`。读取合并后的 `tesseractRoot`，只处理语料，不读桌面、不联网；输出本机 CSV `test/records/ocr-baseline-tesseract*.csv`（gitignored）。

| 项目 | 英语 | 简体中文 | 中英混排 |
|---|---|---|---|
| 引擎 | Tesseract 5.4.0.20240606 | 同左 | 同左 |
| 已运行样本 | 11 | 6 | 4 |
| 平均 CER | 0.52% | 4.88% | 12.74% |
| CER p50 / p95 | 0.00% / 3.25% | 3.49% / 8.33% | 12.50% / 25.00% |
| 单次耗时 p50 / p95 | 136.3 / 217.6 ms | 280.4 / 372.9 ms | 257.0 / 346.3 ms |

英语：代码页与终端造成的错读计入 code 样本（CER 2.4–3.3%）；serif PDF 的 WER 4.9% 主要来自空格与切词，不是字符错误。

中文逐样本：

| 样本 | CER |
|---|---:|
| browser-zh-100-light | 3.49% |
| browser-zh-125-dark | 5.81% |
| browser-zh-150-light | 3.03% |
| pdf-zh-125-dark | 3.03% |
| ui-zh-100-light | 5.56% |
| headline-zh-200-dark | 8.33% |

混排逐样本：

| 样本 | CER |
|---|---:|
| browser-mixed-100-light | 0.00% |
| code-mixed-125-dark | 25.00% |
| ui-mixed-150-light | 13.46% |
| headline-mixed-200-light | 12.50% |

`chi_sim` 来自 `tesseract-ocr/tessdata_fast`（Apache-2.0），文件大小 2,469,156 字节，SHA-256 `a5fcb6f0db1e1d6d8522f39db4e848f05984669172e584e8d76b6b3141e1f730`，本轮由人工放入本机 Tesseract 的 `tessdata`。这是**本机测得的基线**，不是产品默认：安装包仍只带 `eng`，缺少 `chi_sim` 的安装会报告语言缺失。混排的 code 样本是深色 125% 缩放，25% 的 CER 说明必须保留逐样本记录，不能只报平均值。

## 2 RapidOCR-json v0.2.0 审计

验证对象：`hiroi-sora/RapidOCR-json` 发行物 `RapidOCR-json_v0.2.0.7z`，发布于 2023-09-25。下载与运行均在本机私有临时目录，只使用合成图；没有安装、没有加入仓库、没有发送网络请求。

### 2.1 发行物

| 项目 | 结果 |
|---|---|
| 压缩包 | 73,461,693 字节，SHA-256 `7ad9b283d03436c6cd0296723188699299cb4e5cf9140b410c59543aa5793c40` |
| 内容 | `RapidOCR-json.exe` 与 `models/`，没有 DLL，没有 LICENSE 文件 |
| 可执行文件 | 16,849,920 字节，SHA-256 `616bd43a4672ee9fcf4d1ca607d7d9ec413b45d489482064d97e00a7efa9d76e` |
| 签名与版本资源 | Authenticode `NotSigned`；无 FileVersion/ProductName |
| 启动横幅 | `RapidOCR-json v1.1.0`；二进制内没有 `v0.2.0` 字符串 |
| 仓库许可 | 主分支 MIT；v0.2.0 标签树与发行包没有 LICENSE 文件 |
| 模型 | PP-OCRv3/v4 ONNX；上游 PaddleOCR 与 RapidOcrOnnx 均为 Apache-2.0；发行包没有附带许可或 NOTICE |
| 依赖 | 发行包无 DLL，按静态链接处理；未提供 SBOM，无法从发行包锁定 OpenCV/onnxruntime 版本与已知漏洞 |

版本号与标签不一致，信任对象必须按压缩包与可执行文件的摘要记录，不能把 `v0.2.0` 当成二进制内部版本。Lens 不分发该程序与模型，只支持读者自行安装的指定发行物；未签名、无 SBOM 和许可文件仍是必须在界面说明的第三方风险。

### 2.2 协议

v0.2.0 的 README、`api/python/RapidOCR_api.py` 与 `cpp/src/tools.cpp` 都读取 `image_base64`。此前记录的 README 与源码字段不一致并不成立：`base64` 是源码解析后的内部类型名，不是请求字段。

- 启动不带 `--image_path`，等 stdout 出现 `OCR init completed.` 后，写入一行 `{"image_base64":"<PNG 字节的裸 base64>"}`。
- `--ensureAscii=1` 把非 ASCII 转成 `\uXXXX`；解析后恢复 Unicode。不要传 data URI 前缀或命令行图片内容。
- 成功 `code=100`，`data` 是块数组；每块有 `text`、`box`、`score`。`box` 是左上、右上、右下、左下四个点；`score` 是该块字符分数的平均，不是逐词分数。
- `code=101` 表示无文字，`data` 是字符串；`code=205` 表示 JSON 中没有图片；`code=299` 表示未知错误/EOF。错误时 `data` 是错误字符串，不能当识别文本。
- 空图、错误字段与正常图的实测均返回单行 JSON；没有发现多行响应或额外协议字段。

### 2.3 路径、退出与清理

- 可执行文件路径与工作目录含中文、空格时可以启动。
- **模型目录含中文时，进程在初始化前以 `0xC0000409` 退出，stderr 为空**；同一目录改为 ASCII 联接后可以运行。Windows 短路径未缩短中文路径部分，不能作为通用修复。
- 关闭 stdin 后进程不会退出，1.5 秒内输出约 851,967 行 `code=299`，占满一个核心；Lens 必须在得到结果或取消后主动终止并 `wait`，不能发送 Paddle 的 `exit`，也不能等 EOF 收尾。
- 实测终止后退出码为 1，任务列表中没有残留的 `RapidOCR-json.exe`。一次任务一个进程的清理路径可用。
- 首轮只采用标准模型组合，不把任意 ONNX 或命令行参数交给读者：

| 语言 | det / cls | rec | keys | 实测 |
|---|---|---|---|---|
| 英语 | v3 det + mobile cls | `rec_en_PP-OCRv3_infer.onnx` | `dict_en.txt` | 文本可读且保留空格 |
| 简体中文 | v3 det + mobile cls | `ch_PP-OCRv3_rec_infer.onnx` | `dict_chinese.txt` | 中文可读 |
| 中英混排 | v3 det + mobile cls | `ch_PP-OCRv3_rec_infer.onnx` | `dict_chinese.txt` | 可读，但拉丁词之间丢空格 |

`models/configs.txt` 的英语组合写 `dict_chinese.txt`，实测识别为汉字乱码；同一 `rec_en` 配 `dict_en.txt` 才正常。V4 中文模型在英语与混排样本上明显退化（混排只返回 `so`），不作为首轮组合。

RapidOCR 只给块级文本和四边形，没有逐词框。当前截图消费端只使用 `OcrResult.text`，所以块级几何不是本轮阻塞；后续扫描或取词若需要词框，不能把块框均分成假词框。

### 2.4 结论

D 阶段按 [ADR 0132](../adr/0132-rapidocr-model-paths-stay-ascii.md) 交付，采用**模型目录必须是 ASCII 路径**的方案：

- 适配器已实现：固定 v0.2.0 发行物的标准模型组合、每次任务一个进程、读 init/一行响应后主动终止并等待、超时/取消/输出限额、严格 JSON 与四点框解析。
- 设置页新增 RapidOCR 可执行文件、模型目录、显式检测并信任与第三方风险说明；模型目录含非 ASCII 时立即警示、检测按钮禁用、捕获门报 `path-unsupported`，不回退 Tesseract。
- 信任摘要绑定可执行文件、模型目录与当前语言模型文件；路径或语言改变、文件被替换都会撤销信任，必须重新检测。
- 自动策略只考虑 Tesseract；RapidOCR 只能明确选择，不因目录里存在程序而自动运行。
- 未把 RapidOCR 加入安装包；它仍是读者自行安装的第三方程序。发行物未签名、内部版本与标签不一致、缺少模型许可文件与 SBOM 的风险仍在设置页说明。

验证：`OcrRapidOcr.*` 10 例、`RapidOcrProbe.*` 3 例（真实发行物）、QML 非 ASCII 拒绝与检测失败 2 例；真实英语页返回行/框且无假词框，空白页为 `EmptyText`，取消与零时限在启动前返回。中英混排仍会丢拉丁词间空格，这是发行物识别质量，不是适配器静默改写。

## 3 本轮验证分层

| 层 | 证据 | 状态 |
|---|---|---|
| 合成语料 + Tesseract 英语基线 | CSV 与汇总 | 已取得 |
| Tesseract 中文 / 混排基线 | 本机 `chi_sim` + CSV | 已取得；不是安装包默认 |
| RapidOCR 发行物审计 | 摘要、签名、许可、协议、路径、退出 | 已取得 |
| RapidOCR 适配器 | `OcrRapidOcr.*` 10 例；`RapidOcrProbe.*` 3 例真实发行物；QML 路径拒绝/检测失败 2 例 | 已通过；ASCII 路径限制 |
| TSV 解析、坐标、限额、置信度 | `OcrTsv.*` gtest | 已通过 |
| 真实 Tesseract 结构化结果 | `OcrEngineProbe.*` integration | 已通过 |
| 本地路由与旧配置迁移 | `OcrLocal.*` / `OcrMigration.*` / `CaptureDuty.*` | 已通过 |
| 在线请求与零上传断言 | `OcrSpace.*` / `CaptureDuty.*`（注入传输） | 已通过；未发送真实请求 |
| 设置与确认 UI | `lens_qtest_surfaces` 的 `CaptureSettings::test_*`；OCR 两页、RapidOCR 区与确认卡的离屏快照 | 已通过；确认卡预览源只在应用启动时注册，未做真机像素检查 |
| 安装产物中的 Tesseract TSV | [安装产物检查](ocr-installer-2026-10-28.md) | payload/portable 与安装目录已通过；setup 升级/安装/卸载与残留清理已复验 |
| 真实在线 OCR.space 往返 | 未获授权 | 未验证 |

## 4 未验证与下一步

- RapidOCR 已按 ASCII 路径限制交付；中英混排的拉丁词间空格仍会丢，属于发行物识别质量，后续在扫描质量阶段评估。
- 安装产物已构建并验证 payload/portable 与安装目录的 TSV、`chi_sim` 排除与密钥/截图边界；1.7.1 → 1.9.0 升级、卸载、重装已实际执行，profile 保留；修复后 1.9.0 → 1.9.0 的运行中升级、运行中卸载拒绝、干净卸载（含空目录与 `Run\Lens` 清理）和重装已复验。详见[安装产物检查](ocr-installer-2026-10-28.md)。
- 真实 OCR.space 请求、真实密钥、计费与延迟仍需单独授权；离线替身测试不能代替。
- 确认卡的真实桌面预览与 OCR 设置页的真机视觉检查未做；离屏快照不注册应用图片源，不能证明预览路径。
