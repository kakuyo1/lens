# LLM 协议
> 权威：当前线上协议。**实际生效的定义在 `data/llm/` 目录**，代码只组装与校验，不内联任何提示词或字段名；历史取舍见 `docs/adr/`。

服务商、模型与端点由 `data/llm/catalog.json` 驱动，默认是 DeepSeek；解释请求使用 OpenAI 兼容的 `POST {baseUrl}/chat/completions`，请求头为 `Content-Type: application/json` 与 `Authorization: Bearer <API-KEY>`。模型列表不问服务商：**全部服务商共用 `modelSource` 声明的那一个源**，回答按厂商前缀切分。当前目录包含 DeepSeek（`deepseek-flash`、`deepseek-v4-pro`）、OpenAI（`gpt-4.1-mini`、`gpt-4.1-nano`）和 Custom；Custom 允许在设置中填写模型与 HTTPS 服务地址。价目独立放在 `data/llm/pricing.json`。

API key 只保存在读者的 `%APPDATA%\Lens\settings.json`，不进入安装目录、OCR payload 或日志；安装包只承载程序、静态数据与 `ocr/` 下的 Tesseract runtime 和英文 traineddata——读中文要用的 `chi_sim.traineddata` 由读者自己放进那个数据目录（UI-SPEC.md 4.11）。

## 1 通道

请求属于哪个通道在发送前就已经定好，通道决定用哪套提示词与哪套响应 schema。文件按通道一一对应：

| 通道 | 请求 | 响应 schema | 状态 |
|---|---|---|---|
| 单词 | `data/llm/request.word.json` | `data/llm/response.word.schema.json` | 已落地，四字段含 IPA |
| 实体 | `data/llm/request.entity.json` | `data/llm/response.entity.schema.json` | V3 已落地，两字段（en / zh） |
| 句子 | `data/llm/request.sentence.json` | `data/llm/response.sentence.schema.json` | V3 已落地，两字段 + 翻译 / 解释两个预设 |
| 译文 | `data/llm/request.translation.json` | `data/llm/response.translation.schema.json` | 中文选区专用，只回 `en` 一个字段 |
| 一瞥 | `data/llm/request.peek.json` | `data/llm/response.peek.schema.json` | 气泡按住地球的母语一行，只回 `line` 一个字段，不流式 |
| 义项 | `data/llm/request.sense.json` | `data/llm/response.sense.schema.json` | 气泡 Not-the-Sense? 条目的下一个常见义项，至多一个，不流式 |
| 拆解 | `data/llm/request.breakdown.json` | `data/llm/response.breakdown.schema.json` | 句子选区的成分拆解，只回词的下标，不流式，已落地（§3.6） |
| 纠正拼写 | `data/llm/request.spell.json` | `data/llm/response.spell.schema.json` | 单个 token 的拼写修正，只回修正后的词，不流式（§3.7） |
| 纠正语法 | `data/llm/request.grammar.json` | `data/llm/response.grammar.schema.json` | 句子的语法与拼写修正，只回修正后的整句，不流式（§3.7） |

前三条通道由**选区的形状**决定，第四条由**语言**决定：选区里出现汉字（CJK 表意文字）时，它是单词、实体还是句子都不再作数，整段走译文通道，**三种形状共用这一套提示词**（`docs/adr/0044`）。读者要的是中文的英文，词、专名、整句都一样。第五条由**手势**决定：解释语言为英文时按住气泡上的地球，为一个词、句子或实体要一行母语释义（`docs/adr/0112`、`docs/adr/0124`）。第六条由**卡片上的条目**决定：多义关闭时单义卡底部的 `Not-the-Sense?` 条目，为立着的词要一个尚未给出的更常见义项（`docs/adr/0113`）。第七条由**复制抽屉里的 “句子拆解”** 决定，只对句子选区发出（`UI-SPEC.md` 4.3）；它不回显文字，只回词的下标，协议见 §3.6。第八条由**复制抽屉里的 “纠正拼写”** 决定，只对单个 token 选区发出；第九条由**复制抽屉里的 “纠正语法”** 决定，只对句子选区发出。两者都不回显原文，应用持有原文并自行对比修正（§3.7）。注意：第七至第九条的通道由读者按下的动作决定，形状只决定该动作是否出现（`docs/adr/0009` 修订）。

