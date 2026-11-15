#!/bin/sh
# clang-tidy over the files this change touches.
#
# The scope is the same one .githooks/pre-commit keeps everywhere else: only what this change
# touches is held to the bar, so the tree converges instead of a baseline accreting. A header
# alone in the diff is checked through the .cpp files that include it -- clang-tidy can only
# enter a header through a compilation unit, so the TU list is recovered by searching the
# repositories' .cpp files for the header's basename. The basename is an over-approximation
# on purpose: a like-named header elsewhere pulls in an extra TU, never skips a needed one.
#
# .clang-tidy owns the checks, the header filter and the warnings-as-errors mapping, so this
# script carries no check state of its own; a hand run of clang-tidy against the same tree
# gates identically. The config's HeaderFilterRegex is what keeps the vendored headers out:
# a diagnostic inside third_party or Qt is suppressed, and a suppressed diagnostic is not
# fatalised by WarningsAsErrors, so the gate only ever judges our own files.
#
# Needs a configured build tree: the compile commands are where the flags come from, and a
# tree without them cannot be judged. A machine with no tree at all says so and passes, the
# same contract the other checks in .githooks/pre-commit keep; a tree that *was* configured
# and carries no compile_commands.json does not pass -- CMake always writes one
# (CMAKE_EXPORT_COMPILE_COMMANDS is ON in both presets), so a missing file means the presets
# changed and this script's assumption with them. A check that skips and passes is not a check.
#
# The tool comes from PATH first and then from config's vsLlvmBin, the copy Visual Studio
# ships -- the same source the clang-format fallback uses. LENS_TIDY_STRICT=1 (the CI job
# sets it) turns both "tool missing" and "no tree" into failures: on the gate a silent skip
# would be a green run checking nothing.
#
# A .cpp in the diff that no compile command names is reported and left out: clang-tidy
# without flags would judge nothing useful, and the file is not part of any build -- a gap
# the build jobs are the gate for, not this one.
#
# The gate runs the diff, never the tree by itself. LENS_TIDY_WHOLE_TREE=1 replaces the diff
# with every TU the database names -- the audit a config change wants, run by hand. It used
# to fire by itself whenever .clang-tidy or this script changed, on the ground that the
# config is the one file whose correctness is every other file's question; the whole tree
# costs over an hour on the CI runner (four cores against the tree's 276 TUs), so it became
# a hand run. The property it protected holds without it: the shipped list is only shippable
# because the tree passes it, and a diagnostic the config could name arrives on the change
# that touched its file.
#
# Usage: scripts/quality/tidy.sh <base> [head]
#   <base>  the commit the change is measured against; a base git cannot resolve falls back
#           to head's parent, the same fallback scripts/quality/ci-changes.sh keeps.
#   LENS_TIDY_FILES overrides the diff with a newline-separated file list, so the selection
#   can be exercised without a commit. It is a test seam only; the workflow never sets it.

set -u

root=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$root" ] || root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 0

strict=${LENS_TIDY_STRICT:-0}

# --- the build tree ----------------------------------------------------------

# build-ninja-tidy first: the local tree for this script, configured once with
# CMAKE_DISABLE_PRECOMPILE_HEADERS=ON and never built. build-ninja and build come after it so
# a machine that only has the dev tree still finds a db.
db=""
configured=""
for candidate in build-ninja-tidy build-ninja build; do
    if [ -f "$candidate/compile_commands.json" ]; then
        db="$candidate/compile_commands.json"
        break
    fi
    if [ -f "$candidate/build.ninja" ]; then
        configured="$candidate"
    fi
done
if [ -z "$db" ]; then
    if [ -n "$configured" ]; then
        echo "tidy: $configured is configured but carries no compile_commands.json, so the"
        echo "  flags clang-tidy needs are not there. Reconfigure the tree; if the presets"
        echo "  stopped exporting compile commands, that is the assumption that has to change."
        exit 1
    fi
    if [ "$strict" = "1" ]; then
        echo "tidy: no configured build tree on the gate"
        exit 1
    fi
    echo "tidy: no configured build tree; configure one first (skip)"
    exit 0
fi

