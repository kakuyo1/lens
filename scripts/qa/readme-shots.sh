#!/bin/sh
# The README's screenshots: one set per language, out of the prototypes the README points at.
#
# Four READMEs ship -- English, Chinese, Japanese and Spanish -- and each one shows the same images.
# The images are screenshots of ui-prototypes/*.html, which take ?lang= the way they take ?theme=, so
# the four sets differ only in what the surface says. Re-rendering them by hand is what left the
# English README showing Chinese chrome; this is that rendering, written down.
#
# The prototype is loaded through a one-page frame rather than directly. Headless Chrome clamps a
# window to a floor near 500x100, so --window-size=340,200 lays the page out wider than the shot
# and the panel lands off-centre; an iframe of the wanted size inside a page of any size hands the
# prototype the viewport it was drawn for. The frame is written to a temporary directory, because
# a committed one would have to spell out this repository's path and the pre-commit check for
# absolute paths exists to stop exactly that.
#
# ?shot drops the theme toggle: an aid for reading a prototype, not part of the surface, and in no
# screenshot. ?lang is left off for Chinese, which is what the prototypes open as.
#
# Chrome is a machine path, and this file is committed, so it is looked for under the environment
# variables Windows sets instead of in config/paths.default.json -- see config/README.md for why the one
# file that may hold a machine path is not this one.
#
# pet-ui.png is not rendered here. The pet is shown as the running application draws it, so this
# script leaves that image alone.
#
# Usage: scripts/qa/readme-shots.sh
#   Rewrites docs/readme-images/<lang>/. A prototype's rows are all rendered when it is named:
#   scripts/qa/readme-shots.sh bubble.html

set -eu

root=$(git rev-parse --show-toplevel) || exit 1
cd "$root" || exit 1

chrome=
if command -v chrome.exe >/dev/null 2>&1; then
    chrome=$(command -v chrome.exe)
else
    for var in PROGRAMFILES 'PROGRAMFILES(X86)' LOCALAPPDATA; do
        dir=$(printenv "$var" 2>/dev/null || true)
        [ -n "$dir" ] || continue
        for candidate in "$dir/Google/Chrome/Application/chrome.exe" "$dir/Chrome/Application/chrome.exe"; do
            if [ -x "$candidate" ]; then chrome=$candidate; break; fi
        done
        [ -n "$chrome" ] && break
    done
fi
if [ -z "$chrome" ]; then
    echo "readme-shots: no Chrome found under PROGRAMFILES, PROGRAMFILES(X86) or LOCALAPPDATA" >&2
    exit 1
fi

# name:surface:width:height:state:theme -- one image per row, written to docs/readme-images/<lang>/<name>.png.
# The state is the one query key a surface is drawn in besides its default, empty for none. It goes
# through the frame's x= passthrough, so it holds a single key and no ampersand. The theme is the
# prototype's ?theme=. Every bubble image is the same size, so the cards line up in the README's
# tables with each card centred in its frame.
SHOTS='bubble-ui:bubble.html:340:330::light
entity-ui:bubble.html:340:330:kind=entity:light
sentence-ui:bubble.html:340:330:kind=sentence:light
theme-light:bubble.html:340:330::light
theme-dark:bubble.html:340:330::dark
theme-frosted:bubble.html:340:330::frosted
theme-liquid:bubble.html:340:330::liquid
selection-bar-ui:selection-bar.html:600:150::light
settings-ui:settings.html:440:540::light
words-ui:words.html:380:500::light
year-in-words-ui:year-in-words.html:440:430:palette=open:light'

# The language directories, and the ?lang each one passes. Chinese is the prototypes' own text,
# so it passes nothing.
LANGS='en:en
zh-CN:
ja:ja
es:es'

wanted=${*:-}

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

if ! command -v cygpath >/dev/null 2>&1; then
    echo "readme-shots: cygpath not found; this script is meant to run under Git Bash" >&2
    exit 1
fi
# -m keeps the drive letter and the forward slashes, which is what a file:// URL wants.
win_tmp=$(cygpath -m "$tmp")
win_root=$(cygpath -m "$root")

cat > "$tmp/frame.html" <<FRAME
<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;padding:0;overflow:hidden;background:#e9ebef}
iframe{position:absolute;left:0;top:0;border:0;display:block}
</style></head><body>
<iframe id="f" width="10" height="10"></iframe>
<script>
const q = new URLSearchParams(location.search);
const f = document.getElementById('f');
const lang = q.get('lang');
f.width = q.get('w'); f.height = q.get('h');
f.src = 'file:///${win_root}/ui-prototypes/' + q.get('p')
      + '?theme=' + (q.get('theme') || 'light') + '&shot=1'
      + (lang ? '&lang=' + lang : '') + (q.get('x') ? '&' + q.get('x') : '');
</script></body></html>
FRAME

shot() {
    name=$1; surface=$2; lang_dir=$3; lang=$4; w=$5; h=$6; state=$7; theme=$8
    out="docs/readme-images/$lang_dir/$name.png"
    mkdir -p "$(dirname "$out")"
    # A profile of its own per run: Chrome keeps window geometry in one, and a shared directory
    # that another run still holds is a run that renders nothing. The output is named by its
    # absolute path because Chrome resolves a relative one against its own working directory,
    # which is not this one.
    "$chrome" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$tmp/profile-$lang_dir-$name" \
        --window-size="$w,$h" --screenshot="$win_root/$out" \
        "file:///$win_tmp/frame.html?p=$surface&w=$w&h=$h&lang=$lang&theme=$theme&x=$state" \
        >>"$tmp/chrome.log" 2>&1
    # Chrome exits 0 whether or not it wrote anything -- a page it could not load is still a
    # screenshot of a blank one -- so the file is what says the run happened. A silent empty
    # directory is the failure this line exists to turn into a loud one.
    if [ ! -s "$out" ]; then
        echo "readme-shots: $out was not written; see $tmp/chrome.log" >&2
        failed=$((failed + 1))
        return 0
    fi
    echo "  $out"
}

default_ifs=$IFS
failed=0
for row in $SHOTS; do
    # The row's fields are split on the colons, which the default separators would not do.
    IFS=:
    set -- $row
    IFS=$default_ifs
    name=$1 surface=$2 w=$3 h=$4 state=$5 theme=$6
    [ -z "$wanted" ] || case " $wanted " in *" $surface "*) ;; *) continue ;; esac
    for pair in $LANGS; do
        shot "$name" "$surface" "${pair%%:*}" "${pair#*:}" "$w" "$h" "$state" "$theme"
    done
done

if [ "$failed" -ne 0 ]; then
    echo "readme-shots: $failed image(s) missing" >&2
    exit 1
fi
echo "readme-shots: done"