`llm_protocol.{h,cpp}` 加载并校验这些文件：缺文件、非法 JSON、必填值为空一律抛，**不回落内置默认值**——回落会正好掩盖这次抽离要防的漂移。

## 2 请求体

组装后的请求体。`model` 来自当前服务商目录或 Custom 设置，其余字段与默认值都来自该通道的 `request.<通道>.json`：

```json
{
  "model": "deepseek-flash",
  "messages": [
    { "role": "system", "content": "…systemPrompt.template，其中 {outputLanguage} 已替换…" },
    { "role": "user",   "content": "ubiquitous\nresilience" }
  ],
  "response_format": { "type": "json_object" },
  "thinking":       { "type": "disabled" },
  "max_tokens": 4096,
  "stream": true,
  "stream_options": { "include_usage": true }
}
```

- `messages[0]` 是系统提示词，取自所选通道与预设的 `systemPrompt.template`；末句由 `{outputLanguage}` 占位符按语言码替换——解释语言定四条解释通道的收尾行，母语定一瞥的（`outputLanguage.en` / `.zh` / `.zh-Hant` / `.es` / `.ja`）。提示词本身是英文。
- `messages[1]` 是待查内容，**一行一个**。单词只发词根，实体只发实体名，句子只发选中的句子，拆解只发切好的词（§3.6）；发送前做脱敏兜底（邮箱 / 长数字 / URL 掩码成 `<email>` / `<num>` / `<url>`），脱敏只替换行内文字、不增减行数，所以拆解的下标不因此错位。
- `thinking` 必须显式设成 `disabled`：DeepSeek 默认开启思考模式（effort=high），开着会白付 reasoning token 且更慢。思考模式下 `temperature` 无效；`top_p` 仅思考模式生效（有效区间 0.95–1.0），非思考模式固定 1.0。
- `max_tokens` 必须显式设（非思考模式缺省 8K）：JSON 被截断时接口不报错，只把 `finish_reason` 置为 `length`。
- `response_format` 为 `json_object`，因此系统提示词里必须出现 `json` 字样并给出格式示例，缺了模型会一路吐空白到 `max_tokens` 用尽。
- `stream` 为 `true`：答案边写边回，见 §3.4。
- `stream_options.include_usage` 为 `true`：流式接口默认**不回** `usage`，不显式索取则每次请求的花费都记成 0。这一项是 `request.<通道>.json` 里的数据，不写死在代码里；不接受它的服务商可在目录项的 `requestOverrides` 里把 `stream_options` 整块置成 `null` 去掉，代价是那家服务商的花费记不到（`Config::requestOverrides` 的 null 即删除，与 `thinking` 同一条规则）。目录里除 DeepSeek 外都这样配着：没核对过文档的服务商先一律去掉，免得不认这个字段的端点把整份请求判成 400；确认支持后删掉那个 null 即可恢复用量统计。

## 3 响应体：单词

外层是 OpenAI 风格的 envelope，模型真正产出的 JSON 是 `choices[0].message.content` 里的**字符串**：

```json
{
  "choices": [
    {
      "finish_reason": "stop",
      "message": {
        "content": "{\"results\":[{\"word\":\"ubiquitous\",\"ipa\":\"/juːˈbɪkwɪtəs/\",\"en\":\"present everywhere\",\"zh\":\"无处不在的\"}]}"
      }
    }
  ],
  "usage": { "prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0 }
}
```

把 `content` 再解析一次，得到本项目的载荷：

```json
{
  "results": [
    {
      "word": "ubiquitous",
      "ipa": "/juːˈbɪkwɪtəs/",
      "en": "present everywhere",
      "zh": "无处不在的"
    }
  ]
}
```

字段含义以 `data/llm/response.word.schema.json` 为准，那里同时是**校验的唯一真源**——代码从 schema 的 `properties.results.items.required` 读出必填字段名，所以往 schema 里加一个字段，校验立刻跟着变，不会与本文档漂移。单词响应可以使用 `senses` 数组返回多义结果；Lens 按频率顺序最多保留 3 条，关闭多义开关时只保留第一条。

