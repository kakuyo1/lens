#!/bin/sh
# Run the QML surface suite one case file per process, several files at a time.
#
# The surface battery is one executable over 29 QML files and the longest step in the qml-surfaces job.
# The files have no order between them, and setup.cpp copies the settings fixture to a file named
# by the process id, so two processes never share a document. One file per process therefore
# keeps every case's state to itself, and a failure in one file does not hide the rest.
#
# Files start longest-first, by test-function count, so the tail is not one slow file; xargs
# gives a free worker the next one as soon as it can. A successful file prints its Totals line,
# a red file prints its whole log, and the log directory is kept when anything failed.
#
# Usage: scripts/qa/qtest-parallel.sh [--build DIR] [--shards N]
#   --build   build tree, default build-ninja; the tree must already be built
#   --shards  surface processes at once, default 2
#
# Run it from anywhere; the log directory is under logs/, which is gitignored. QT_ROOT, when set,
# puts Qt's bin on PATH; CI already has it there.

set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=build-ninja
SHARDS=2
while [ "$#" -gt 0 ]; do
    case "$1" in
        --build)
            [ "$#" -ge 2 ] || { echo "qtest-parallel: --build needs a directory" >&2; exit 2; }
            BUILD=$2
            shift 2
            ;;
        --shards)
            [ "$#" -ge 2 ] || { echo "qtest-parallel: --shards needs a number" >&2; exit 2; }
            SHARDS=$2
            shift 2
            ;;
        *)
            echo "qtest-parallel: unknown argument '$1'" >&2
            exit 2
            ;;
    esac
done
case "$SHARDS" in
    '' | *[!0-9]*) echo "qtest-parallel: --shards takes a whole number, not '$SHARDS'" >&2; exit 2 ;;
esac
[ "$SHARDS" -ge 1 ] || { echo "qtest-parallel: --shards must be at least 1" >&2; exit 2; }

EXE="$ROOT/$BUILD/test/qtest/lens_qtest_surfaces.exe"
[ -x "$EXE" ] || {
    echo "qtest-parallel: $EXE is not built; build lens_qtest_surfaces first" >&2
    exit 1
}
SURFACES="$ROOT/test/qtest/surfaces"
[ -d "$SURFACES" ] || { echo "qtest-parallel: $SURFACES is not there" >&2; exit 1; }

if [ -n "${QT_ROOT:-}" ]; then
    PATH="$QT_ROOT/bin:$PATH"
    export PATH
fi
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
export QT_FORCE_STDERR_LOGGING=1

mkdir -p "$ROOT/logs"
LOG_DIR=$(mktemp -d "$ROOT/logs/qtest-parallel.XXXXXX") || {
    echo "qtest-parallel: could not make a log directory under $ROOT/logs" >&2
    exit 1
}

# Function count is a cheap proxy for a file's cost and keeps the heavy cases in different
# workers. The path itself is the tie-break, so a run's assignment is reproducible.
FILES="$LOG_DIR/files"
for file in "$SURFACES"/tst_*.qml; do
    [ -f "$file" ] || continue
    weight=$(grep -c '^[[:space:]]*function test_' "$file" 2>/dev/null || true)
    printf '%s %s\n' "${weight:-0}" "$file" >> "$LOG_DIR/weighted"
done
sort -k1,1nr -k2,2 "$LOG_DIR/weighted" -o "$LOG_DIR/weighted"
cut -d' ' -f2- "$LOG_DIR/weighted" > "$FILES"

export LENS_QTEST_EXE="$EXE"
export LENS_QTEST_LOG="$LOG_DIR"
# cygpath turns the shell's POSIX path into the native path a Qt binary reads. A non-Windows
# shell has no cygpath and passes the path through unchanged.
xargs -P "$SHARDS" -n 1 -d '\n' sh -c '
    file=$1
    name=$(basename "$file" .qml)
    native=$(cygpath -m "$file" 2>/dev/null || printf "%s" "$file")
    "$LENS_QTEST_EXE" -input "$native" >"$LENS_QTEST_LOG/$name.log" 2>&1
    printf "%s\n" "$?" >"$LENS_QTEST_LOG/$name.status"
' sh < "$FILES"
XARGS_STATUS=$?

FAILED=$([ "$XARGS_STATUS" -eq 0 ] && echo 0 || echo 1)
TOTAL=$(wc -l < "$FILES" | tr -d ' ')
printf '\nqtest-parallel: lens_qtest_surfaces, %s files, %s at a time\n' "$TOTAL" "$SHARDS"
while IFS= read -r file; do
    name=$(basename "$file" .qml)
    log="$LOG_DIR/$name.log"
    totals=$(grep -E '^Totals:' "$log" 2>/dev/null | tail -1)
    status=$(cat "$LOG_DIR/$name.status" 2>/dev/null || printf '?')
    printf '  %-34s %s (exit %s)\n' "$name" "${totals:-no Totals line}" "$status"
    if [ "$status" != 0 ] || [ -z "$totals" ] || grep -q '^FAIL!' "$log" 2>/dev/null; then
        FAILED=1
        printf '\n===== %s =====\n' "$name"
        cat "$log"
        printf '\n'
    fi
done < "$FILES"

if [ "$FAILED" -eq 0 ]; then
    rm -rf "$LOG_DIR"
    exit 0
fi
printf '\nqtest-parallel: logs kept in %s\n' "$LOG_DIR"
exit 1