# The dev tree's compile commands carry the targets' precompiled headers, and clang cannot read
# the MSVC PCH format -- every PCH-backed TU fails with "not a valid precompiled PCH file" and
# the gate would judge a fraction of the tree without saying why. A tree configured with
# CMAKE_DISABLE_PRECOMPILE_HEADERS=ON (configure-only; the tidy tree is never built) is the
# requirement, and a db that still names cmake_pch means it was not met.
if grep -q 'cmake_pch' "$db"; then
    echo "tidy: $db carries the targets' precompiled headers, which clang cannot read -- the"
    echo "  PCH-backed files would all fail, not all be judged. Configure the tree with"
    echo "  CMAKE_DISABLE_PRECOMPILE_HEADERS=ON, e.g.:"
    echo "    cmake -S . -B build-ninja-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug \\"
    echo "      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON"
    echo "  (from a vcvars shell; configure-only, the tree is never built)"
    exit 1
fi

# --- the tool ----------------------------------------------------------------

tidy=$(command -v clang-tidy 2>/dev/null || true)
if [ -z "$tidy" ]; then
    if command -v powershell >/dev/null 2>&1 && [ -f config/paths.default.json ]; then
        llvmBin=$(powershell -NoProfile -ExecutionPolicy Bypass -Command \
            ". ./scripts/build/paths.ps1; (Get-LensPaths -Root \$PWD).vsLlvmBin" 2>/dev/null | tr -d '\r')
        if [ -n "$llvmBin" ] && [ -x "$llvmBin/clang-tidy.exe" ]; then
            tidy="$llvmBin/clang-tidy.exe"
        fi
    fi
fi
if [ -z "$tidy" ]; then
    if [ "$strict" = "1" ]; then
        echo "tidy: clang-tidy not found on PATH or beside config's vsLlvmBin"
        exit 1
    fi
    echo "tidy: clang-tidy not found (skip)"
    exit 0
fi

# --- the file selection ------------------------------------------------------

# The diff by default; LENS_TIDY_FILES is a test seam and LENS_TIDY_WHOLE_TREE the hand audit.
if [ -n "${LENS_TIDY_FILES+x}" ]; then
    changed=$LENS_TIDY_FILES
    whole_tree=
elif [ -n "${LENS_TIDY_WHOLE_TREE:-}" ]; then
    whole_tree=1