| 字段 | 类型 | 说明 |
|---|---|---|
| `word` | string | 原样回显输入的词，用于逐词核对 |
| `ipa` | string | 音标，两侧带斜杠；可选，缩写（如 `qml`）没有音标时可以缺席或为空 |
| `pos` | string | 最常见义项的词性，一个小写英文词（如 `noun`）；**可选**：模型可以不给，卡片就不画 |
| `en` | string | 一行英文释义 |
| `zh` | string | 一行中文释义 |
| `translation` | string | `es` / `ja` 解释语言下的目标语言释义；对应语言时非空 |
| `etymology` | string | 词源，一句话，语言跟随解释语言；**可选**：仅词源开关打开时才请求，缩写等来历不明的词没有 |
| `example` | string | 例句，一句英文，使用该词；**可选**：仅例句开关打开时才请求，且始终为英文（不随解释语言） |
| `senses` | array | 多义结果，按频率降序；每项含 `en`、`zh`，并在 `es` / `ja` 下含 `translation`，最多保留 3 项 |
| `signpost` | string | 义项路标，两到四个英文词，给释义链的下一层用；**可选**：没有简短路标的词缺席，卡片不画 |
| `collocations` | array of string | 最多两个常用搭配，词写作 `~`；**可选**：缺席或为空时脚注不画，超出两项的部分丢弃 |
| `imageable` | boolean | 一张照片能否让这个义项更清楚；**可选且仅供参考**：只有单词通道请求，取不到布尔值时按 `false` 读 |
| `imageQuery` | string | `imageable` 为 true 时给出的两到五个英文词，供图片搜索消歧；**可选**：恒为英文，不随解释语言变化 |

`imageable` / `imageQuery` 是**建议值，不是契约**：卡片只按 `imageable && imageQuery 非空` 画图片入口，所以一个 true 而没有 query 的义项就是没有图（`docs/adr/0079`）。两条都不进 `required`，也**故意不在 schema 里声明类型**——`matchesSchema()` 只校验它认识的词汇，声明成 `boolean` / `string` 会把一个类型写错的建议值升级成整批失败，而丢掉一份已经到手的释义是更坏的取舍。校验层仍然只管 “有没有”，空与错都留给解析层读成 “没有图”。

`etymology` 的开关落在提示词上：`request.word.json` 的 `etymologyPrompt` 给出句子与 JSON 示例里的字段片段，模板用 `{etymologyNote}` 与 `{etymologyField}` 两处占位符接住，代码按开关填入或填成空串——关闭时提示词与没有这个字段时逐字相同，因此单义与多义两份模板各一份就够，不必为 “开 / 关 × 单义 / 多义” 维护四份重复文本。字段不进 `required`：关闭时本就不发，来了也按可选处理。

`example` 同样落在提示词上：`examplePrompt` 给出 `{exampleNote}` 与 `{exampleField}` 两处占位符的内容，开关关着时两处都填成空串。它与词源开关相互独立，可以同时开。例句只用英文、不附译文，这是有意的简化（`ponytail:` 注释记着升级路径）。缓存里 `example` 的三态见 `docs/adr/0033`：缺失表示买的时候没问，此后开关打开会重新请求；空串表示问过没给，不再重问。

## 3.1 响应体：实体

实体响应同样嵌在 `choices[0].message.content`，只带 `en` / `zh` 两行，不带单词专属的 IPA，也不回显实体名：

```json
{
  "results": [
    {
      "en": "A major city in the United States.",
      "zh": "美国的一座大城市。"
    }
  ]
}
```

`en` 与 `zh` 各是一行百科式说明。**实体响应不回显实体名**：App 已知道选中的文本，让模型逐字复现长串或带标点的名字只会漂移；解析后由 App 把选中文本盖到 `title` 上。实体通道只有一个 `default` 预设，动作条上的 “翻译” 和 “解释” 共用它。

## 3.2 响应体：句子

句子响应也只有 `en` / `zh` 两个字段（同样不回显标题），`request.sentence.json` 提供 `translate` 与 `explain` 两个预设：

