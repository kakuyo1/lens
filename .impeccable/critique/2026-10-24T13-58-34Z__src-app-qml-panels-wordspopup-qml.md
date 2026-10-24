---
target: 统计/年度面板（修复流② re-critique）
total_score: 32
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\WordsPopup.qml"
target_fingerprint: "sha256:f19fb4121b4c939d0453dbfd21072d14d0f91667d83180a3ef9d00985ccbd1df"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\WordsPopup.qml"
timestamp: 2026-10-24T13-58-34Z
slug: src-app-qml-panels-wordspopup-qml
---
Assessment A — design-director review, Lens statistics panels (StatsPopup.qml, YearWordsPopup.qml), round 3.

**1. Authored or interchangeable?**
Authored. The hue-wheel palette, the mono-numeral glass family, and the dual-denominator year summary could not be transplanted to a generic dashboard; the stats card is the plainest member but speaks the same dialect.

**2. Nielsen scores (0–4)**

| # | Heuristic | Score | Key issue |
|---|---|---|---|
| 1 | Visibility of status | 3 | Day selection is a 1px border on a ~5px cell — easily missed; cells have no hover affordance, so nothing hints they are selectable |
| 2 | Match real world | 3 | On-screen units now follow ADR 0104, but screen-reader copy still said "words" for pop counts (fixed in this round's wake-up commit; cells and legend now speak lookups) |
| 3 | User control | 4 | solid — Escape peels palette→card exactly matching the visual stack; card draggable; palette toggles shut |
| 4 | Consistency & standards | 2 | 8–10px text violates DESIGN.md's own floor ("Don't: 用小于 11 px 的字号"); the summary line carried monoFamily over "845 次 · 30 词" (also corrected this round: mixed CJK units take the app face) |
| 5 | Error prevention | 4 | solid — read-only data, reversible color; nothing destructive lives here |
| 6 | Recognition over recall | 3 | Legend explains the encoding, but the palette hides behind a glyph that looks like legend chrome; no year anchor forces recall of "which 365 days" |
| 7 | Flexibility & efficiency | 3 | Esc, drag, drill-downs all wired; but the graph and palette are pointer-only — no keyboard path at all |
| 8 | Aesthetic & minimalist | 3 | Quiet and tight; the 8px captions crowd against the 5px grid and read as afterthoughts |
| 9 | Error recognition & recovery | 4 | solid — little to fail; empty year degrades to an all-neutral graph legibly |
| 10 | Help & documentation | 3 | Self-evident legend, but level thresholds (1 / 2–3 / 4–5 / 6+) are invisible; no hover tooltip on cells |

**3. Cognitive load — failures only**
- Two captions share the year summary row with different jobs: "弹词总数"/"Words explained" (a graph caption) sits where a value label would, directly facing "845 次 · 30 词".
- The stats card carries three denominators in four rows (today's pop events, today's judgments, all-time vocabulary) with no on-surface cue separating them.
- Controls disguised as data glyphs: graph cells, legend cells and shade swatches have no affordance; the palette is discovered by accident.
- Sub-11px micro-labels in five places (months, weekdays, Less/More, "365 days", shade captions, 9px hue name).

**4. Strengths**
- **"845 次 · 30 词" side by side** — ADR 0104's best move: the dedup story is told by juxtaposition, no footnote needed; the pair *is* the explanation.
- **The hue wheel** — a genuine signature moment: shortest-arc rotation to bring the pick to 3 o'clock, upright centre name, glass centre disc, and the graph re-encodes live (WYSIWYG, zero confirmation).
- **StatRow** — one component announces drill-down in three channels: widened hover target past the text, faint chevron, and full keyboard + combined Accessible.name ("label, note, value"). This is the family's quality bar.

**5. Priority issues**
- **[P1] Keyboard-and-AT dead zone on the year card.** All 371 cells, 12 hue dots and 4 shade swatches are TapHandler-only Rectangles — no `activeFocusOnTab`, no key handling; swatches expose no Accessible role at all. Fix: arrow-key navigation over the grid with one focused cell (Enter selects), `Accessible.action Press` on hue dots, Button role on swatches.
- **[P1] Sub-11px type.** 8px month/weekday/legend/shade labels, 9px hue name, 10px "365 days" — below the product's own published floor, mostly in `faint`. Fix: 11px floor for all captions; month labels in `muted`. (Already queued as 修复流③'s 年度卡字号 item.)
- **[P2] Judgment semantics still unexplained (carried open).** 已会/新词 read as vocabulary states but count today's judgments, and a re-judged word migrates between rows; next to "All time 30 words" the card invites a vocabulary misreading. Fix: one inline cue — e.g. an 11px faint footnote under the divider ("已会 / 新词按今日弹词的判定计") or a per-row note slot StatRow already has.
- **[P2] ADR leftovers in accessible copy.** Cell names "%1, %2 words" and legend "No words, choose color" labelled pop counts as words — a defined bug per ADR 0104's gloss. (Fixed in this round's wake-up: cells and legend now speak lookups in all four languages.)
- **[P3] Mono font on mixed-unit summary.** The summary set `monoFamily` on "845 次 · 30 词"; CJK glyphs fell back and broke the tray's mixed-value convention. (Fixed in this round's wake-up: app face, per UI-SPEC 4.7's mixed-value rule.)

**6. Persona red flags**
- **Sam (accessibility):** the entire year surface is pointer-only — she can hear a cell's name but cannot select a day, open the palette, or pick a hue from the keyboard; the 8px faint labels are also under the product's own accessibility budget.
- **Riley (stress):** 9px legend targets and 5px cells — a hurried mis-tap near "More" unrolls a 124px palette that shoves the graph up under her cursor; recovery is a precise re-tap on the same 9px cell or remembering Esc.

**7. Provocative questions**
1. A 12-hue wheel with four shades personalizes cells 5 pixels wide — is this the product's signature delight, or the most calibration ever offered for the least visual payoff? Would six curated hues plus "plain" serve the reader better?
2. The stats card's headline number ("Lookups today") is the only figure that leads nowhere, while its clickable rows answer "what did this cost me" and "what do I have". Is a pop count a number anyone acts on — or is the card wearing "what did I learn" clothes over a billing summary?
