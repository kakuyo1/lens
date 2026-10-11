<!--
How a pull request here is written. GitHub fills this in; replace every section and delete the
guidance. English, like the commit messages it summarises -- the design docs are Chinese, the
technical record is not.

Say what was wrong and what owns the behaviour now. Link the ADR or the doc that holds the
reasoning instead of repeating it, and spell `GLOSSARY.md` terms the way that file does. No AI
attribution anywhere in the body (`AGENTS.md`).
-->

## Summary

<!--
The smallest view that makes the point: a diff, a call tree, a file tree, a state flow. Prose only
when no shape carries it.
-->

## Evidence

- **Before:**
  **After:**

<!--
Executable, not asserted: a case name and its result, a command and its output, the exact run.

Before is what the reader saw, or what nothing was catching; After is that same thing pinned. Name
the suites that ran, and the ones left to a human with the reason -- TEST.md section 2 has the
recipes and says when each is worth running.
-->

## Merge Danger

**Door:** <!-- one-way when a revert loses something or a persisted format moves; two-way otherwise -->

**Blast Radius:** <!-- one word -->

<!--
What a revert does not undo, and who feels it: the surfaces that read this, the data on disk, the
wire format. If a test or a doc is what would catch a mistake here, name it.
-->
