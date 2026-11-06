# Changelog

## Unreleased

### Added

- The desktop pet sometimes turns a stroll into a **taskbar trip**: when the screen has a fixed bottom taskbar (auto-hide off), it leaps in a small arc toward the roomier side of the screen, bounces on the taskbar's top edge, pauses, walks at a slow steady pace almost to that side's end and back to where it bounced, then hops back up to where it started on its walk cycle. Other pet behaviour is held until the trip ends; a drag or a click cuts it short. Each pet's chance is the optional `behavior.taskbarChance`, 0.3 by default (docs/adr/0155).
- The statistics popup opens a **Week profile** panel: this week's lookups from Monday to today, with the new-word and repeat shares, the parts of speech, lookups by hour, and the five most looked-up words. The counts come from the local history and cost nothing. **Interpret this week** is the one request: it sends a summary of those counts and the five words (no sentences, definitions or times) and bills like any other request. Its answer opens in a full-screen **weekly report card**, and the same button opens that card again without a request (docs/adr/0150, and docs/adr/0151 for the card).
- The **Week profile** panel adds one line of behaviour: the share of this week's word explanations made in a non-English language, and how many mother-tongue peeks were completed. Both are counted on the device from the history and the daily tallies, and neither enters the interpretation request. Switching the explanation language away from English also adds a note under that setting saying that the held-globe peek only exists under English; choosing English again takes the note away.
- The weekly **report card** shows this week's figures, the hours they fall on, and five observations. Each observation carries a figure drawn from the counts on the device, so the model writes no numbers (docs/adr/0151). Its **Story** button writes one short English story from up to fifteen of the week's looked-up words, chosen on the device; the words the story uses are underlined in the text. Only those words leave the machine, and the story is kept in memory for that week (docs/adr/0152).
- The words export's save dialog offers two learning-card formats, **CSV with explanations** and **Anki TSV with explanations**. Each row is a word from the list with its cached IPA, part of speech, English and Chinese explanations and example. A word whose explanation was never fetched keeps its row with those columns empty, and the export makes no request. The CSV has a header row and a UTF-8 byte-order mark so a spreadsheet reads the Chinese; the Anki file opens with `#separator:tab` and has no header, for Anki's text import.

### Changed

- The weekly report's AI observations now follow the **interface language**, not the explanation language, so they read in the same language as the panel that shows them.

### Fixed

- The first-run wizard's step body scrolls inside its fixed 380×480 card. Longer translations, English and Spanish among them, no longer run under the footer; the header and buttons stay put, the wheel scrolls from anywhere on the card, and keyboard focus brings the control into view.

## 1.0.0-alpha - 2026-11-05

This is an early alpha of Lens, and the version numbering starts over at 1.0.0-alpha. It may have many problems, including crashes, wrong results and rough edges on every surface. Please report what you find as an issue at https://github.com/kakuyo1/lens/issues, with the steps that led to it and your Lens version.

这是 Lens 的早期 alpha 初版，版本号从 1.0.0-alpha 重新开始。可能存在很多问题，包括崩溃、结果错误，以及各个界面上的粗糙之处。遇到问题请到 https://github.com/kakuyo1/lens/issues 提交 issue，并附上复现步骤和 Lens 版本号。