else
    whole_tree=
    if [ $# -eq 0 ]; then
        echo "tidy: usage: scripts/quality/tidy.sh <base> [head]  (or set LENS_TIDY_FILES)"
        exit 1
    fi
    base=$1
    head=${2:-HEAD}
    # A base git cannot resolve falls back to head's parent -- a push whose "before" is the null
    # sha (a brand-new branch) is the case ci-changes.sh keeps the same fallback for -- and a
    # diff that still fails is a hard error, because a gate that measures nothing would pass
    # everything.
    if ! git cat-file -e "$base^{commit}" 2>/dev/null; then
        base=$(git rev-parse "$head^" 2>/dev/null) || true
    fi
    if ! changed=$(git diff --name-only --diff-filter=ACM "$base" "$head"); then
        echo "tidy: cannot diff '$base'..'$head'"
        exit 1
    fi
fi

tus=
if [ -n "$whole_tree" ]; then
    # The pattern clang-tidy matches against the database is the path as the database stores
    # it: backslash-separated inside its JSON strings. The sed halves the backslashes, tr
    # points them forward again. The tree's sources sit at any depth under src/ or test/ --
    # generated code (.qt/.rcc/qmlcache, autogen's mocs_compilation.cpp, the qmltype and plugin
    # shims) and the vendored trees stay out. All of the generated ones live inside the build
    # tree, whose name comes from the database, so a configure-only tree's unmaterialised names
    # (and any new generated name CMake adds later) leave the list without being guessed one by
    # one; the explicit _autogen arm keeps the common case readable.
    builddir=$(dirname "$db")
    tus=$(grep -oE '"file": "[^"]+"' "$db" \
        | sed -e 's#^"file": "##' -e 's#"$##' -e 's#\\\\#/#g' \
        | tr '\\/' '//' \
        | grep -E '/(src|test)/.*\.cpp$' \
        | grep -vE "/$builddir/" \
        | grep -vE '/(third_party|\.qt|\.rcc|qmlcache|[^/]*_autogen)/' | sort -u)
else
    files=
    for file in $changed; do
        case "$file" in
            src/*.cpp | src/*.h | test/*.cpp | test/*.h) files="$files $file" ;;
        esac
    done

    for file in $files; do
        case "$file" in
            *.cpp)
                # The database stores the same path with doubled backslashes; a hit means it
                # is compiled, and a miss means it compiles nowhere.
                if grep -qF "$(printf '%s' "$file" | sed 's,/,\\\\,g')" "$db"; then
                    tus="$tus $file"
                else
                    echo "tidy: $file is in the diff but in no compile command; not part of any build"
                fi
                ;;
            *.h)
                header=$(basename "$file")
                for tu in $(git ls-files '*.cpp' | grep -E '^(src|test)/'); do
                    # The include carries the header's path ("util/log.h", <log.h>), so the
                    # basename is matched up to its closing quote or angle, not at line start.
                    # The match only decides which TUs get checked, so erring wide is the safe
                    # side.
                    if grep -qE "$header[\">]" "$tu" 2>/dev/null; then
                        tus="$tus $tu"
                    fi
                done
                ;;
        esac
    done
fi

# --- the TUs clang cannot parse ----------------------------------------------

# Two incompatibilities in this Qt/MSVC pairing, each entirely outside our sources. A listed TU
# is reported as skipped, never silently passed; when a Qt or SDK upgrade lets one parse again,
# the message below is what stops matching and the file rejoins the gate by itself.
clang_skips() {
    case "$1" in
        # Windows SDK's directxmath.h includes <cpuid.h>, a clang-libc header the cl-mode driver
        # does not search.
        src/app/glass/glass_capture.cpp) return 0 ;;
        # Qt's qplugin.h computes offsets in constexpr, which clang rejects where MSVC accepts.
        src/app/pet/pet_assets.cpp | src/app/pet/pet_scenes.cpp | src/pictures/picture_cache.cpp | src/pictures/picture_pure.cpp) return 0 ;;
        # A QTest target ends on the include of its own .moc, which only exists after a build --
        # and the tidy tree is never built.
        test/qtest/setup.cpp | test/qtest/pet/tst_pet_controller.cpp) return 0 ;;
    esac
    return 1
}

tus=$(printf '%s\n' $tus | sort -u)
skipped=0
keep=
for tu in $tus; do
    if clang_skips "$tu"; then
        skipped=$((skipped + 1))
    else
        keep="$keep $tu"
    fi
done
if [ "$skipped" -gt 0 ]; then
    echo "tidy: skipping $skipped compilation unit(s) clang cannot parse against this toolchain;"
    echo "  the reasons live in clang_skips in this script"
fi
tus=$(printf '%s\n' $keep | sort -u)
if [ -z "$tus" ]; then
    echo "tidy: no compilation unit this change can reach"
    exit 0
fi

# --- the run -----------------------------------------------------------------

# Every TU runs in parallel with its own log: a diff can name a widely included header, so the
# TU list can reach most of the tree, and the CI runner has four cores. The gate judges a log
# by its diagnostics rather than by clang-tidy's exit code, because the shapes that must fail
# are not all visible there: a TU clang could not finish at all ("Error while processing") is a
# judgement that was skipped, and a skipped judgement that passes is exactly what this gate
# exists to refuse.
#
# Diagnostics carry a location and a check name in brackets ("D:\...\log.h:44:46: error: ...
# [performance-enum-size,-warnings-as-errors]"); the tool's own failures ("error: no input
# files") have neither. Both fail. The earlier whole-tree branch matched only the bare form,
# which is why a tree full of findings could report itself clean.

out=${TMPDIR:-/tmp}/lens-tidy.$$
mkdir -p "$out"
printf '%s\n' $tus | xargs -P 8 -I{} sh -c \
    '"$1" -p '"$db"' --quiet "{}" > "$2/$(echo "{}" | cksum | cut -c1-8).log" 2>&1' _ "$tidy" "$out" || true
rc=0
for tu in $tus; do
    log="$out/$(echo "$tu" | cksum | cut -c1-8).log"
    if [ ! -f "$log" ]; then
        echo "tidy: no log for $tu; the run did not cover it"
        rc=1
        continue
    fi
    if grep -qE '(^error: |(^|: )(error|warning): .*\[[A-Za-z0-9_.-]+(,-[A-Za-z-]+)*\][[:space:]]*$)|^Error while processing' "$log"; then
        echo "tidy: violations in $tu"
        cat "$log"
        rc=1
    fi
done
rm -rf "$out"
[ "$rc" = "0" ] && echo "tidy: $(echo $tus | wc -w) compilation units, clean"
exit "$rc"
