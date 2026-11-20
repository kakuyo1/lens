---
name: delete-comment
description: Audit and prune code comments in src/ and test/ — categorize every comment (tautology / stale / excuse / decision-record), delete the ones the code already carries, move real decisions into docs/adr/, and step the prose-comment ratchet down. Use when the user says 删注释, asks to clean up or audit comments, wants the comment ratchet lowered, mentions stale or tautological comments, or asks to rerun the comment sweep — even if they only point at one module.
---

# Delete comments: the audit sweep

The 2026-11 sweep put every comment in `src/` and `test/` through four verdicts and left only
what AGENTS.md, Code Style keeps. This skill repeats that sweep: same categories, same actions,
same ratchet. The state is held by `scripts/quality/comment-lint.sh` (baselines inside it) and
enforced by `.githooks/pre-commit` check 14.

## The verdicts and their actions

Judge every comment — `///`, `//!`, `//`, and `/* */` — against the current code, never against
its own text:

- **tautology** — restates what the adjacent code already shows (`@brief Sets the name.` on
  `setName`, `// increment i`, a section header naming the thing below). Delete.
- **stale** — references code, files, ADRs or behaviors that no longer exist or have changed.
  Verify before judging: read the referenced symbol, check the ADR exists. A comment whose
  decision is already recorded verbatim in an existing ADR is a duplicate — stale. Delete; when
  only a citation fragment is dead, prune the fragment and keep the substance.
- **excuse** — papers over a hack (`TODO this leaks`, `HACK:`, `works around`, `for now we just`).
  Delete the comment, file a GitHub issue for the hack (`docs/agents/issue-tracker.md` owns the
  `gh` mechanics). Never fix the code in the sweep.
- **decision-record** — an important, non-obvious decision that passes the three-condition test
  in `docs/adr/README.md` (costly to reverse + confusing without context + a real alternative
  was rejected) and has no ADR yet. Record verbatim, delete the comment, write the ADR (number
  = existing max + 1, the four-section template).
- **keep** — none of the above: a contract or constraint the name cannot carry (exactly what
  AGENTS.md, Code Style mandates for `@param`/`@return`/`@note`), or a gotcha pinned to that
  spot. Leave it. When torn between tautology and keep, keep.

Never delete: `/** @file` blocks (pre-commit check 9), anything inside a string literal or a
C++ raw string `R"(...)"`, and a comment whose deletion leaves a dangling `\` line-splice.

## Mechanics

- Read each file once, judge all its comments, edit, move on. `Edit` with exact-match: delete
  whole comment-only lines; for a trailing comment, strip only the comment text; for a block,
  the entire block including markers. Collapse a blank line left orphaned next to another.
  Check the diff afterward for indentation damage — a rewrite must land at the original indent.
- Comment-only change: run nothing (`AGENTS.md`, the test-time table). The proof the sweep was
  clean is `git diff` showing comment lines only, plus the build if the sweep touched C++ at
  scale — one incremental Debug build, no suites.

## Scale it

One pass over the whole tree is 8+ subagents of work. Split by directory with disjoint file
sets (`src/core`; the small modules; `src/app` root + duties; the pet/glass/platform cluster;
QML split in two; googletest; qtest), have each agent audit **and** edit its scope, and require
an inventory file (one line per judged comment: line, category, DELETED/KEPT, the text; `why:`
lines for decision-records and excuses) plus a summary of counts. The inventory is what the ADR
drafting, the issue filing and the estimate are built from, so it outlives the agents.

## Close the sweep

1. `sh scripts/quality/comment-lint.sh --report` — the new prose totals.
2. Step the baselines in that script down to the new numbers, in the same change.
3. Write the ADRs, file the excuse issues, and report: per-category counts, lines shed
   (`git diff --numstat`), and what was kept and why it earns its place.
