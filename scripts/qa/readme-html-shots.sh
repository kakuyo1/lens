#!/bin/sh
# Render the README's HTML marketing stills with headless Chrome.
#
# The current README does not drive the running application: the hero, the feature stills and the
# frames of the language-switch GIF come from ui-prototypes/ and docs/readme/scenes/. Nothing here
# opens a window on the reader's desktop, moves the pointer, or touches the clipboard.
#
# The companion script, readme-html-compose.py, crops the stills, joins the GIF frames, and builds
# the learning-loop collage and the theme contact sheet. Run this first, then the Python script.
#
# Chrome is a machine path, so it is found under the environment variables Windows sets, the same
# way readme-shots.sh does. The old readme-shots.sh writes docs/readme-images/; this script writes
# docs/readme/ and is the one the current README references.
#
# Usage: sh scripts/qa/readme-html-shots.sh

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
    echo "readme-html-shots: Chrome not found under PROGRAMFILES, PROGRAMFILES(X86) or LOCALAPPDATA" >&2
    exit 1
fi

url_root=$(cygpath -m "$root" | sed 's/ /%20/g')
out="$root/docs/readme"
frames=${LENS_README_FRAMES:-${TEMP:-/tmp}/lens-readme-gif}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$out" "$out/brand" "$frames"

# screenshot <name> <page under the repository> <query> <width> <height> <scale> <directory>
#
# Headless Chrome clamps a window to a floor near 500 x 100. Going to the surface directly at
# 380 px makes Chrome lay the page out at 500 px and then crop the screenshot to 380, which cuts
# the right edge off every card. The exact viewport therefore comes from an iframe of the wanted
# size inside a throwaway page; the screenshot then only has to take that iframe's top-left corner.
screenshot() {
    name=$1; page=$2; query=$3; width=$4; height=$5; scale=$6; dir=$7
    url="file:///$url_root/$page"
    [ -z "$query" ] || url="$url?$query"
    src=$(printf '%s' "$url" | sed 's/&/\&amp;/g')
    frame="$tmp/frame-$name.html"
    cat >"$frame" <<HTML
<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;padding:0;overflow:hidden;background:#e9ebef}
iframe{display:block;width:${width}px;height:${height}px;border:0}
</style></head><body><iframe src="$src"></iframe></body></html>
HTML
    frame_url="file:///$(cygpath -m "$frame" | sed 's/ /%20/g')"
    "$chrome" --headless=new --disable-gpu --hide-scrollbars \
        --force-device-scale-factor="$scale" \
        --virtual-time-budget=1500 \
        --window-size="$width,$height" \
        --user-data-dir="$tmp/profile-$name" \
        --screenshot="$dir/$name.png" "$frame_url" >/dev/null 2>&1
    if [ ! -s "$dir/$name.png" ]; then
        echo "readme-html-shots: $name.png was not written" >&2
        exit 1
    fi
    echo "  $name.png"
}

# The hero scene is designed at 1440 x 960 CSS pixels and rendered at 1x: the README shows it at
# about 1000 px wide, so a second copy would only cost bytes.
screenshot hero-light "docs/readme/scenes/hero.html" "" 1440 960 1 "$out"
screenshot hero-dark "docs/readme/scenes/hero.html" "theme=dark" 1440 960 1 "$out"

# The bubble stills are 2x so their text survives a retina screen at the README's display width.
for theme in light dark frosted liquid; do
    backdrop=""
    if [ "$theme" = "frosted" ]; then backdrop="&bg=sky-light"; fi
    if [ "$theme" = "liquid" ]; then backdrop="&bg=web-light"; fi
    screenshot "bubble-$theme" "ui-prototypes/bubble.html" \
        "lang=en&shot=1&theme=$theme&actions=open$backdrop" 380 440 2 "$out"
    # The theme sheet and the GIF both want the card over the same reading page, not over a
    # flat backdrop; ?page=1 draws that page inside the same document so glass can blur it.
    screenshot "bubble-page-$theme" "ui-prototypes/bubble.html" \
        "lang=en&shot=1&theme=$theme&actions=open&page=1&pageScale=1.5" 900 700 2 "$out"
done

screenshot selection-bar "ui-prototypes/selection-bar.html" \
    "lang=en&shot=1&theme=liquid&bg=white" 620 180 2 "$out"

# The review still is deliberately the hover state: it shows the sign and both answers. The
# compose step crops the empty desktop and writes review.png.
screenshot review-hover "ui-prototypes/pet-review.html" \
    "lang=en&shot=1&state=hover&pet=mochi&theme=liquid&bg=paper&scale=4" 1000 700 2 "$out"

screenshot year-in-words "ui-prototypes/year-in-words.html" \
    "lang=en&shot=1&palette=open&theme=liquid&bg=paper" 440 430 2 "$out"

# The language-switch GIF: English card, picker open, Chinese card. The compose step repeats the
# first frame to close the loop. The frames stay in the scratch directory.
screenshot fb-page-en "ui-prototypes/bubble.html" \
    "lang=en&shot=1&theme=liquid&actions=open&page=1&pageScale=1.4" 760 760 2 "$frames"
screenshot fb-page-menu "ui-prototypes/bubble.html" \
    "lang=en&shot=1&theme=liquid&actions=open&page=1&pageScale=1.4&menu=open" 760 760 2 "$frames"
screenshot fb-page-zh "ui-prototypes/bubble.html" \
    "lang=en&shot=1&theme=liquid&actions=open&page=1&pageScale=1.4&explain=zh" 760 760 2 "$frames"

# OCR flow frames: start, frame the region, read it locally, then explain it in place.
for stage in start frame read result; do
    screenshot "ocr-$stage" "docs/readme/scenes/ocr-capture.html" "stage=$stage" 900 620 2 "$frames"
done

# Five action-bar shapes over the same reading page, one frame each.
for style in classic side cards tag fan; do
    screenshot "sel-style-$style" "docs/readme/scenes/selection-styles.html" \
        "style=$style" 900 620 2 "$frames"
done

# The social preview uses a crop of the page-mode light still, so it has to come after that file
# exists. The crop starts below the article title; keeping the title would cut it at the card.
python - "$out/bubble-page-liquid.png" "$out/brand/bubble-page-social.png" <<'PY'
import sys
from PIL import Image
Image.open(sys.argv[1]).crop((600, 300, 1800, 1400)).save(sys.argv[2], optimize=True)
PY

screenshot social-preview "docs/readme/scenes/social.html" "" 1280 640 1 "$out/brand"

echo "readme-html-shots: done; frames in $frames"
echo "readme-html-shots: next, python scripts/qa/readme-html-compose.py"
