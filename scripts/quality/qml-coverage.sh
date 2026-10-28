#!/bin/sh
# Which parts of the QML module the two headless QTest suites reach.
#
# The profiler launches the test binary itself, handing it the QML debug connector on the way,
# so what comes out is coverage by the test suite rather than by a hand-driven session -- which
# is the number worth pairing with the C++ one. Qt has no QML line-coverage tool;
# docs/metrics/profiling-and-coverage.md says why, and what this counts instead.
#
# The surface suite is the long half, and the one whose trace is enormous: the profiler records
# an event for every binding evaluation, and a case that waits on an animation evaluates them on
# every frame. So the suite runs one case file per profiler, two at a time, through the list the
# gate's sharded run uses (scripts/qa/qtest-cases.sh). The wall clock is the longest file rather
# than the sum, and each trace is a size the analysis can walk. Two at a time is what a four-vCPU
# runner carries, which is the count the surface gate job settled on. Concurrent profilers do
# not collide: each one connects its child over a socket file of its own (qmlprofiler --verbose
# prints `-qmljsdebugger=file:...`), not over one fixed port.
#
# The same caveat as scripts/quality/coverage.sh applies: it measures an existing build tree, and adds
# nothing to it.
#
# Usage: scripts/quality/qml-coverage.sh [output-dir]

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build-ninja"
OUT=${1:-"$ROOT/test/records/qmlcov"}
case "$OUT" in
    /* | [A-Za-z]:*) ;;
    *) OUT="$ROOT/$OUT" ;;
esac

# The development machine has Qt's bin off PATH; a CI runner has it on.
if [ -d /b/qtt/6.9.0/msvc2022_64/bin ]; then
    PATH="/b/qtt/6.9.0/msvc2022_64/bin:$PATH"
    export PATH
fi
if [ -n "${QT_ROOT:-}" ]; then
    PATH="$QT_ROOT/bin:$PATH"
    export PATH
fi
command -v qmlprofiler >/dev/null 2>&1 || {
    echo "qml-coverage: qmlprofiler is not on PATH; see AGENTS.md for where Qt lives" >&2
    exit 1
}
[ -d "$BUILD" ] || { echo "qml-coverage: no build tree at $BUILD; run scripts/build/build.bat first" >&2; exit 1; }

mkdir -p "$OUT"
export QT_QPA_PLATFORM=offscreen
export QT_FORCE_STDERR_LOGGING=1

# The three event kinds the analysis reads. Asking for less means a smaller trace; asking for
# more (creating, compiling, painting) would only add events that name no binding.
INCLUDE=binding,handlingsignal,javascript

# A trace of the previous run would be read as one of this run's.
rm -f "$OUT"/*.qtd

echo "qml-coverage: lens_qtest_components"
qmlprofiler --output "$(cygpath -m "$OUT/lens_qtest_components.qtd")" --include "$INCLUDE" \
    "$(cygpath -m "$BUILD/test/qtest/lens_qtest_components.exe")" >"$OUT/lens_qtest_components.log" 2>&1 ||
    echo "qml-coverage: lens_qtest_components exited non-zero; its trace is read anyway"

SURFACES="$BUILD/test/qtest/lens_qtest_surfaces.exe"
CASES=$(sh "$ROOT/scripts/qa/qtest-cases.sh")
[ -n "$CASES" ] || { echo "qml-coverage: no surface case files" >&2; exit 1; }

echo "qml-coverage: lens_qtest_surfaces, one case file per profiler, 2 at a time"
LENS_QMLCOV_EXE=$(cygpath -m "$SURFACES")
LENS_QMLCOV_OUT="$OUT"
LENS_QMLCOV_INCLUDE="$INCLUDE"
export LENS_QMLCOV_EXE LENS_QMLCOV_OUT LENS_QMLCOV_INCLUDE
printf '%s\n' "$CASES" | xargs -P 2 -n 1 -d '\n' sh -c '
    case=$1
    [ -n "$case" ] || exit 0
    name=$(basename "$case" .qml)
    qmlprofiler --output "$(cygpath -m "$LENS_QMLCOV_OUT/$name.qtd")" --include "$LENS_QMLCOV_INCLUDE" \
        "$LENS_QMLCOV_EXE" -input "$(cygpath -m "$case")" >"$LENS_QMLCOV_OUT/$name.log" 2>&1 ||
        echo "qml-coverage: $name exited non-zero; its trace is read anyway"
' sh

traces=0
set --
for trace in "$OUT"/*.qtd; do
    [ -f "$trace" ] || continue
    set -- "$@" --trace "$(cygpath -m "$trace")"
    traces=$((traces + 1))
done
[ "$traces" -gt 0 ] || { echo "qml-coverage: no trace was written" >&2; exit 1; }

expected=$(printf '%s\n' "$CASES" | grep -c .)
[ "$traces" -eq $((expected + 1)) ] ||
    echo "qml-coverage: $traces traces for $expected surface cases plus the components suite" >&2

exec node "$ROOT/scripts/quality/qml-coverage.js" "$@" --json "$(cygpath -m "$OUT/qml-coverage.json")"
