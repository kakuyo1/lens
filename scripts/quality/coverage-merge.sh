#!/bin/sh
# Merge the binary coverage files a scan produced into one Cobertura report.
#
# OpenCppCoverage's own merge is the union: a line any input hit is hit, and the sources filter
# keeps the report on src/. The scan can be several processes on one machine and several CI jobs
# (.github/workflows/coverage.yml cuts the surface side in two), so this is the one place that
# turns their .cov files into the single number.
#
# Usage: scripts/quality/coverage-merge.sh <cov-dir>
#   Every *.cov under the directory is read, at any depth; the report lands beside them as
#   coverage.xml.

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR=${1:-}
[ -n "$DIR" ] || { echo "coverage-merge: say which directory holds the .cov files" >&2; exit 2; }
[ -d "$DIR" ] || { echo "coverage-merge: no directory at $DIR" >&2; exit 1; }

COV='/c/Program Files/OpenCppCoverage/OpenCppCoverage.exe'
[ -x "$COV" ] || COV=$(command -v OpenCppCoverage 2>/dev/null || true)
[ -n "$COV" ] && [ -x "$COV" ] || {
    echo "coverage-merge: OpenCppCoverage is not installed; docs/metrics/profiling-and-coverage.md has the one-line install" >&2
    exit 1
}

# Backslashes: OpenCppCoverage rejects a path with forward slashes in it, even on the arguments
# that are not the program name.
win() { cygpath -w "$1"; }

covs=$(find "$DIR" -type f -name '*.cov' | sort)
[ -n "$covs" ] || { echo "coverage-merge: no .cov file under $DIR" >&2; exit 1; }

# One --input_coverage per file, the paths read line by line so none of them goes through the
# shell's word splitting.
merge=""
while IFS= read -r cov; do
    [ -n "$cov" ] || continue
    merge="$merge --input_coverage $(win "$cov")"
done <<EOF
$covs
EOF

# shellcheck disable=SC2086 # the argument list is meant to split
"$COV" $merge --sources "$(win "$ROOT/src")" --quiet \
    --export_type "cobertura:$(win "$DIR/coverage.xml")" >/dev/null

python "$ROOT/scripts/quality/coverage-summary.py" "$DIR/coverage.xml"
