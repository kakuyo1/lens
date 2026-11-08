# 0166 判定投影收进一处：判定到虚线，一次作答

- **状态**：accepted
- **背景**：评审（2026-11-08）卡 3。判定库对这个词说什么，这个问题在四个 module 里各自重算、各自拼成 `new`/`known`/`mastered` 字符串穿过 QML seam：`filterWords` 逐候选判态、`ScanSession` 的判定接线（`judgeAdded` 与 `onVerdictChanged` 各取一次 known/mastered/band 三元组再喂同一个 `judgeSurface`）、铅笔层 `Word::verdict` 的字符串传递、词汇弹窗 `words_view` 从两份集合再拼一遍；气泡徽章与复习卡又在 `explanation_duty` 里第五处拼，`mark()` 里第六处把字符串解析回判定值。ADR 0159 定了语义，ADR 0162 定了键，ADR 0165 把判定与失效搬进了 `ScanSession`，但投影散在四处没人收。0165 落地时留了一句 “投影的收拢是另一张卡”，就是这张。
- **决定**：
  - **判定一处作答**：`filter_core` 增 `stateOf(surface, known, mastered, difficulty)`——读者自己的判定优先，档位频段与词书其后，词表不收的词只由读者判定说话；`filterWords` 的两条判态路径（散文门与孤词豁免）都改走它。逐表面判定 `ScanSession::judgeSurface` 收拢为单参入口，判定库三元组的取数搬进函数体内：帧路径与 verdict 失效路径共用的不只是纯函数，还有喂它的接线。
  - **token 一处铸造**：新增 `src/app/duties/verdict_view.h`（纯头文件，无 QObject）：`verdictToken`（`WordVerdict` 与 `CandidateState` 两个重载）、`verdictToken(store, lemma)`（气泡徽章与复习卡读的库内答案）、`verdictOf`（两份集合到判定值，弹窗与导出口径）、`verdictFromToken`（QML 按下的解析，`mark()` 的字符串不再自己认）。字符串协议本身不改——QML 依旧读 `verdict`/`status` 的 `new`/`known`/`mastered`——改的是这些字符串只在一处出生，QML 只读一个答案。
  - **六处落点改线不改行为**：`capture_duty` 的 scanPlaced payload、铅笔层 `Word::verdict` 默认值与 `verdictChanged` 的 mastered 比较、`explanation_duty` 的两处徽章与 `mark()` 的解析与回填、`words_view` 的行判定，全部改读 `verdict_view`。行为逐字保真：token 集合、语义口径（弹窗与徽章不含档位、扫描线含档位）、日志格式均不动。
  - **测试**：`filter_core_test` 增 `StateOfIsTheOneAnswerToWhichLine`（频段词、判定越过频段、孤词豁免口径、屈折覆盖、表外词）；新增 `verdict_view_test.cpp` 四条进 `lens_gtest_unit`（token 铸造、库内答案、按下解析与往返、集合口径）。既有 `ScanSession.*`、`WordsView.*`、`CaptureDuty.*` 不动，为行为保真门禁。
- **后果**：
  - 新增一个判定投影的落点，即未来要读判定的地方，只需调一个函数；新增一种 token 的叫法只需改 `verdict_view.h` 一处。从判定库到 QML，判定到虚线这条链只剩一次字符串铸造，且首次有测试钉住。
  - 语义未动，UI-SPEC 与 GLOSSARY 不改；0159 的口径（弹窗与徽章不读频段）原样保留——本卡收拢投影，不重裁语义。
  - `AskOrigin`、判定失效的时机（0165 的契约）都不在本卡范围；铅笔层 Alt 气泡的 origin 保真是评审卡 4，另行处理。
