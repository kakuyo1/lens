---
target: 更新卡（修复流② re-critique）
total_score: 32
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 0
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\UpdateCard.qml"
target_fingerprint: "sha256:4e3c014a40312db8b3b929382dcd0d9fbe73046ca0883f5567d42c20a5b43cef"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\panels\\UpdateCard.qml"
timestamp: 2026-10-24T13-58-34Z
slug: src-app-qml-panels-updatecard-qml
---
# Assessment A — UpdateCard.qml (critique round 2+)

**1. Authored or interchangeable?** Authored. The proxy-in-mainland-China failure line, the clock ETA that dodges unit-word translation, and the checksum trust arc (verify → "Checksum verified" → Install) could belong to no other updater.

**2. Nielsen scores (0–4)**

| # | Heuristic | Score | Key issue |
|---|---|---|---|
| 1 | System status | 4 | solid — state line + bytes + % · ETA, all duty-driven |
| 2 | Match real world | 4 | solid — "1:02:33 left" clock, named proxy cause |
| 3 | User control | 3 | `verifying` hides the primary pill: no Cancel exactly when the file exists on disk |
| 4 | Consistency | 2 | View shares ink-primary with Download/Install (queued); 26 px vs confirm card's 27 (queued); §4.0 row stale (queued) — the residue is real but already owned |
| 5 | Error prevention | 3 | checksum gate is genuine prevention; but Cancel sits in the loudest style during download |
| 6 | Recognition | 4 | solid — everything visible, nothing recalled |
| 7 | Flexibility | 2 | keyboard gets Escape and nothing else |
| 8 | Minimalism | 3 | two reflows mid-flow disturb an otherwise tight card |
| 9 | Error recovery | 3 | three fixed sentences, danger color; but only the checksum failure says what happened to the file |
| 10 | Help & docs | 4 | solid — in-context proxy guidance instead of a docs pointer |

**3. Cognitive load (8 items — failures only)**
- One primary action per view — fail (primary pill + View both ink)
- Control style matches consequence — fail (Cancel, the least-wanted act, wears the strongest)
- Layout stable across states — fail (downloading → verifying → ready reflows width and height twice)
- Input path mirrors visual path — fail (five controls visible; keyboard has one key)

**4. Strengths**
- `downloadText()`/`primaryAction()` are pure functions of duty state; failure text is one of three fixed keyed sentences — no transport string can ever leak onto the surface, and localization stays closed.
- The ETA as a bare clock with a translated "%1 left" frame — i18n thinking baked into the interaction, not bolted on.
- Honest indeterminate handling: when the server states no length, the bar and percent drop and raw bytes tick on — no fake spinner.

**5. Priority issues**
- **[P2] Cancel wears the ink primary pill.** Round 1 removed the fake 0.58 opacity; the overcorrection is that the transfer's least-wanted action now carries the strongest style. One slip aborts the download and deletes the bytes. Fix: keep full opacity, switch to the secondary recipe (panel2 bg / text ink) while downloading — absence of an ink pill is honest ("the right action is to wait").
- **[P2] Two reflows mid-flow.** `primaryAction("verifying")` returns `""`, so the pill vanishes and the progress column (visible only while `downloading`) collapses — the window's height changes, then again when Install appears. Fix: during `verifying` keep the pill present (a disabled "Install" or a "Verifying…" label) and hold the block's height.
- **[P2] No keyboard path at all.** The window refuses focus by design, so Tab, rings, and the DESIGN.md focus-state Do are unsatisfiable; Escape is the only key. Fix: mirror the existing Shortcut pattern — an application-level Return mapped to the current primary action. Cheap, and it gives Sam a way to install.
- **[P3] Failure copy is inconsistent about the file.** Checksum failure says "It was deleted"; the generic failure doesn't say whether partial bytes persist. Fix: one sentence of disposition in each branch.

**6. Persona red flags**
- **Sam (accessibility):** every action is mouse-only. Install — running an executable — is unreachable without a pointer; Accessible names exist but there is no traversal to deliver them.
- **Riley (stress):** a stays-on-top, center-screen card appears uninvited mid-work, and while it downloads its loudest button says Cancel — the ambient-pressure state is exactly when the easiest click does the most harm.

**7. Provocative questions**
- The card's most permanent decision (Skip this version) gets the quietest style while its most temporary one (Cancel) gets the loudest — is that mapping intentional, or just the order the states were written in?
- If the card may never take focus, what does Lens owe a reader it interrupts mid-keystroke — a quieter landing spot, or is "drag it out of the way" the whole answer?
