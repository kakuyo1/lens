#!/bin/sh
# Format a C++ file in place when an edit lands on it, so the drift never reaches a commit.
#
# Wired as a PostToolUse hook in .claude/settings.json. It reads the hook payload on stdin and
# takes tool_input.file_path from it. .githooks/pre-commit checks the same files at commit time
# and refuses a commit that is not formatted; this makes the edit formatted before that point.
#
# Only files inside this checkout are touched, because a .cpp outside it would be formatted
# against the wrong .clang-format or none. third_party/ is left alone, as the pre-commit check does.
#
# The formatter is found the way check_formatting() in .githooks/pre-commit finds it: clang-format
# from PATH, else the copies under clangFormatFallbacks in config/paths.default.json. lens_path_list is
# copied from that hook for the same reason. If one of them changes, change both.
#
# A missing jq, a missing formatter or a file that is not there exits 0 and says nothing: the
# check is optional on a machine without the tools. A formatter that runs and fails is reported.

root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$root" || exit 0

file=$(jq -r '.tool_input.file_path // empty' 2>/dev/null) || exit 0
case "$file" in
    "$root"/*) ;;
    *) exit 0 ;;
esac
case "${file#"$root"/}" in
    third_party/*) exit 0 ;;
    *.cpp | *.h | *.hpp | *.cc) ;;
    *) exit 0 ;;
esac
[ -f "$file" ] || exit 0

lens_path_list() {
    command -v powershell >/dev/null 2>&1 || return 0
    [ -f config/paths.default.json ] || return 0
    powershell -NoProfile -ExecutionPolicy Bypass -Command \
        ". ./scripts/build/paths.ps1; Get-LensPathList -Root \$PWD -Key $1" 2>/dev/null | tr -d '\r'
}

formatter=$(command -v clang-format 2>/dev/null || true)
if [ -z "$formatter" ]; then
    while read -r candidate; do
        if [ -x "$candidate" ]; then
            formatter="$candidate"
            break
        fi
    done <<EOF
$(lens_path_list clangFormatFallbacks)
EOF
fi
[ -n "$formatter" ] || exit 0

"$formatter" -i "$file"
