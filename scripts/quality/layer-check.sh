#!/bin/sh
# Enforce the module boundaries CODING_STANDARDS.md and docs/adr/0077 name.
#
# The groups are directories, not targets: lens_app is still one library, so the compiler cannot
# stop a duty from including a glass header. This can. The rules are directional and narrow:
#
#   util/         no Qt, and no module above it. qt_log.cpp is the one exception: it is Qt-aware
#                 and compiled by lens_llm, not lens_util.
#   core/         no Qt, and nothing above it (app/, llm/, capture/, sync/, update/).
#   app/duties/   no glass/, pet/, qml/, or the facade.
#   app/platform/ no duties/, glass/, pet/, qml/, or the facade.
#   app/glass/    no duties/, pet/, qml/, or the facade.
#   app/pet/      no duties/, glass/, qml/, or the facade.
#
# app/ at the root is the facade and the shared settings headers, and app/dev/ is the developer
# tool over the facade; neither is restricted. The platform and pet groups may include the
# platform window helpers, and duties may include them too: that is the allowed direction.
#
# Silent when clean; one line per violation and exit 1 otherwise.
#
# Usage: scripts/quality/layer-check.sh

set -u

root=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$root" ] || root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 0

QT='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](Q[A-Za-z]|Qt)'
APP='"app/'
CORE='"core/'
CAPABILITY='"(llm|capture|sync|update)/'
DUTIES='"app/duties/'
PLATFORM='"app/platform/'
GLASS='"app/glass/'
PET='"app/pet/'
QML='"app/qml/'
FACADE='"app/app_controller\.h"'

status=0

# Prints each offending line, then records the failure. The pipe is a subshell, so status is
# assigned after it rather than inside the loop.
forbid() {
    file=$1
    pattern=$2
    why=$3

    hits=$(grep -nE "$pattern" "$file" || true)
    [ -n "$hits" ] || return 0

    printf '%s\n' "$hits" | while IFS=: read -r line _; do
        printf 'layer-check: %s:%s: %s\n' "$file" "$line" "$why"
    done
    status=1
}

files=$(git ls-files src | grep -E '\.(cpp|h)$' || true)
[ -n "$files" ] || files=$(find src -type f \( -name '*.cpp' -o -name '*.h' \))

for file in $files; do
    case "$file" in
        src/util/qt_log.h|src/util/qt_log.cpp)
            ;;
        src/util/*)
            forbid "$file" "$QT" 'util is Qt-free; qt_log is the one exception'
            forbid "$file" "$CORE|$APP|$CAPABILITY" 'util depends on no other module'
            ;;
        src/core/*)
            forbid "$file" "$QT" 'core is Qt-free'
            forbid "$file" "$APP" 'core is below app'
            forbid "$file" "$CAPABILITY" 'core is below the capability modules'
            ;;
        src/app/duties/*)
            forbid "$file" "$GLASS" 'a duty does not know the glass subsystem'
            forbid "$file" "$PET" 'a duty does not know the pet shell'
            forbid "$file" "$QML" 'C++ does not include QML'
            forbid "$file" "$FACADE" 'a duty is below the facade'
            ;;
        src/app/platform/*)
            forbid "$file" "$DUTIES" 'platform is below the use cases'
            forbid "$file" "$GLASS" 'platform does not know the glass subsystem'
            forbid "$file" "$PET" 'platform does not know the pet shell'
            forbid "$file" "$QML" 'C++ does not include QML'
            forbid "$file" "$FACADE" 'platform is below the facade'
            ;;
        src/app/glass/*)
            forbid "$file" "$DUTIES" 'the glass subsystem is below the use cases'
            forbid "$file" "$PET" 'the glass subsystem does not know the pet shell'
            forbid "$file" "$QML" 'C++ does not include QML'
            forbid "$file" "$FACADE" 'the glass subsystem is below the facade'
            ;;
        src/app/pet/*)
            forbid "$file" "$DUTIES" 'the pet shell is below the use cases'
            forbid "$file" "$GLASS" 'the pet shell does not know the glass subsystem'
            forbid "$file" "$QML" 'C++ does not include QML'
            forbid "$file" "$FACADE" 'the pet shell is below the facade'
            ;;
    esac
done

exit $status