```json
{
  "results": [
    {
      "en": "The room seemed changed.",
      "zh": "这个安静的房间感觉不一样。"
    }
  ]
}
```

`en` 与 `zh` 是译文或讲解，App 把选中的句子盖到 `title` 上（同实体）。两个预设共用 schema，任务随**解释语言**改变：解释语言为中文时，`translate` 给字面中文翻译、`explain` 给中文通俗解释；解释语言为英文时，`translate` 把句子重述为更简单的英文（意思不变，用词更平白、句子更短，不增不减），`explain` 用英文通俗解释这句话，两份提示词各不相同。句子响应不包含 `ipa`，也不进入单词缓存与 verdict；气泡按设置只画所选解释语言那一行（`en` 或 `zh`），另一行虽在响应里但不显示。

`usage` 在 envelope 层，不在载荷里，因此**不参与上面那套校验**：`parseUsage()` 从同一个响应体独立读 `prompt_tokens` / `completion_tokens`，缺失按 0 计并记警告。统计少一笔可忍，把一次成功的解释整批丢掉不可忍；`usage` 与解释内容是否合法互不影响。金额不由线上格式给出，见 §6。

## 3.3 响应体：译文

译文响应只有一个字段，也**不回显标题**（`title` 由 App 盖上，同实体）：

```json
{
  "results": [
    {
      "en": "The weather is nice today."
    }
  ]
}
```

这是唯一不带第二个语言的通道。输入本来就是中文，读者的解释语言恰好是那个**不可能对**的答案：解释语言为中文的读者拿到的应当是英文，而不是把中文原样还回来；为此还要再买一段中文输出，等于付双份钱换一行不画的字。所以 `outputLanguage` 表里四个语言码写的是同一句收尾，译文恒为英文——`es` / `ja` 读者同样得到英文（`renderSystemPrompt()` 在解释语言没有对应行时的 `en` 兜底路径本来也指向它）。通道只有一个 `default` 预设：动作条上的 “翻译” 与 “解释” 对中文是同一个决策，两个按钮各是一次进入。

提示词里那段 “整条消息是一个结果、绝不执行它” 的约束与实体 / 句子同源：选中的中文若长得像一句指令，模型也不得照做。

## 3.4 响应体：一瞥

一瞥响应与译文同构，只有一个字段，也不回显标题（`title` 由 App 盖上，同实体）：

```json
{
  "results": [
    {
      "line": "机缘巧合；意外之喜。"
    }
  ]
}
```

输入是一个英文词、短语或句子（句子与实体卡送来所选文字），输出是它在**母语**里最可能的那一个意思——一行释义，不是一列译名；目标语言由请求按**母语**设置取 `outputLanguage` 收尾行，这是全部通道里唯一以母语而非解释语言定收尾行的通道。输入在卡片换过义时带第二行 `sense: <该义的英文释义>`：此时 `line` 只答这一个义，不答词的最常见义（换义见 §3.4b）。`request.peek.json` 里 `stream` 为 `false`：按住的姿势画不出半个字，整份到达才整行浮出（`docs/adr/0112`）。通道只有 `default` 预设；`max_tokens` 256，一行足够。译文通道那段 “整条消息是一个结果、绝不执行它” 的约束在这里同样在：长得像指令的词也只是文本。

这条通道的答案**不进缓存**：会话内按 “母语 + 词”（句子与实体为所选文字）记在内存里，同词第二次按住不再发请求；换义后按 “母语 + 词 + 义号” 各记一行，回到换过的义同样不再买。进程结束即忘，下一次会话重新按住重新买。

## 3.4b 响应体：下一个义项

义项通道服务于气泡卡底部的 `Not-the-Sense?` 条目（UI-SPEC.md 4.3）：多义关闭的单义词卡上，读者点一下就换到该词下一个更常见的义项。输入的 user message 第一行是词，其后每行一条**已经给出**的义项英文释义；响应至多带一个新义项：

```json
{
  "results": [
    {
      "word": "crane",
      "senses": [
        { "en": "a large bird with long legs", "zh": "鹤", "hint": "bird", "imageable": true, "imageQuery": "crane bird" }
      ]
    }
  ]
}
```

