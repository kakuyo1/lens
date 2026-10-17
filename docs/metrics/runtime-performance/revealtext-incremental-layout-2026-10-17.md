# RevealText 增量排版

## 1、范围

`RevealText.qml` 的流式答案每到一个 chunk 就整段重排：重新分词、逐字符测量、重建 `pieces` / `positions` 两个数组，并让 Repeater 销毁、重建全部字符 delegate。单个 chunk 的成本随已到达的文本线性增长，chunk 数也随文本增长，因此一条答案的总排版成本是 O(n²/chunk)。

本次改动把排版改成增量：

- 字符 delegate 的 model 从普通 JS 数组换成 `ListModel`。新 chunk 只 `append` 新行，已落定的 delegate 不重建。
- 只重排最后一个 token（一个词，或一段空白）：append 只可能增长它，或接在它后面。词增长后越过行宽时，从这个词自己的起点重新应用换行规则。
- 宽度、换行模式、字体变化仍走全量重建。
- `positions` 仍按文本下标保存尾部模糊要用的字符位置；`layoutRevision` 让尾部绑定在就地修改后重新读取。

## 2、方法

用 `lens_qtest_components` 里的临时 QML 用例（`QT_QPA_PLATFORM=offscreen`）直接给 `RevealText.text` 逐块赋值，用 `Date.now()` 包住整条流，统计总耗时、每 chunk 平均耗时和最后一个 chunk 的耗时。每项取多轮的中位数，抵消 1 ms 计时精度和机器噪声。

| 项 | 值 |
| --- | --- |
| 构建 | `build-ninja-release`（RelWithDebInfo），QML 经 qmlcachegen 预编译，与应用发布路径一致 |
| 长文本 | 877 字符的英文释义 |
| 短文本 | 106 字符的典型释义 |
| 分块 | 17 字符/块、63 字符/块 |
| 轮数 | 长文本 5 轮，短文本 9 轮 |
| 系统 | Windows 11 10.0.26200，AMD Ryzen 7 7735H |
| Qt | 6.9.0 MSVC 2022 x64 |

赋值循环是同步的，计时只覆盖排版本身，不含事件循环里的渲染；这正是一个 chunk 到达时占住 GUI 线程的那段时间。

## 3、结果

### 877 字符，17 字符/块（52 块）

| 指标 | 改动前 | 改动后 | 变化 |
| --- | ---: | ---: | ---: |
| 整条流总耗时（中位数） | 2,035 ms | 102 ms | 20.0× |
| 每 chunk 平均 | 39.1 ms | 1.96 ms | 20.0× |
| 最后一个 chunk | 79 ms | 3 ms | 26× |

### 877 字符，63 字符/块（14 块）

| 指标 | 改动前 | 改动后 | 变化 |
| --- | ---: | ---: | ---: |
| 整条流总耗时（中位数） | 613 ms | 91 ms | 6.7× |
| 每 chunk 平均 | 43.8 ms | 6.5 ms | 6.7× |
| 最后一个 chunk | 96 ms | 9 ms | 10.7× |

### 106 字符，17 字符/块（7 块）

| 指标 | 改动前 | 改动后 | 变化 |
| --- | ---: | ---: | ---: |
| 整条流总耗时（中位数） | 45 ms | 10 ms | 4.5× |
| 每 chunk 平均 | 6.4 ms | 1.4 ms | 4.5× |
| 最后一个 chunk | 12 ms | 2 ms | 6× |

改动后，877 字符的两种分块总耗时都落在 90–100 ms：成本取决于文本总长（已落定的字符只排版一次）和最后一个 token 的重复排版次数，不再取决于块数或已显示长度。改动前，块数越多总耗时越高（2,035 ms 对 613 ms），这是每个 chunk 都重排整段的直接证据。

最后一个 chunk 的数字是读者最能感觉到的：长释义在最后一块到达时，改动前要停住 79 ms，改动后是 3 ms。

## 4、正确性

