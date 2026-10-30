#!/bin/sh
# Line coverage for the C++ libraries, through OpenCppCoverage.
#
# OpenCppCoverage reads the PDB of a binary that is already built, so there is no coverage
# build tree and no extra build flag: it instruments the Debug objects build-ninja already
# produced. See docs/metrics/profiling-and-coverage.md for the scope and what it cannot see.
#
# The work is one task per target plus one per surface case file, two tasks at a time. The
# surface suite is most of the cost -- tens of minutes under the instrumentation, where the
# other three targets together are a few -- and the gate already runs it one case file per
# process (scripts/qa/qtest-parallel.sh). Here the same split keeps one process from carrying
# the whole suite, so the wall clock is the longest case file rather than the sum.
#
# Usage: scripts/quality/coverage.sh [--cases K/N] [output-dir] [target...]
#   output-dir defaults to test/records/coverage, which is gitignored.
#   Naming one or more targets narrows the run. The default is every target that finishes
#   unattended; CI names the five it can build and leaves lens_gtest_integration out, because
#   that one drives the real mouse and clipboard and a runner has neither.
#
#   --cases K/N runs only the K-th of N slices of the surface case files. CI splits the surface
#   suite across two runners this way; each job reports on what it ran, and the jobs' .cov files
#   are merged once more afterwards (scripts/quality/coverage-merge.sh). A single job leaves the
#   flag out and runs every file.

set -eu

# Two at a time: the runner is four vCPUs and three surface processes oversubscribed it
# (the count is reasoned about in .github/workflows/ci.yml, on the surface job).
JOBS=2

CASE_SHARD=1
CASE_SHARDS=1
if [ "${1:-}" = "--cases" ]; then
    case "${2:-}" in
        */*) ;;
        *) echo "coverage: --cases takes K/N, like 1/2" >&2; exit 2 ;;
    esac
    CASE_SHARD=${2%%/*}
    CASE_SHARDS=${2#*/}
    shift 2
fi
case "$CASE_SHARD$CASE_SHARDS" in
    *[!0-9]*) echo "coverage: --cases takes K/N, both whole numbers" >&2; exit 2 ;;
esac
if [ "$CASE_SHARD" -lt 1 ] || [ "$CASE_SHARDS" -lt 1 ] || [ "$CASE_SHARD" -gt "$CASE_SHARDS" ]; then
    echo "coverage: --cases K/N needs 1 <= K <= N" >&2
    exit 2
fi

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build-ninja"

ALL_TARGETS="lens_gtest_unit lens_gtest_perf lens_gtest_integration lens_qtest_components lens_qtest_surfaces lens_qtest_pet"

