# 0135 本地音色：Kokoro 为自动，LJSpeech 可选，按模型整体下载

- **状态**：accepted。修订 [ADR 0134](0134-pronunciation-is-word-only-and-system-first.md) 的本地档：音色按模型下载，不再按口音分槽；其余取舍不变。
- **背景**：0134 定了本地档的契约，但没有选定模型。选型时核实了三件事。其一，sherpa-onnx v2.0.0 起不再使用 espeak-ng（GPL-3.0），v2.0.1 的 Windows 运行时没有 espeak 函数符号，导入表里也没有 espeak 的 DLL；英文靠词典读音，不需要 espeak 数据。其二，Kokoro int8 multi-lang v1.1 的英文音色只有三个：美式 `af_maple`、`af_sol`，英式 `bf_vale`（女声）。其三，词典外的词会让引擎失败，没有音频。LJSpeech（VITS）只有一个美式音色。
- **决定**：
  1. 两个模型，各自整体下载。Kokoro int8 v1.1（约 180 MB）是自动档；LJSpeech int8（约 41 MB）只在读者明确选择时使用，只支持美式。
  2. 自动档只走 Kokoro，不静默切到 LJSpeech。Kokoro 未下载时自动档静音。这与 OCR 的自动策略（ADR 0133）是同一结构。
  3. 运行时是 sherpa-onnx v2.0.1 Windows x64 shared MD Release，随安装包带 `sherpa-onnx-c-api.dll` 与 ONNX Runtime 1.28.2（MIT）。不引入 piper，它依赖 GPL 的 espeak-ng。
  4. 设置键 `speechLocalEngine` 取 `auto` / `kokoro` / `ljspeech`，缺省 `auto`，不同步。音色文件只在本机，选择它的理由也只在本机。0134 写过音色状态随本地档落盘；改为不落盘，是否已下载由 `voices\` 下的文件推导，文件被删后状态立即正确。
  5. 下载逐文件从固定修订版取得，每个文件校验大小与 SHA256，校验通过后才改名到位，落 `%APPDATA%\Lens\voices\<模型>\`。之后每次按下只查大小，不再重算摘要。
  6. 合成在单线程工作池上做，引擎按模型与口音常驻。本地输出不进缓存，每次按下重新合成。
- **后果**：
  - 英式只有一个女声，LJSpeech 完全没有英式；选英式又选 LJSpeech 时设置页写明静音，不请求，也不切换。
  - 词典外的词静音，不弹通知，与 ADR 0107 一致。
  - 切换口音会重新加载引擎，因为 Kokoro 的词典按口音选。
  - 模型文件与运行时都固定到版本，改版本要重新做选型，不是刷新。
  - 运行时 DLL 随 `lens.exe` 一起分发，仓库只带 `c-api.h`（Apache-2.0），因为发布包里没有头文件。
  - 词典文件的许可没有逐条核实。Kokoro 仓库的 LICENSE 为 Apache-2.0。
  - 本地输出不缓存，每次按下都要合成，作为已知限制记在代码注释里。