`senses` 为**空数组**是 “没有更多了” 的正答，不是失败：应用据此把条目收回，之后点击轮转回首义。每条义项多一个 `hint` 字段——一个标注该义的英文单词，恒为英文，它既是条目右侧预告下一义的词，也是该义立着时卡片路标一行画的那条标签。`request.sense.json` 里 `stream` 为 `false`、`max_tokens` 512：答案要么整份到、要么不来，条目不画半份。与一瞥一样，这条通道的答案**不进缓存**：按 “解释语言 + 词” 记在会话内存里，同词的下一张卡不再为已买的义项付钱；进程结束即忘。

通道的失败与一瞥同一套安静：传输与校验的拒绝不上通知、不换卡片上的释义，条目留在原地，下一次点击或悬停再试。区分 “没有更多” 与 “没送到” 靠 `usage`：整份到达却空 `senses` 的判为耗尽，不带 usage 的判为失败（§3.4b 的判定在 duty 里，客户端两种都原样上报）。

## 3.5 流式到达

请求是流式的（§2 的 `stream: true`），响应是 SSE：每个 `data:` 事件带一段 `choices[0].delta.content`，把各段文本依次拼起来，就是整份响应里 `choices[0].message.content` 那一个字符串。`finish_reason` 与 `usage` 落在最后的那些事件上，`data: [DONE]` 收尾。线上没有第二个格式，只是到达方式变了。

客户端把整条流**重新拼回一个 envelope**（`choices[0].message.content` + `finish_reason` + `usage`），再走 §4 那套整份校验——所以校验规则一个字没改。

- **画的可以是半份，存的只能是整份**：拼到一半的文本边到边画进气泡，但只画**值已闭合**的字段（`word` / `ipa` / `pos` / `en` / `zh` / `etymology` / `example` / `senses`）；值还没写完的字段一律缺席，与整份响应里省略该字段时长得一样。缓存、词汇档位与统计只认 `parseExplanations()` 通过的那一份，半途而废的答案不落任何一处。
- **只画 `results[0]`**：一次请求可能查多个词（扫描候选，最多 5 条）。卡片画的是首个被命名的结果——与整份到达后 `ExplanationQueue::settled()` 选中的那一条一致；其余各条在整份到达后一并入缓存，不上屏。
- **`finish_reason` 缺位或为 `length`**：与整份响应同判，整批失败，走 §5 的失败路径出通知；已经画出去的半份随之被通知替换。
- **`usage`**：靠 `stream_options.include_usage` 索要，落在最后一个事件上；拿不到时按 0 计并记警告，与 §3 的取舍相同。

客户端按字节缓冲、到事件终止（空行）才解码，所以一个 `data:` 行、一段转义、一个多字节字符被读取边界切开都不会读坏；事件终止既认 `\n\n` 也认 `\r\n\r\n`。`partial` 只在真正画得出的字段发生变化时才再发一次。

## 3.6 响应体：拆解

句子选区的成分拆解（`UI-SPEC.md` 4.3）。**应用先切词，模型只回下标**：切词在本地做，模型不负责数词，所以它数错一个词不会把颜色画到别的词上。

- `messages[1]` 是切好的词，**一行一个**，行号从 0 起即下标。切词按空白，标点随词，与选区分类所用的切词一致。
- 系统提示词说明四个角色：`S` 主语、`V` 谓语、`C` 表语、`A` 状语。从句按其在句中的功能归入（状语从句归 `A`）。`from` 与 `to` 是待查内容中从 0 起的行号，两端含。
- 响应只给被标出的词：

```json
{
  "results": [
    {
      "spans": [
        { "role": "A", "from": 0, "to": 4 },
        { "role": "S", "from": 5, "to": 6 },
        { "role": "V", "from": 7, "to": 7 },
        { "role": "C", "from": 8, "to": 8 },
        { "role": "A", "from": 9, "to": 10 }
      ]
    }
  ]
}
```

