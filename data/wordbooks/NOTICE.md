# 档位词书：来源与许可

本目录的词书由 `scripts/data/build-wordbooks.py` 生成，每行一个小写英文词头。生成时只保留 `data/wordlist.txt` 里已有的词，不含释义；词表是 “是否真词” 的唯一权威（`data/wordlist-source.md`）。

| 词书 | 收录范围 | 来源 | 许可 | 词数 |
|---|---|---|---|---|
| `cet4.txt` | 四级 | ECDICT 1.0.28 的 `cet4` 标签 | MIT | 3802 |
| `cet6.txt` | 六级，累积，含四级词 | ECDICT 1.0.28 的 `cet6` 标签 | MIT | 5352 |
| `toefl.txt` | 托福 | ECDICT 1.0.28 的 `toefl` 标签 | MIT | 6921 |
| `ielts.txt` | 雅思 | ECDICT 1.0.28 的 `ielts` 标签 | MIT；标签来源未核实，见下 | 4943 |
| `b1-b2.txt` | CEFR A1–B2，累积 | CEFR-J Vocabulary Profile 1.5 | 研究与商用免费，须注明出处 | 6478 |
| `c1-c2.txt` | CEFR A1–C2，累积 | CEFR-J 1.5 加 Octanove Vocabulary Profile C1/C2 1.0 | Octanove 部分 CC BY-SA 4.0 | 8185 |
| `tem4.txt` | 专业四级（TEM-4） | 读者自备 PDF 整理，文字层由 `pdftotext` 提取 | 8440 |
| `tem8.txt` | 专业八级（TEM-8），累积，含四级词 | 读者自备 PDF 整理，同上 | 12410 |

## 分发要求

- **ECDICT（MIT）**：词书派生自 ECDICT，下面是其许可原文，分发时须保留。

  ```
  MIT License

  Copyright (c) 2025 Linwei

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```

- **CEFR-J（`b1-b2.txt` 与 `c1-c2.txt` 的 A1–B2 部分）**：版权属 Tono Laboratory（东京外国语大学）。引用时写明：Yukio Tono 编，《CEFR-J Wordlist Version 1.5》。数据经 `openlanguageprofiles/olp-en-cefrj` 取得。CEFR-J 没有许可证文件，许可条款以其 README 的使用条款为准。
- **Octanove（`c1-c2.txt` 的 C1/C2 部分）**：Octanove Labs 的 C1/C2 词表依 [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 授权。`c1-c2.txt` 是其改编，分发时须同样以 CC BY-SA 4.0 署名并相同方式共享。

## 已知的数据问题

- CEFR-J 按词头与词性标注，个别词的等级与直觉不符，例如 `dignify` 列入 B1–B2。这是来源的标注，不在本地改动。
- ECDICT 的考试标签是它的标注，不等于官方大纲的全文。词书反映的是该标签的收录，与官方大纲的差异未逐一核对。