OUT=${1:-test/records/coverage}
case "$OUT" in
    /* | [A-Za-z]:*) ;;
    *) OUT="$ROOT/$OUT" ;;
esac
if [ "$#" -gt 0 ]; then shift; fi
[ "$#" -gt 0 ] || set -- $ALL_TARGETS

# winget's default location, and the one the installer uses on a runner too. Fall back to a
# PATH entry so a differently-placed install still works.
COV='/c/Program Files/OpenCppCoverage/OpenCppCoverage.exe'
[ -x "$COV" ] || COV=$(command -v OpenCppCoverage 2>/dev/null || true)
[ -n "$COV" ] && [ -x "$COV" ] || {
    echo "coverage: OpenCppCoverage is not installed; docs/metrics/profiling-and-coverage.md has the one-line install" >&2
    exit 1
}
[ -d "$BUILD" ] || { echo "coverage: no build tree at $BUILD; run scripts/build/build.bat first" >&2; exit 1; }

# Backslashes: OpenCppCoverage rejects a path with forward slashes in it, even on the arguments
# that are not the program name.
win() { cygpath -w "$1"; }

target_path() {
    case "$1" in
        lens_qtest_*) echo "test/qtest/$1.exe" ;;
        *) echo "test/googletest/$1.exe" ;;
    esac
}

TASKS=$(mktemp)
trap 'rm -f "$TASKS"' EXIT

# Three of the suites carry EXCLUDE_FROM_ALL, so a tree built the ordinary way has no executable
# for the names above. Drop a target that is not there: letting it through fails inside
# OpenCppCoverage on a missing file, and the merge below then names a .cov that was never written.
{
    for name in "$@"; do
        exe="$BUILD/$(target_path "$name")"
        if [ ! -f "$exe" ]; then
            echo "coverage: $name is not built, skipping; scripts/build/build.bat --target $name builds it" >&2
            continue
        fi
        if [ "$name" = "lens_qtest_surfaces" ]; then
            # Round-robin over the weight-ordered list: the heavy files land on different
            # slices, and K/N = 1/1 keeps every one of them.
            index=0
            sh "$ROOT/scripts/qa/qtest-cases.sh" | while IFS= read -r file; do
                index=$((index + 1))
                [ $(( (index - 1) % CASE_SHARDS )) -eq $(( CASE_SHARD - 1 )) ] || continue
                printf 'case|%s|%s|%s\n' "$(basename "$file" .qml)" "$exe" "$file"
            done
        else
            printf 'target|%s|%s|\n' "$name" "$exe"
        fi
    done
} >"$TASKS"

[ -s "$TASKS" ] || { echo "coverage: none of the named targets is built" >&2; exit 1; }

# The development machine has Qt's bin off PATH; a CI runner has it on. Only the first needs
# the prefix, and prefixing a directory that does not exist would shadow nothing but is noise.
if [ -d /b/qtt/6.9.0/msvc2022_64/bin ]; then
    PATH="/b/qtt/6.9.0/msvc2022_64/bin:$PATH"
    export PATH
fi
if [ -n "${QT_ROOT:-}" ]; then
    PATH="$QT_ROOT/bin:$PATH"
    export PATH
fi
export QT_FORCE_STDERR_LOGGING=1

mkdir -p "$OUT"

# Every path handed to the tool below is absolute, and the test targets bake the source
# directory in too, so the working directory is free -- and worth moving, because
# OpenCppCoverage drops a LastCoverageResults.log in whatever one it is started from.
cd "$OUT"

LENS_COV_SOURCES=$(win "$ROOT/src")
export LENS_COV="$COV"
export LENS_COV_SOURCES
export LENS_COV_OUT="$OUT"

# The merge below reads every .cov under $OUT, so a rerun must not carry the last one's files
# into this report.
rm -f "$OUT"/*.cov "$OUT"/surfaces/*.cov

# One export per task, merged at the end. A task that exits non-zero still contributes what it
# did run, so a red case does not throw the run away.
printf 'coverage: %s tasks, %s at a time\n' "$(wc -l <"$TASKS" | tr -d ' ')" "$JOBS"
xargs -P "$JOBS" -n 1 -d '\n' sh -c '
    line=$1
    [ -n "$line" ] || exit 0
    kind=${line%%|*}
    rest=${line#*|}
    name=${rest%%|*}
    rest=${rest#*|}
    exe=${rest%%|*}
    arg=${rest#*|}

    echo "coverage: $name"
    if [ "$kind" = "case" ]; then
        mkdir -p "$LENS_COV_OUT/surfaces"
        cov="$LENS_COV_OUT/surfaces/$name.cov"
        log="$LENS_COV_OUT/surfaces/$name.log"
        child="-input $(cygpath -w "$arg")"
    else
        cov="$LENS_COV_OUT/$name.cov"
        log="$LENS_COV_OUT/$name.log"
        child=""
    fi
    module=$(basename "$exe" .exe)

    # shellcheck disable=SC2086 # the child gets these as separate arguments, and a test binary
    # takes none or one
    QT_QPA_PLATFORM=offscreen "$LENS_COV" \
        --sources "$LENS_COV_SOURCES" \
        --modules "*$module*" \
        --quiet \
        --export_type "binary:$(cygpath -w "$cov")" \
        -- "$(cygpath -w "$exe")" $child >"$log" 2>&1 ||
        echo "coverage: $name exited non-zero; its coverage is counted anyway"
' sh <"$TASKS"

# The same call is what CI's report job runs over the artifacts of several jobs.
sh "$ROOT/scripts/quality/coverage-merge.sh" "$OUT"
