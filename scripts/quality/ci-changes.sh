#!/bin/sh
# Classify a CI change set into the jobs that can be affected by it.
#
# A documentation-only push never reaches this: ci.yml's paths-ignore skips the whole workflow.
# For everything else the changes job runs this once and the build jobs read its booleans. A
# file that no build job reads -- config, a hook, a build script -- therefore runs only hygiene,
# and a QML-only change skips the unit suite while still running the app and qml jobs.
#
# Usage: scripts/quality/ci-changes.sh <base> [head]
#   Prints unit=, app=, qml= and coverage= as true or false, one per line, for $GITHUB_OUTPUT.
#   A base that git cannot resolve falls back to head's parent; a diff that fails prints all
#   four true rather than silently narrowing the gate.
#
# LENS_CI_FILES overrides the diff with a newline-separated file list, so the classification can
# be exercised without a commit. It is a test seam only; the workflows never set it.
#
# The map:
#   unit      src/ except the app's qml and shaders, the gtest suite, the pet QTest, data,
#             third_party, CMake, the configure action and the workflows
#   app       src, data, translations, third_party, CMake, the packaging and dev-smoke scripts,
#             the configure action and the workflows
#   qml       src, the qtest suite, data, translations, third_party, CMake, the QML lint and
#             parallel-runner scripts, the configure action and the workflows
#   coverage  src, test, data, CMake, and the coverage scripts
#
# The classifier itself is in every list: a change to the map runs the jobs whose inclusion it
# changes, so the new map is exercised by the run that introduces it.

set -u

base=${1:-}
head=${2:-HEAD}

if [ -n "${LENS_CI_FILES+x}" ]; then
    files=$LENS_CI_FILES
else
    if [ -z "$base" ] || ! git cat-file -e "$base^{commit}" 2>/dev/null; then
        base=$(git rev-parse "$head^" 2>/dev/null || git rev-parse "$head")
    fi
    if ! files=$(git diff --name-only "$base" "$head"); then
        echo "ci-changes: cannot diff '$base'..'$head'; every job runs" >&2
        printf 'unit=true\napp=true\nqml=true\ncoverage=true\n'
        exit 0
    fi
fi

has() {
    printf '%s\n' "$files" | grep -qE "$1"
}

# A QML or shader file in the app does not affect the unit target's C++, but every other src/
# file does. The two exclusions are what let a QML-only change skip the unit suite.
unit_src=$(printf '%s\n' "$files" | grep -E '^src/' | grep -vE '^src/app/(qml|shaders)/' || true)

unit=false
if [ -n "$unit_src" ] ||
    has '^test/googletest/|^test/qtest/pet/|^test/CMakeLists\.txt$|^data/|^third_party/|^cmake/|^CMakeLists\.txt$|^CMakePresets\.json$|^scripts/quality/ci-changes\.sh$|^\.github/(actions|workflows)/'; then
    unit=true
fi

app=false
if has '^src/|^data/|^i18n/|^third_party/|^scripts/release/|^scripts/qa/dev-smoke\.ps1$|^scripts/quality/ci-changes\.sh$|^cmake/|^CMakeLists\.txt$|^CMakePresets\.json$|^\.github/(actions|workflows)/'; then
    app=true
fi

qml=false
if has '^src/|^test/qtest/|^test/CMakeLists\.txt$|^data/|^i18n/|^third_party/|^scripts/quality/qml-lint\.sh$|^scripts/quality/qmllint-capped\.ps1$|^scripts/quality/ci-changes\.sh$|^scripts/qa/qtest-parallel\.sh$|^cmake/|^CMakeLists\.txt$|^CMakePresets\.json$|^\.github/(actions|workflows)/'; then
    qml=true
fi

coverage=false
if has '^src/|^test/|^data/|^scripts/quality/(coverage\.sh|coverage-summary\.py|qml-coverage\.sh|qml-coverage\.js|ci-changes\.sh)$|^cmake/|^CMakeLists\.txt$|^CMakePresets\.json$'; then
    coverage=true
fi

printf 'unit=%s\napp=%s\nqml=%s\ncoverage=%s\n' "$unit" "$app" "$qml" "$coverage"
