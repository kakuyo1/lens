# 扫描池清洗报告

## 1、报告信息

| 项目 | 内容 |
| --- | --- |
| 回答的问题 | 自动扫描可标池里哪些专名/缩写/外来碎片该清掉，清掉后真实画线少多少，覆盖与选区是否受影响 |
| 生成日期 | 2026-11-17 |
| 被测数据 | `data/scan-words.txt`（可标池）、`data/wordlist.txt`（覆盖表） |
| 真实样本 | 四份滚动日志 `logs/lens*.log` 的 `scan: N candidate(s) read on the page`，1,449 次画线、602 个唯一候选 |
| 规格样本 | `test/eval_corpus.json`，3,005 段 |
| 构建源 | Open English WordNet 2022 + ECDICT `stardict.csv`（232,668,349 字节，2025-01-02），与 0186 同一份 |
| 运行方式 | `scripts/data/build-wordlist.py --exclude data/scan-exclude.txt`；`lens_gtest_unit` 两个 CoreTest 用例，Debug（`build-ninja`） |

数据是确定性产物：同输入同字节。无 `--exclude` 重建逐字复现现船两文件，带 `--exclude` 两次重建字节一致，故不按多轮记区间。

## 2、结论

**两阶段共清掉 1,094 个噪声词（连带词形 2,065），覆盖与选区通道完全不动**。池从 80,734 降到 78,669，覆盖表 152,058 字节不变。阶段 1（日志证据驱动）清 27 词，真实日志少画 54 次（3.7%）；阶段 2（全池系统清扫）清 1,067 词，是预防性长尾清洗——这些词在当前日志里出现 0 次，清的是未来会在人名/地名/缩写页画线的池噪声。eval 语料 3,005 段 0 问题。

| 量 | 清洗前 | 阶段 1 后 | 阶段 2 后 | 总差 |
| --- | ---: | ---: | ---: | ---: |
| 覆盖表词数 | 152,058 | 152,058 | 152,058 | 0 |
| 可标池词数 | 80,734 | 80,687 | 78,669 | −2,065 |
| 池基础词 | 42,870 | 42,845 | 41,778 | −1,092 |
| 池词形 | 37,864 | 37,842 | 36,891 | −973 |
| 排除表词数 | 0 | 27 | 1,094 | +1,094 |
| 日志在池唯一候选 | 461 | 434 | 434 | −27 |
| 日志画线受影响次数 | 54 | 0 | 0 | −54（3.7%） |

阶段 2 池基础词 −1,067、词形 −951（合 −2,018）；基础词总差 −1,092 = 阶段 1 的 27 + 阶段 2 的 1,067。

## 3、排除的 27 词与判据

判据：专名主导（特定人/地/朝代/机构/星/星座，无一般类义）、缩写与缩略（无独立一般义）、外来碎片（无自然化英语义）排除；有一般英语义则保留，即便同时也是人名、科技词或罕见词。证据是 OEWN 义项加 ECDICT 释义。

| 类 | 词 | 数 |
| --- | --- | ---: |
| 专名 | joyce jensen heisenberg owen clark catherine tate jong wei gao taiwan mira wang kee gemini libra | 16 |
| 缩写/缩略 | cli feb sys ses lib dev | 6 |
| 外来碎片 | que las chanson livre | 4 |
| 叹词碎片 | hoo | 1 |

连带出池的 20 个词形：catherines chansons clis devs febs gaos heisenbergs jensens jongs joyces kees libras libs livres miras ques taiwans tates tatest wangs。

**边界保留**（有一般义，按判据留，透明记此）：`fil`（伊拉克钱币）、`quod`（监狱俚语）、`boeuf`（OEWN 食物义项，法语拼写）、`ret`（沤麻动词）、`matt`（暗淡的）、`milo`（蜀黍）、`comparator`/`reverb`/`tromp`/`moonrise`。0190 曾把 `matt`/`ret`/`undefined` 一并当噪声，OEWN 与 ECDICT 证它们有真实义项，故保留。

