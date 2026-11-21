#!/bin/sh
# Count the prose comments left in src/ and test/, and hold each count to a ratchet.
#
# The comment audit of 2026-11 (the process lives in .claude/skills/delete-comment/) put the
# tree's comments through four verdicts: tautologies, stale references and excuses were
# deleted, decisions moved into docs/adr/, and what remains is what AGENTS.md's Code Style
# keeps -- the non-obvious contracts a name cannot carry. This check is the ratchet around
# that state: prose comments may go down, never up.
#
# What counts as prose: a // line that is not a /// or //! document line, a trailing // on a
# code line, and any line inside a /* */ block that did not open as /**. A line carrying
# NOLINT (NOLINT, NOLINTNEXTLINE, NOLINTBEGIN, NOLINTEND) is a mechanical suppression the
# tidy gate needs, not prose, and is not counted; the baselines below were stepped down with
# that exemption in force, so it creates no room for comment creep. The /// form is the
# API contract AGENTS.md's Code Style mandates and a pre-commit check holds the /** @file
# blocks, so neither is this check's business. The counter has no parser, so a // or /*
# inside a string literal reads as prose; the URL forms it would most often meet
# (https?://, qrc:/) are filtered the way .githooks/pre-commit's path check filters them. A
# false positive can only make the count stricter; if one ever bites, the answer is deleting
# comments and stepping the baseline down, never raising it.
#
# The count is a ratchet, like the typography budgets in that hook and the warning count in
# qml-lint.sh: lower it when comments are deleted, never raise it to get a commit through.
#
# Scope is src/ and test/ only. third_party/ is vendored; the .githooks/, scripts/ and
# ui-prototypes/ trees own their own comments.
#
# Usage: scripts/quality/comment-lint.sh [--report]
#   --report  print the per-file counts even when every scope is under its baseline

set -u

# The 2026-11 audit left these counts behind; each only goes down. They were recomputed when
# NOLINT lines stopped counting as prose (3981 src / 2070 test), so mechanical suppressions
# create no room here.
BASELINE_SRC=3981
BASELINE_TEST=2070

root=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$root" ] || root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 0

# One awk over a file list. Per file it tracks block-comment state; a block opened as /** is
# a document block and never counts. Per line it counts: a comment-only // (not /// or //!),
# a trailing // after code (same exclusion, and not a URL), or a line inside a prose block.
# Prints "<prose-lines> <file>" per file and the scope total as the last line. Handed to
# xargs, so it lives in a variable: xargs runs programs, not shell functions.
AWK_COUNT='
    FNR == 1 {
        if (NR > 1) printf "%d %s\n", count, prev
        prev = FILENAME; inblock = 0; isdoc = 0; count = 0
    }
    {
        line = $0
        if (inblock) {
            if (!isdoc && index(line, "NOLINT") == 0) count++
            if (index(line, "*/") > 0) inblock = 0
            next
        }
        if (match(line, /\/\*/)) {
            isdoc = (substr(line, RSTART, 3) == "/**")
            if (!isdoc) count++
            if (index(substr(line, RSTART + 2), "*/") > 0) inblock = 0
            else inblock = 1
            next
        }
        l = line
        sub(/^[ \t]+/, "", l)
        p = index(l, "//")
        if (p == 0) next
        if (p == 1) {
            # A comment-only line: prose unless it opens as /// or //!, or carries a NOLINT.
            if (substr(l, 3, 1) != "/" && substr(l, 3, 1) != "!" && index(l, "NOLINT") == 0) count++
        } else if (l !~ /https?:\/\// && l !~ /qrc:\//) {
            # A trailing comment after code: prose unless it opens as /// or //!, or carries a NOLINT.
            if (substr(l, p + 2, 1) != "/" && substr(l, p + 2, 1) != "!" && index(l, "NOLINT") == 0) count++
        }
    }
    END { printf "%d %s\n", count, prev }
'

report=
[ "${1:-}" = "--report" ] && report=1

status=0
for scope in src test; do
    # The same file set git tracks, restricted to the extensions the audit covered.
    files=$(git ls-files "$scope" | grep -E '\.(h|cpp|hpp|cc|qml)$')
    [ -n "$files" ] || continue

    counts=$(printf '%s\n' "$files" | xargs awk "$AWK_COUNT")
    total=$(printf '%s\n' "$counts" | awk '{ s += $1 } END { print s + 0 }')

    if [ "$scope" = "src" ]; then baseline=$BASELINE_SRC; else baseline=$BASELINE_TEST; fi

    if [ -n "$report" ]; then
        printf '%s\n' "$counts" | awk -v scope="$scope" '$2 != "TOTAL" && $1 > 0 { printf "%s: %d\n", $2, $1 }'
    fi

    if [ "$total" -gt "$baseline" ]; then
        echo "comment-lint: $scope holds $total prose comment lines, baseline $baseline -- the baseline only goes down;"
        echo "  delete comments rather than raise it. The process is .claude/skills/delete-comment/;"
        echo "  what stays is what AGENTS.md, Code Style keeps (/// contracts), and those are not counted."
        offenders=$(printf '%s\n' "$counts" | awk '$2 != "TOTAL" && $1 > 0 { print $1, $2 }' | sort -rn | head -8)
        printf '%s\n' "$offenders" | sed 's/^/    /'
        status=1
    else
        echo "comment-lint: $scope $total prose comment lines, baseline $baseline"
    fi
done
exit $status