`test/qtest/components/tst_revealtext.qml` 新增 `test_anExtendedLayoutMatchesOneLaidOutWhole`：同一段文本分 5 个 chunk 流式写入，与一次性整段写入的字段逐字符比较文本、x、y，必须完全一致。用例里有一个在行尾继续增长、随后换行的词。该文件 8 个用例全部通过。

另做了临时压力验证（测量后已删除）：5 段文本（含显式换行、连续空格、行尾空格、超长连字符词）× 3 种宽度 × 7 种分块模式（含每块 1 字符）共 105 条流，全部与整段排版一致；还验证了流到一半改变宽度（触发全量重建）后继续追加，结果仍与整段一致。

## 5、结论

1. 流式过程中不再全量重建：已显示的字符既不重新测量，也不重新创建 delegate。
2. 一条 877 字符答案的排版总成本从 2.0 秒降到 0.1 秒，单 chunk 从 39 ms 降到 2 ms，读者可感知的最后一帧停顿从 79 ms 降到 3 ms。
3. 未改动：揭示期间每个已落定字符的 `visible` 绑定仍随光标每 16 ms 重新求值（审计 P0-1 提到的另一项）。本次测量只覆盖 chunk 到达时的排版成本；逐帧绑定成本是否值得按词/行合并已落定字符，需要 QML profiler 的逐帧数据另行判断。
4. 手测检查点：让模型返回一条含词源或例句的长答案，观察文字逐字出现，换行与改动前一致，行尾没有字符跳动或重复；模糊尾巴仍只在最新几个字符上。开启“减少动态效果”时，缓存弹出的卡片应整段出现。

## 6、复现

测量用的临时用例核心如下（放进 `test/qtest/components/`，以 `lens_qtest_components.exe -input <file>` 运行；用例在测量后已删除）：

```qml
import QtQuick
import QtTest
import Lens
import "../"

Item {
    width: 400
    height: 300
    TokenHost { id: host }
    Component {
        id: revealComponent
        RevealText { width: 280 }
    }

    TestCase {
        when: windowShown
        readonly property string longAnswer: "From the Middle English term, borrowed through Anglo-Norman French from the classical Latin root, the word passed into early modern usage by way of legal and ecclesiastical documents, where scribes wrote it in the margins beside borrowed phrases and glossed it for readers who knew no Latin; the spelling drifted with each copying, consonants doubled and vowels shortened, until the printers of the sixteenth century fixed the form we recognise today, though regional dialects preserved older pronunciations in compound words and family names long after the standard had settled. Scholars disagree about which manuscript first carried the sense that survived, but the record is consistent enough to show a gradual narrowing from a broad legal meaning to the particular everyday one, and the examples below trace that movement through letters, inventories and commonplace books."

        function test_streamingChunkCost() {
            const reveal = createTemporaryObject(revealComponent, host.contentItem,
                                                 {animated: true, characterMs: 35});
            const chunks = [];
            for (let end = 17; end < longAnswer.length + 17; end += 17)
                chunks.push(longAnswer.substring(0, Math.min(end, longAnswer.length)));
            const totals = [];
            for (let trial = 0; trial < 5; ++trial) {
                reveal.text = "";
                wait(0);
                const started = Date.now();
                for (const chunk of chunks)
                    reveal.text = chunk;
                totals.push(Date.now() - started);
            }
            totals.sort(function (a, b) { return a - b; });
            console.log("BENCH total_ms=" + totals.join(","),
                        "median=" + totals[Math.floor(totals.length / 2)]);
        }
    }
}
```

```powershell
$env:PATH = "B:/qtt/6.9.0/msvc2022_64/bin;$env:PATH"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_FORCE_STDERR_LOGGING = "1"
./build-ninja-release/test/qtest/lens_qtest_components.exe `
  -input test/qtest/components/tst_revealbench.qml
```

改动前的数字用同一用例在改动前的 `lens_app` 上测得；两份结果都取同一台机器的同一构建类型。
