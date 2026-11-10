# FilterCore 吞吐复测

## 对象与口径

同一个 `CoreTest.FilterWordsThroughput` 用例，两套构建树，各跑三轮。
`ninja-qt6-perf` 开 `LENS_ENABLE_PROFILE=ON`，`ninja-qt6-release` 关着插桩；
两棵都是 RelWithDebInfo，只有宏不同，所以两者之差就是插桩的价。
用例把 `test/eval_corpus.json` 的全部 excerpt 跑 200 轮，known 与 mastered 都给空集，
是最坏情况；唯一断言是候选数大于零，时间只打印不判。

```
cmake --preset ninja-qt6-perf
cmake --build build-ninja-perf --target lens_gtest_perf
cmake --build build-ninja-release --target lens_gtest_perf
```

输入这一次是 3005 条 excerpt，词表 168205 条、不规则屈折 5756 条、非正式拼写 115 条，
数字来自运行日志。

## 结果

| 树 | 轮 | 中位 ms | 最小 ms | 最大 ms | 每 excerpt ms | 200 轮候选数 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 插桩开 | 1 | 21.728 | 20.7702 | 44.074 | 0.00723062 | 3470600 |
| 插桩开 | 2 | 21.5483 | 21.0139 | 54.7628 | 0.00717082 | 3470600 |
| 插桩开 | 3 | 22.4278 | 20.8872 | 46.7766 | 0.00746349 | 3470600 |
| 插桩关 | 1 | 19.6441 | 18.8617 | 29.527 | 0.00653714 | 3470600 |
| 插桩关 | 2 | 19.557 | 19.1482 | 31.9361 | 0.00650815 | 3470600 |
| 插桩关 | 3 | 19.5267 | 18.9136 | 30.2654 | 0.00649807 | 3470600 |

插桩开的中位数区间是 21.548 到 22.428 ms，插桩关是 19.527 到 19.644 ms。
按两端配对算，插桩抬高约 10% 到 15%，与
`profiling-and-coverage.md` 记的约 16% 是同一量级，本次没有变贵得离谱。
要报吞吐就用关插桩那三行，开插桩那三行只用来看时间花在哪。

第三轮插桩开的汇总表：

| timer | count | total ms | mean ns | max ns |
| --- | ---: | ---: | ---: | ---: |
| `filterWords` | 601000 | 5055.073 | 8411 | 3642400 |
| `lemmatize` | 7092600 | 1923.581 | 271 | 206200 |
| `filterWords/tokens` | 4259800 | | | |

601000 次外层调用正好是 200 轮乘 3005 条 excerpt；平均每条约 7.1 个 token、
11.8 次 `lemmatize`。内层次数是外层的十一点八倍，而单次均值只有 271 ns，
所以内层计时器的固定开销不能被当作业务时间去读，方法文档里那条仍然成立。

## 读法

六轮的候选数完全相同，都是 3470600，说明 11-08 修过的 `mastered` 集合签名没有
再漂移，工作量也没有被 hard filter 悄悄削掉。这是这次复测最想确认的一件事。

不要拿这些数字去和 2026-10 月的 4.36 到 7.04 ms 直接比：
`docs/metrics/code-quality/selection-classification-accuracy-2026-10-04.md` 里
样例集还是 1001 条，`idle-memory-2026-10-05.md` 里词表是 88918 条，
两者现在分别是 3005 与 168205；中间又有 ADR 0159、0161、0162 改过
verdict 与词形覆盖。口径和输入都变了，所以这份是新的起点基线，不是退化结论。

本机噪声照旧：插桩开那三轮的 max 在 44 到 55 ms 之间，是 min 的两倍多。
报区间，不报点值，也不对时间下断言。

## 没覆盖到的

- 只量了 FilterCore。扫描帧、玻璃采集、QML 场景与设置投影仍要按
  `docs/qa.md` 的循环负载与 2026-10-03 的 QML profiler 场景各自复测。
- `lens_gtest_glass_probe` 要可复制的桌面会话，CI 没有，这次没跑。
- 内存侧的同一次复测在
  [`resource-footprint/idle-sample-2026-11-10.md`](../resource-footprint/idle-sample-2026-11-10.md)，
  那只是一分钟只读采样，不是长稳结论。

## 数字来源

`logs/perf-20261110.txt` 与 `logs/perf-noinstrument-20261110.txt`（gitignored）。
数字照抄原始输出，没有重算或补齐。