## 4、判别方法的一个坑

OEWN 的 `noun.person`/`noun.group`/`noun.location` 是语义类别，不是专名标记：`beginner`/`dweller`/`sherpa`/`superpower`/`wasteland` 都落在这些类别里，却是普通名词。按 lexname 自动判专名会误删它们。须读 gloss 是否指特定实体（如爱尔兰作家、鲸鱼座中的星指特定实体，是专名；新手、居住者指一类，是类义）。

## 5、全池系统清扫（阶段 2）

对整池 42,845 个基础词做义项级判定（词形随基础词出池，不单独扫）。信号与流程：

1. **硬保留**：有任何 OEWN 小写 synset（一般英语义）的 37,384 词直接保留，占 87%。
2. **flagA**（在 OEWN 但每个 synset 都大写）1,322 词：gloss 全指特定实体（传记/地名/组织/星座）→ 自动排除 793；有一般/类义 → 保留 367；其余 162 进手审。
3. **flagB**（不在 OEWN）：ECDICT 译文义项全为人名/缩写/外来 → 候选 190；译文有一般义 → 保留 3,618；译文无词性标记的 331 多为真实词与屈折形（caught/children/alumni/antennae），整桶保留。
4. **手审纠偏**：自动排除集剔回 21 个误报（真实屈折形译文混入同形专名义，如 jarred/nicknamed/frescoed/holstered/pears/rivers/weeks/welling；以及 alma/polly/desiderata/milieux/kroner/extraordinaire/saut/ye 有一般义）；ambiguous 堆排除 105 个明确专名/缩写，保留 58 个名源普通名词。

阶段 2 净排除 1,067 词（自动 962 + 手审 105），按来源：OEWN 全大写特定实体 793、不在 OEWN 的人名/缩写 169、ambiguous 特定实体 105。

**名源普通名词保留**（有一般类义，非特定实体）：`viking`（海盗）、`pharaoh`/`shah`（头衔）、`cinderella`（灰姑娘）、`romeo`（情人）、`samson`（大力士）、`boswell`（作传者）、`milquetoast`（胆小者）、`fagin`（教唆犯）、`lazarus`（乞丐）、`cajun`/`chicano`/`cornishman`/`saracen`（族裔/郡人）、`druid`/`druze`/`satanist`/`trotskyite`（祭师/教徒）、`ladyship`（夫人头衔）、`francophile`（亲法者）、`godhead`（神性）、`who`（代词谁）、`fed`（联邦探员）、`sung`（sing 过去分词）、`tara`（再会）等。

**预防性**：这 1,067 词在真实日志里出现 0 次。阶段 1 清的是日志已见的噪声，阶段 2 清的是池里尚未被读到、但会在人名/地名/缩写页画线的长尾噪声。

## 6、残留

- 全池清扫已做。余下是保守保留的名源普通名词与边界词（`fil`/`quod`/`boeuf`/`ret` 等），以及不在 OEWN、译文无词性标记的偶发姓氏（`bates`/`cheng`，量极小，可接受残留）。
- 语言门（0187/0190）仍把非英语页整页 held，本刀只处理英语页上的词级噪声，不重议语言门与词表换源（0186）。
- 覆盖表里这些词仍在，选区按单词解释不受影响；`wordlist-extra.txt` 不动。

## 7、来源与复现

```
py -3 -I scripts/data/build-wordlist.py ^
    --wordnet <wordnet2022-dir> --ecdict <stardict.csv> ^
    --output data/wordlist.txt --scan-pool data/scan-words.txt ^
    --extra data/wordlist-extra.txt --exclude data/scan-exclude.txt
```

验收：`scripts/data/check-eval-corpus.py`（3,005 段 0 问题）；`lens_gtest_unit` 的 `CoreTest.ReplaysTheSampleCorpus` 与 `CoreTest.TheScanPoolDropsNamesButASelectionStillExplainsThem`。决定与取舍见 `docs/adr/0192-scan-pool-exclude-list.md`。