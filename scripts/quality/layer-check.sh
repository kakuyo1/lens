#!/bin/sh
# Enforce the module boundaries CODING_STANDARDS.md and docs/adr/0077 name.
#
# The groups are directories, not targets: lens_app is still one library, so the compiler cannot
# stop a duty from including a glass header. This can. The rules are directional and narrow:
#
#   util/         no Qt, and no module above it. qt_log.cpp is the one exception: it is Qt-aware
#                 and compiled by lens_llm, not lens_util.
#   core/         no Qt, and nothing above it (app/, llm/, capture/, sync/, update/).
#   capture/      no ocr/ and no app/: the scan path must not be able to reach the online client.
#   ocr/          no app/; the online adapter is a capability below the facade.
#   app/duties/   no glass/, pet/, qml/, or the facade.
#   app/platform/ no duties/, glass/, pet/, qml/, or the facade.
#   app/glass/    no duties/, pet/, qml/, or the facade.
#   app/pet/      no duties/, glass/, qml/, or the facade.
#   app/geometry/ no duties/, glass/, pet/, qml/, or the facade: the surfaces and their tests
#                 share it, so it stays a leaf over Qt's value types.
#
# app/ at the root is the facade and the shared settings headers, and app/dev/ is the developer
# tool over the facade; neither is restricted. The platform and pet groups may include the
# platform window helpers, and duties may include them too: that is the allowed direction.
#
# Silent when clean; one line per violation and exit 1 otherwise.
#
# One awk pass over the whole list, not a grep per rule per file. The rules are the same either
# way, but this check runs on every commit that touches src/, and on Windows every grep is a
# process spawn: the per-file form came to about 800 of them, three minutes of fork and no work.
#
# Usage: scripts/quality/layer-check.sh

set -u

root=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$root" ] || root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 0

files=$(git ls-files src | grep -E '\.(cpp|h)$' || true)
[ -n "$files" ] || files=$(find src -type f \( -name '*.cpp' -o -name '*.h' \))

printf '%s\n' "$files" | xargs awk '
function hit(why) {
    printf "layer-check: %s:%d: %s\n", FILENAME, FNR, why
    bad = 1
}

BEGIN {
    QT         = "^[ \t]*#[ \t]*include[ \t]*[<\"]((Q[A-Za-z])|Qt)"
    APP        = "\"app/"
    CORE       = "\"core/"
    CAPABILITY = "\"(llm|capture|sync|update)/"
    OCR        = "^[ \t]*#[ \t]*include[ \t]*\"ocr/"
    DUTIES     = "\"app/duties/"
    PLATFORM   = "\"app/platform/"
    GLASS      = "\"app/glass/"
    PET        = "\"app/pet/"
    QML        = "\"app/qml/"
    FACADE     = "\"app/app_controller\\.h\""
    bad        = 0
}

{
    if (FILENAME == "src/util/qt_log.h" || FILENAME == "src/util/qt_log.cpp") next

    if (FILENAME ~ /^src\/util\//) {
        if ($0 ~ QT) hit("util is Qt-free; qt_log is the one exception")
        if ($0 ~ (CORE "|" APP "|" CAPABILITY)) hit("util depends on no other module")
    } else if (FILENAME ~ /^src\/core\//) {
        if ($0 ~ QT) hit("core is Qt-free")
        if ($0 ~ APP) hit("core is below app")
        if ($0 ~ CAPABILITY) hit("core is below the capability modules")
    } else if (FILENAME ~ /^src\/capture\//) {
        if ($0 ~ OCR) hit("capture must not reach the online OCR client")
        if ($0 ~ APP) hit("capture is below app")
    } else if (FILENAME ~ /^src\/ocr\//) {
        if ($0 ~ APP) hit("the online adapter is below the facade")
    } else if (FILENAME ~ /^src\/app\/duties\//) {
        if ($0 ~ GLASS) hit("a duty does not know the glass subsystem")
        if ($0 ~ PET) hit("a duty does not know the pet shell")
        if ($0 ~ QML) hit("C++ does not include QML")
        if ($0 ~ FACADE) hit("a duty is below the facade")
    } else if (FILENAME ~ /^src\/app\/platform\//) {
        if ($0 ~ DUTIES) hit("platform is below the use cases")
        if ($0 ~ GLASS) hit("platform does not know the glass subsystem")
        if ($0 ~ PET) hit("platform does not know the pet shell")
        if ($0 ~ QML) hit("C++ does not include QML")
        if ($0 ~ FACADE) hit("platform is below the facade")
    } else if (FILENAME ~ /^src\/app\/glass\//) {
        if ($0 ~ DUTIES) hit("the glass subsystem is below the use cases")
        if ($0 ~ PET) hit("the glass subsystem does not know the pet shell")
        if ($0 ~ QML) hit("C++ does not include QML")
        if ($0 ~ FACADE) hit("the glass subsystem is below the facade")
    } else if (FILENAME ~ /^src\/app\/pet\//) {
        if ($0 ~ DUTIES) hit("the pet shell is below the use cases")
        if ($0 ~ GLASS) hit("the pet shell does not know the glass subsystem")
        if ($0 ~ QML) hit("C++ does not include QML")
        if ($0 ~ FACADE) hit("the pet shell is below the facade")
    } else if (FILENAME ~ /^src\/app\/geometry\//) {
        if ($0 ~ DUTIES) hit("the geometry module is below the use cases")
        if ($0 ~ GLASS) hit("the geometry module does not know the glass subsystem")
        if ($0 ~ PET) hit("the geometry module does not know the pet shell")
        if ($0 ~ QML) hit("C++ does not include QML")
        if ($0 ~ FACADE) hit("the geometry module is below the facade")
    }
}

END { exit bad ? 1 : 0 }
' || exit 1

exit 0
