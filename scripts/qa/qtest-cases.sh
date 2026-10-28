#!/bin/sh
# The surface case files, longest first: one path per line.
#
# The weight is a file's test-function count, a cheap proxy for its cost, so the heavy cases start
# first and no run ends on one slow file; the path breaks ties, which keeps an assignment
# reproducible. Three scans read this list -- the gate's sharded run (qtest-parallel.sh) and both
# coverage scans -- so that the same split is used everywhere and no single process carries the
# whole suite.
#
# Usage: scripts/qa/qtest-cases.sh [case-dir]

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR=${1:-"$ROOT/test/qtest/surfaces"}
[ -d "$DIR" ] || { echo "qtest-cases: no directory at $DIR" >&2; exit 1; }

for file in "$DIR"/tst_*.qml; do
    [ -f "$file" ] || continue
    weight=$(grep -c '^[[:space:]]*function test_' "$file" 2>/dev/null || true)
    printf '%s %s\n' "${weight:-0}" "$file"
done | sort -k1,1nr -k2,2 | cut -d' ' -f2-