- 没有标出的词照常画原色，不加底。同一个词只属于一段。
- 请求与 §2 相同，除了 `stream` 为 `false`（整份到达才画，同一瞥的理由）；因此不发 `stream_options`，用量由非流式响应的 `usage` 给出，花费按 §6 计。`max_tokens` 取 1024 作起点值，40 词以内的句子足够。
- 响应不回显句子文字。气泡上的原句由应用自己持有，与句子通道的 `title` 盖章规则一样。
- 结果不入单词缓存，不进 verdict。

## 3.7 响应体：纠正

纠正拼写与纠正语法（`UI-SPEC.md` 4.10）。两条通道的请求与 §2 相同，`stream` 为 `false`，只回修正后的文字，不回显原文；应用持有原文，修正前后的词差由应用本地对比得出，模型不负责标出改动。

```json
{
  "results": [
    { "corrected": "receive" }
  ]
}
```

- 拼写通道只收单个 token：修正结果不得含空白，否则整份作废。语法通道的修正结果是整句，不得为空。
- 原文与修正相同即 “无需改动”，不作失败。
- 结果不入单词缓存，不进 verdict，计入花费。

## 4 校验规则

响应是**第三方不可信数据**，任一项不过即整批失败，不静默丢词：

1. 外层是 JSON 对象，且 `choices` 非空；
2. `finish_reason` 必须是 `stop`——`length`（截断）、`content_filter`、`insufficient_system_resource`、`aborted` 一律判失败；
3. `message.content` 能解析成 JSON 对象，且带 `results` 数组（容忍 Markdown ``` 围栏或前后闲话：提取首个 `{` 到末个 `}`）；
4. `results` 每一项是对象，且**具备所选 schema 要求的全部字段**；
5. `en` 非空；`zh` 对前三条通道同样必须非空，**译文与一瞥通道除外**——它们只要求各自的一行（译文的 `en`、一瞥的 `line`），`zh` 与 `translation` 根本不在它们的 schema 里；解释语言为 `es` / `ja` 时 `translation` 也必须非空，同样不适用于这两条一行通道；义项通道的空 `senses` 应答连 `en` 都可以空——那是耗尽正答，不是残批；单词通道额外要求 `word` 非空，`ipa` 可空（孤立缩写的单词没有音标），`etymology` 同样可空（开关关着时不发，来历不明的词也没有）；`example` 也可空（开关关着时不发，模型没有句子可给时缺席）；`imageable` / `imageQuery` 可空（见 §3 的表：建议值不因缺失或类型不对而判批失败）；多义数组按 schema 校验，应用最多保留 3 项；
6. 单词通道回显的词与请求**逐一对应**：不少、不拼错；**同一个词回显两次不算失败**——模型把一条义项一个结果写出来时就会这样命名同一个词，代码把重复的那条并入已保留的那条（相同义项去重，条数仍受 3 条上限约束），因为丢掉已经到手的释义比多一次回显更糟；模型回显了请求里没有的词仍然算失败。实体 / 句子不回显，按请求顺序取结果（`title` 由 App 盖上），模型把一段拆成多条时按顺序**合并**（各条 `en` / `zh` 以换行拼接）。返回顺序不作要求，代码按请求顺序回填。

7. **拆解通道**（§3.6）：`spans` 必须是非空数组；每项的 `role` 只能是 `S` / `V` / `C` / `A`，`from` 与 `to` 是 `[0, 词数 − 1]` 内的整数且 `from ≤ to`，各段互不重叠。任一项不过即整份失败，不画部分色。
8. **纠正通道**（§3.7）：`results` 恰好一项，`corrected` 非空；拼写通道的 `corrected` 不得含空白。任一项不过即整份失败。

失败原因会经 Qt 翻译（`tr()` / `QCoreApplication::translate()`）后交给 `LlmClient::failed(QString)`，译文见 `i18n/lens_zh_CN.ts`。

## 5 错误码

| 状态码 | 含义 |
|---|---|
| 400 | 请求格式错误 |
| 401 | API key 无效或缺失 |
| 402 | 账户余额不足 |
| 422 | 请求参数无效 |
| 429 | 请求过于频繁，被限流 |
| 500 | 服务端错误 |
| 503 | 服务过载 |

`failed(message)` 按上表分类；**任何分支都不得回显密钥**。

## 6 金额与价目

金额不由线上格式给出，本地按 `data/llm/pricing.json` 算（`llm_pricing.{h,cpp}`）。**价目是数据**：按模型名给输入 / 输出单价（每百万 token）与币种，改价是改数据，不重编译；价目缺失或模型不在表内时金额记 0 并记警告，不影响解释。模型目录与价目分离，Custom 或未列价模型仍可请求。

**牌价与展示币种分开**（2026-10-03）：表里 0.15 / 0.6 是厂商的**美元**牌价，界面显示人民币。做法是 `display` 块写明目标币种与一个乘数（`multiplier`）及其取值日期（`asOf`），**牌价原样不动**——把 `currency` 直接改成 `CNY` 等于宣称厂商按人民币报价，那是说错。换算只在 `Pricing::cost()` 里发生一次，`currency()` 返回展示币种，故统计、花费、托盘提示三处无需各自知道还有另一种币。乘数是数据，会过期：启动时把乘数与日期一起记进日志，看到 ¥ 想问 “哪来的” 时不必翻文件。`display` 块缺失时展示币种回落到厂商币种。

## 7 相关文件

模型列表由目录项的 `modelSource` 定义：一条不带认证、不带查询的 `GET https://openrouter.ai/api/v1/models`，一次回答所有服务商。每个服务商条目上的 `openRouterPrefix` 给出该厂商在 OpenRouter id `vendor/slug` 里的 vendor 部分，取到字段前被去掉；前缀为空表示整份目录、id 保持原样（OpenRouter 自己，它的接口收的就是 `vendor/slug`），没有这个键的服务商不参与切分。回答里有两条过滤：id 带 `:` 的（OpenRouter 自己的路由与计价模式，如 `:batch`、`:free`）丢掉，`architecture.output_modalities` 恰好只有 `text` 一项的才留——不是 “含有 text”，16 个同时写图像或音频的条目都含 text，那样的条件过滤不掉任何东西。

