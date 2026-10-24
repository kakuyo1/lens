---
target: 设置浮层（修复流② re-critique）
total_score: 31
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
target_fingerprint: "sha256:4a76b3d02451f729ef5125b8ee21def13fcff6d55ac8e097ca7e04fb1a1acda8"
target_path: "C:\\Users\\wdmzs\\Desktop\\ClaudeCode\\demo\\src\\app\\qml\\settings\\SettingsPopup.qml"
timestamp: 2026-10-24T13-58-34Z
slug: src-app-qml-settings-settingspopup-qml
---
# Assessment A — design-director review, `SettingsPopup.qml` (re-critique round 3)

**1. Specificity verdict**: Authored for this product. The bubble-reuse preview channel, live-effect settings with no Save button, note semantics split by state-vs-value, and reason lines tied to pet/OCR gating are all grounded in Lens's model — a stock settings modal could not wear any of it.

**2. Nielsen scores (31/40, Good)**

| # | Heuristic | Score | Key issue |
|---|---|---|---|
| 1 | System status | 3 | No scroll indicator in the body — a full learning page hides 5+ switches with no hint they exist |
| 2 | Real world | 3 | "token", "API key" appear bare on model/connection pages with no inline gloss |
| 3 | User control | 3 | Esc ladder (question→route→category→close) is excellent; Defaults is instant and irreversible |
| 4 | Consistency | 3 | Back icon drawn in `muted`, close in `faint` — same header class, two steps |
| 5 | Error prevention | 3 | Hotkey-conflict line, availability gating, editable model never clobbered; Defaults unguarded |
| 6 | Recognition | 4 | Hover previews render the reader's own bubble; rows carry their values/states as notes |
| 7 | Flexibility | 3 | Full keyboard grammar per spec; no type-to-jump across the 7-category catalog |
| 8 | Minimalist | 3 | Quiet three-step ramp; learning page is a flat run, model page's lower half is dead |
| 9 | Error recovery | 3 | OCR status names each cause ("English OCR data is missing"); recovery is always inline |
| 10 | Help/docs | 3 | The preview card is the best contextual help; nothing answers "how do I get an API key" |

**3. Cognitive load — failures only (2, moderate)**
- Chunking: learning page stacks 11 controls in one column; 8 are flat switch rows.
- Grouping: the 7 switches that all shape the word card are visually indistinguishable from unrelated ones; the capture page containers its trio, learning doesn't.

**4. Strengths**
- **The preview channel**: hovering a title raises the real explanation bubble, rendered with the reader's current font/language/typing pace, placed against the panel, marked only by the Preview pill and dashed frame. It teaches by rendering the product's own output — recognition without a docs page.
- **Row grammar with honest notes**: `SettingsLinkRow`'s `noteIsState` gives one 11px slot two truthful meanings (muted status vs faint value), and the row ships Tab, Enter/Space, `Accessible.role`, and FocusRing by default — craft other rows should inherit.
- **Reason at the gate**: pet-gated dropdown and OCR switches state the cause (muted 12px) at the dimmed control, not in a help page; the screenshot shows it reads calmly, not as an error.

**5. Priority issues** (excluding the fixed footer channel, note steps, and reason lines)
- **[P1] Body scroll is invisible and keyboard-blind.** The Flickable in SettingsPopup.qml carries no ScrollBar — UI-SPEC 4.4 explicitly specs a thin `faint` slider on a transparent track — and nothing reveals the focused item, so Tab can vanish below the fold silently. Fix: attach the spec'd thin ScrollBar and keep `activeFocusItem` inside the viewport on focus change.
- **[P1] Footer Defaults pill is mouse-only.** It's a Rectangle + HoverHandler + TapHandler: no Tab stop, no Accessible role/name, no focus ring, no pressed state — breaking the spec's keyboard-reachability list and DESIGN.md's "every clickable element has a focus state". Fix: promote it to a real button with press fill (like the catalog rows), focus ring, Accessible name.
- **[P2] Defaults is a one-tap global reset with no undo.** It silently rewrites ~16 settings (theme, font, hotkey, bubble timing); the status line only confirms what already happened, and the 27px pill sits permanently under the scroll area. The vocabulary delete already has a two-button confirm precedent. Fix: two-step pill (flips to 确认 for a beat) or a snapshot + 撤销 in the status line.
- **[P2] The learning page never names its own group.** Seven switches share one concept — what the word card draws — but no label says so; the capture page names and containers its trio, so the sibling page reads as noise. Fix: one 11px muted group label above the run (a label, not a container; the spec's "唯一分组容器" stays true).
- **[P3] Model price line breaks the mono rule.** "输入 0.15 / 输出 0.6 USD / 每 1000000 个 token" is CJK+amounts in a plain Text — digits leave Cascadia (DESIGN.md requires MixedText for mixed values) and 1000000 has no grouping. Fix: MixedText + thousand separators.

**6. Persona red flags** (Alex + Sam, admin-type surface)
- **Alex (power user)**: the richest information on the surface — every preview — is bound to hover, an input his keyboard never sends; his fastest click (Defaults) is the most destructive one; no way to type-jump to a category in a 7-row catalog.
- **Sam (accessibility)**: the Defaults pill is unreachable by Tab and invisible to screen readers; body scroll has no indicator and no focus reveal, so his focus disappears below the fold; the pet-gated reason line is a sibling Text with no accessible relation to the control it explains; on glass themes muted/faint sit below AA (documented boundary, still his daily reality).

**7. Provocative questions**
- If every setting applies live and the preview bubble already teaches by rendering the real thing, what is a global Defaults bomb in the footer still doing — would per-row "reset this" make it obsolete?
- The bubble is the product's best explanation of itself; why is its settings twin reserved for a pointer? What would a keyboard-first preview look like?
