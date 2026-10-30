# TODO by agents

> 只写还没做的。每条说清还剩什么、从哪下手；做完直接删，不记日期与过程。

## 单词发音（ADR 0134）

- **本地输出不缓存**：每次按下都重新合成。按实测决定是否进缓存（`src/app/platform/local_voice.h`）。
- **Cambridge Dictionary 暂缓**（`docs/adr/0138`）：词条页的音频地址可以解析，但站点的 Cloudflare 对非浏览器 UA 返回 403，只有浏览器 UA 才通过。未绕过。需要决定：放弃，还是接受伪装浏览器 UA（不建议）。

自动播放只对 `cardKind === "selection"` 的单词卡生效（`Bubble.qml` 的 `show()`），预览卡、悬浮复习卡与宠物作答卡都不读；句子、实体、译文与拆解卡没有喇叭（`BubbleCard.qml` 的 `speakable`）。

## `lens_qtest_pet` 的旧断言

该目标不在 `TEST.md` 的目标表里，`aPreviewedSceneExposesItsSheetsAndAlignment` 仍按旧狗的脚点（57，84）与场景对位 -1.5 断言，实际脚点是（51，84）。先确认狗在共享场景里是否仍对位，再改测试或场景。另有两条随数据漂移过期：`threeClicksMakeTheDogAngry` 仍点三次，而 `dog/animations.json` 的 `angryClicks` 已是 4；`accessoryCatalogueAndWornSlotsAreExposed` 仍数 18 个动作，实际 19（六场景之后新增了非场景动作）。三条都在动测试或数据之前先确认真机读感。

## 玻璃主题的 CPU 占用

只走 GDI 的屏（标准档、Windows 10、两个快速源都被拒的机器）在桌面静止、设置面板常开时约 10% 单核，主要是每秒 15 次的 GDI 读取。能复制或能被 Windows Graphics Capture 读取的屏已不再付这笔（ADR 0087）；剩下的按顺序试：空闲时暂停、按负载退帧、缩小读取区域。读数与判色见 `docs/adr/0040`、`docs/adr/0041`。

## 档位词书数据

档位要落地，需要 8 份词表：B1–B2 / C1–C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / 雅思 / TOEFL。现在 `data/wordlist.txt` 是纯词频表，不含考试标签，只能按词频阈值近似，会误判：CET-6 词书含大量非高频词，词频前列也混着 CET-4 之外的专业词。确定来源（自备 txt / 公开词表）后，把 known-set 预置从 “词频阈值” 改为 “词书集合”。见 `docs/history/PHASE1.md` 4.1。

## 不规则表把 offer 归给了 off

`data/irregulars.tsv` 有 `offer → off`，命中即裁定，`offer` 的请求、缓存、气泡标题与已会标记都按 `off` 处理。核对 WordNet 的 `verb.exc` / `noun.exc` / `adj.exc` / `adv.exc`：源里没有就删掉这一行并重跑 `scripts/data/gen_irregulars.py`；源里有，就把 `docs/history/PHASE1.md` 4.1 的反例改掉。

## 更新源镜像

检查更新只走 GitHub Releases，地址在 `data/update.json`，国内需代理。实测国内可达的镜像（版本接口与发布页两处），定下后只改 `data/update.json`，并在 `SECURITY.md` 写明出站地址。

## 账号与同步的扩展

- **其他登录方式**：ADR 0042 只定 GitHub 设备登录。评估微软账号（OneDrive）、谷歌账号、手机号或邮箱验证码；后两者需要服务端发码，属于托管后端，要另开 ADR，并实测国内短信与邮件的可达性与费用。

# TODO by user

## 气泡在多词错峰弹出时的对齐与去重策略

## 自定义安装页面

## level 加上自定义词表支持

## 卸载时可选是否删除所有用户信息
