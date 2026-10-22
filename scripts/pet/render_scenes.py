"""Render the desktop pet's scenes from scripts/pet/scenes/<id>.json into data/scenes/.

    python -I scripts/pet/render_scenes.py

A scene file is one prop rig baked into twelve frames. Each frame has the pet's own shift and scale
(`dx`, `dy` in canvas units, `scale` about its feet) plus `back` and `front` part lists, in the same
shape the prototype's pixel-art.js uses: a `rect` or `art` drawn from a cell's top-left corner,
`step` canvas units per character (3 unless it says 1), and colours by name from the scene palette
(the pet palette plus the warm props cardboard, earth, paper and candlelight use).

Coordinates are the prototype's: cell (x, y) starts at canvas (30 + 3x, 30 + 3y), authored against
the cat's feet at (58.5, 81). The app measures each pet's own feet at load and shifts the whole stage
by the difference.

The script also writes data/scenes/scenes.json, the runtime catalogue: the sheet paths, z values and
each frame's pet transform. It checks what the prototype tests check -- every prop stays inside the
canvas (the bath's tub may run under the bottom edge), art rows are rectangular, and every colour is
in the palette -- then reads every sheet back and checks its size.
"""

import json
import pathlib
import sys

from PIL import Image, ImageDraw

sys.stdout.reconfigure(encoding="utf-8")

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = HERE / "scenes"
OUT = ROOT / "data" / "scenes"
CANVAS = 88
CELL = 3
ORIGIN = 30
# The bath's tub runs this far past the bottom edge on purpose: the pet is sunk to the chin, and the
# tub's last rows are what keep its feet from showing below the rim.
FLOOR_BLEED = 2 * CELL
PALETTE = {
    "ink": (26, 26, 31, 255),
    "fur": (251, 251, 253, 255),
    "grey": (74, 78, 86, 255),
    "rose": (236, 98, 140, 255),
    "sky": (82, 160, 236, 255),
    "green": (52, 170, 96, 255),
    "lilac": (160, 116, 232, 255),
    # Warm props: cardboard, earth, paper, candlelight.
    "amber": (216, 164, 84, 255),
    "brown": (150, 98, 54, 255),
    "cream": (247, 232, 200, 255),
    "gold": (242, 186, 64, 255),
    "pumpkin": (240, 136, 56, 255),
}


def paint(draw, part):
    """Paints one part. Unknown colour names and '.' cells are skipped, so art may use '.' as a hole."""
    left = ORIGIN + part["at"][0] * CELL
    top = ORIGIN + part["at"][1] * CELL
    if "rect" in part:
        colour = PALETTE.get(part["colour"])
        if colour:
            w, h = part["rect"]
            draw.rectangle((left, top, left + w * CELL - 1, top + h * CELL - 1), fill=colour)
        return
    step = part.get("step", CELL)
    for j, row in enumerate(part["art"]):
        for i, ch in enumerate(row):
            colour = PALETTE.get(part["colours"].get(ch))
            if colour:
                draw.rectangle((left + i * step, top + j * step, left + i * step + step - 1, top + j * step + step - 1), fill=colour)


def part_box(part):
    """The canvas units a part covers, right and bottom exclusive."""
    step = part.get("step", CELL)
    if "rect" in part:
        w, h = part["rect"][0] * CELL, part["rect"][1] * CELL
    else:
        w = max(len(row) for row in part["art"]) * step
        h = len(part["art"]) * step
    left = ORIGIN + part["at"][0] * CELL
    top = ORIGIN + part["at"][1] * CELL
    return left, top, left + w, top + h


def check_parts(scene_id, frames_data):
    """The prototype test's rules, checked on the data the app will draw."""
    for frame in frames_data:
        for part in frame["back"] + frame["front"]:
            left, top, right, bottom = part_box(part)
            if left < 0 or top < 0 or right > CANVAS or bottom > CANVAS + FLOOR_BLEED:
                raise SystemExit(f"{scene_id}: a prop is outside the canvas: {left},{top},{right},{bottom}")
            if "art" in part:
                width = len(part["art"][0])
                if any(len(row) != width for row in part["art"]):
                    raise SystemExit(f"{scene_id}: a ragged art row")
                for row in part["art"]:
                    for ch in row:
                        if ch != "." and part["colours"].get(ch) not in PALETTE:
                            raise SystemExit(f"{scene_id}: draws {ch!r} with no palette colour")
            elif part["colour"] not in PALETTE:
                raise SystemExit(f"{scene_id}: uses colour {part['colour']}")


def frame_image(parts):
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for part in parts:
        paint(draw, part)
    return img


def save_sheet(path, frames):
    sheet = Image.new("RGBA", (CANVAS * len(frames), CANVAS), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        sheet.paste(frame, (i * CANVAS, 0))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)
    with Image.open(path) as written:
        size = written.size
    if size != (CANVAS * len(frames), CANVAS):
        raise SystemExit(f"{path}: {size[0]}x{size[1]}, expected {CANVAS * len(frames)}x{CANVAS}")


def main():
    catalogue = {"canvas": CANVAS, "scenes": []}
    for file in sorted(SOURCE.glob("*.json")):
        scene = json.loads(file.read_text(encoding="utf-8"))
        scene_id = scene["id"]
        frames_data = scene["framesData"]
        if len(frames_data) != scene["frames"]:
            raise SystemExit(f"{scene_id}: {len(frames_data)} frames of data for {scene['frames']} frames")
        check_parts(scene_id, frames_data)

        save_sheet(OUT / scene_id / "back.png", [frame_image(f["back"]) for f in frames_data])
        save_sheet(OUT / scene_id / "front.png", [frame_image(f["front"]) for f in frames_data])

        catalogue["scenes"].append({
            "id": scene_id,
            "name": scene["name"],
            "frames": scene["frames"],
            "fps": scene["fps"],
            "foot": scene["foot"],
            "layers": scene["layers"],
            "sheets": {"back": f"{scene_id}/back.png", "front": f"{scene_id}/front.png"},
            "framesData": [
                # The pet is scaled about its own feet and it never turns; docs/adr/0049 says why the
                # prototype's tilt of the rig is not carried over.
                {"dx": f["dx"], "dy": f["dy"], "scale": f["scale"]}
                for f in frames_data
            ],
        })
        print(f"wrote {OUT / scene_id}")

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "scenes.json").write_text(json.dumps(catalogue, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT / 'scenes.json'}")


if __name__ == "__main__":
    main()
