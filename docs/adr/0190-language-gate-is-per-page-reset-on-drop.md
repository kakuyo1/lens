# 0190 语言门按页判定：丢页即重置，换页先读自己的语言

- **状态**：accepted（2026-11-17）
- **背景**：0187 的语言门把 `foreign_` 与专名集 `names_` 按 track 记，丢页重取（`lose()`）与 `page_.reset()` 都不重置。真机日志坐实后果：`00:39:55` 切到法语页立刻画出 `pas tromp` / `ses temp` / `lien`，直到 `00:40:06` 才 `held: the page reads non-English`——约 10 秒里新页继承旧英语页的判定，法语词照画；专名集合同样过期，新页的小写专名也漏画。
- **决定**：把五处 `reader_.drop()` ＋ `page_.reset()` ＋ `cacheWarm_ = false` 收拢成 `dropPage()`，并在其中重置 `names_` / `namesLoaded_` / `foreign_` / `namesAt_`，让换页回到冷启动的语言门路径：新页必须先读自己的文本、拿到自己的语言判定与专名集才画词。重置 keyed 在 “页被丢弃” 而非 “丢页重取”——`lose()` 不动 `page_`，滚动丢页（0189）保留页与判定，不受影响。
- **后果**：每次换页多一次名字读取（冷启动本就付这笔），换取换页即正确的语言判定。确定性用例 `AFreshPageAfterADropReadsItsOwnLanguageBeforeDrawing` 钉住：英语页画线 → tab 切换丢页 → 法语页接管 → 不画词、`held`。TrackCore 12 用例全过，acq-cost 臂不变（首线 86／ 读尽 94 次缝调用），稳定页获取成本无回归。残留：池里漏进的专名 / 缩写（`taiwan` / `matt` / `dev` / `ret` / `undefined`，ECDICT 既有词频又有释义，池判据剔不掉）是数据问题，另起一刀。
- **被排除的做法**：
  - 在 `lose()` 重置：每次滚动丢页都重读名字（约 300 ms），加重刚收尾的滚动 churn（0189）。
  - 按页身份比较再重置：`Page` 缝未暴露身份，且丢页后缓存已清，无法可靠判定是否同页。