模型列表缓存写在用户的 `%APPDATA%\Lens\settings.json` 的 `MODELS`（键仍是服务商 `value`），`catalog.json` 只作为安装包中的只读种子；同一份回答按前缀切开后一次写入各家。刷新失败、超时、空响应或结构无效时保持已有缓存，一个字也不写。模型列表请求超时为 10 秒，响应上限为 2 MiB，模型条目上限为 2000。这次请求不带 API key，也不带读者内容，但每次启动都会发出，包括从不打开设置页、没有 key 的读者（取舍见 `docs/adr/0020`）。取到的列表**不改写当前模型**：它是整个行业的清单，不是当前那家服务的货架（`docs/adr/0020` 取代 `docs/adr/0017` 的这一条，模型跟随服务商只在换服务商时仍然发生）。

- `data/llm/request.word.json`、`request.entity.json`、`request.sentence.json`、`request.translation.json`、`request.peek.json` — 请求模板、预设、系统提示词与输出语言行
- `data/llm/response.word.schema.json`、`response.entity.schema.json`、`response.sentence.schema.json`、`response.translation.schema.json`、`response.peek.schema.json` — 响应 schema（校验真源）
- `data/llm/request.breakdown.json`、`data/llm/response.breakdown.schema.json` — 拆解通道的请求模板与校验真源（§3.6）
- `data/llm/request.spell.json`、`data/llm/response.spell.schema.json`、`data/llm/request.grammar.json`、`data/llm/response.grammar.schema.json` — 纠正通道的请求模板与校验真源（§3.7）
- `data/llm/pricing.json` — 价目数据，算金额用（§6）
- `data/llm/catalog.json` — 服务商、离线模型种子、共用模型源与厂商前缀、base URL、解释语言与请求覆盖项
- `src/llm/llm_protocol.{h,cpp}` — 加载与访问上述协议文件
- `src/llm/llm_pricing.{h,cpp}` — 加载价目、把 `Usage` 折成金额
- `src/llm/llm_pure.{h,cpp}` — 组装请求体、校验响应、读 `usage`，均无网络
- `test/googletest/unit/llm_pure_test.cpp` — 用固定样例锁住上述行为
