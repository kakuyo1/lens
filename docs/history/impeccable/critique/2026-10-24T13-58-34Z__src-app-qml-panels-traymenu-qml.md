---
target: 托盘菜单（修复流② re-critique）
total_score: 32
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 1
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
target_fingerprint: "sha256:402ca29676c54a44c3f52ab131abb10325e7bdda4056f9a59b61e5b5d022d9f7"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\TrayMenu.qml"
timestamp: 2026-10-24T13-58-34Z
slug: src-app-qml-panels-traymenu-qml
---
# Assessment A — Tray menu (round 3)

**1. Authored or interchangeable:** Authored. The silhouette is a standard tray menu, but the first row is Lens's own invention — the tray icon's four-state priority restated in words, on the product mark — and the token/typography discipline (MixedText figures, danger-by-color-only Quit) is nowhere generic.

**2. Nielsen scores (0–4):**

| # | Heuristic | Score | Key issue |
|---|---|---|---|
| 1 | System status | 4 | Solid — four-state line mirrors `tray_state.h` priority; text disambiguates busy vs idle where the mark can't |
| 2 | Match real world | 4 | Solid — 次/词 split (ADR 0104) landed in both menu and tooltip |
| 3 | User control | 2 | Language submenu: hover is the only way in; Escape skips folding the list and closes everything |
| 4 | Consistency | 3 | Quit divider: spec's 5px breathing room unimplemented (`anchors.margins` with no anchors is a no-op) |
| 5 | Error prevention | 3 | Quit fires instantly; danger color + divider is the only guard — acceptable tray convention, nothing more |
| 6 | Recognition | 4 | Solid — every row carries its live value inline (tally, language) |
| 7 | Flexibility/efficiency | 2 | No keyboard path at all: per the file's own Escape rationale the window never becomes active, so MenuRow's tab/keys machinery is unreachable |
| 8 | Minimalist aesthetic | 4 | Solid — five rows, one column, tokens all the way down |
| 9 | Error recovery | 3 | Budget exhaustion is named plainly but the message is a dead end; the fix (Settings → Daily budget) is two rows down with no link |
| 10 | Help/docs | 3 | Solid at this scale — tooltip 4.6 carries mode + tally; nothing more belongs inside a 240px card |

**3. Cognitive load failures:**
- One mark, two meanings: the lens mark is `ok` green for both "idle and ready" and "explanation in flight" — the reader must parse the text line every time; the tray icon's busy art (corner dot) has no twin here.
- (No other failures: ≤5 flat rows, clear grouping, one semantic color per meaning, state readable without interaction, submenu is honest progressive disclosure.)

**4. Strengths:**
- `stateLineFor()` is real product thinking: a pure function mirroring the icon's priority, testable without spending API money, and the ordering is honest — exhausted budget outranks capture mode precisely because capture gates pause with it, so the line never lies.
- Row anatomy discipline: the state line shares the rows' 11px/15px glyph column instead of starting a second one; MixedText keeps digits in Cascadia while CJK stays in the app face — "12 次 · ¥0.04" renders crisp and tabular.
- i18n is finished work, not intent: zero `unfinished` across all four `.ts`, including the two strings new this round.

**5. Priority issues:**
- **[P1] Language submenu is hover-only and its accessible action is a no-op.** The Language `MenuRow` has no `onPicked`; the window never activates so Tab can't reach rows; list options have no key handling. Keyboard and AT users cannot change UI language from the menu at all — NVDA firing the row's press action does nothing. Fix: connect `onPicked` to toggle `listVisible`; give options `activeFocusOnTab` + Return handling and move focus to the current one on open; make Escape fold the list before the menu.
- **[P2] Long localized strings are unbounded.** The state `Text` anchors left only — no right anchor, no elide; es "La captura por selección está desactivada" (41 chars, 12px bold) and "El presupuesto de hoy se ha agotado" exceed the ~193px remaining and paint past the card edge. The MixedText note has no `maximumWidth`, so a heavy day ("1,280 consultas · ¥12.34") can collide with the row label. Fix: right-anchor + ElideRight the state text; bound and elide the note.
- **[P2] Quit divider ignores its own spec.** `anchors.margins: 5` on an unanchored item is dead, so the line sits flush against the 32px hover fills (UI-SPEC 4.2: 上下留 5 px); hovering 设置 or 退出 puts panel2 fill against the line. Fix: wrap the divider in a ~10px Item, line centered; delete the dead property.
- **[P3] Exhausted-budget line is a statement, not an affordance.** The warn state is communicated but not actionable; every neighbor row is a button. Fix: when exhausted, make the state row tappable → `settingsRequested()` (the same hand-off the other rows use).
- **[P3] Spec/code drift.** `tray.cpp` opens the menu on left-click `Trigger` too, but UI-SPEC 4.0 lists only 右键; and `stateLineFor()` re-derives `trayState()`'s priority in QML — a fifth state means two places to update. Fix: document left-click in 4.0/4.1 (correct for a tray-only product) or restrict it; expose the C++ readout (state enum + labels) as a controller property QML consumes, instead of re-implementing the ordering.

**6. Persona red flags:**
- **Sam (accessibility):** the whole menu is mouse-only in practice — a never-active window means Tab/Enter/FocusRing never engage, Escape is the single key that works, and the one configurable action (language) fails even under a screen reader's press action.
- **Alex (power user):** opens the menu daily to check and control capture and can only look — no capture toggle lives here, Settings is a round-trip, and left-click is spent duplicating right-click instead of carrying that toggle.

**7. Provocative questions:**
- If the tray icon earns four distinct artworks, why does the menu's prime row reuse one static logo — is text-only status the honest scope of a 240px card, or is the busy state's corner-dot twin missing here?
- For a windowless product, this menu is the entire front door: should its first row be a status bar, or the product's headline — what you did today — with system state demoted to the mark?
