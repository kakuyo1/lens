"""Render one desktop pet from its recipe into data/pet/<id>/.

    python -I scripts/pet/render_pet.py mochi

Reads scripts/pet/recipes/<id>.json and writes pet.json, animations.json, anchors.json, accessories.json and every
sprite under data/pet/<id>/. The app reads that folder as it is; nothing is staged anywhere else. Each sheet is read
back and checked the way pet_assets.cpp checks it: frames x 88 wide and 88 tall, and the expression sheet is a row of
88 x 88 cells.

Recipe schema. Units are grid cells. Cell (x, y) covers canvas px (30 + 3x) to (32 + 3x), the dog's mapping. A shape
point is a cell centre, (x + 0.5, y + 0.5) for cell (x, y).

  id, scale          folder name and the whole-number scale the app shows the pet at (3).
  body               {cx, cy, rx, ry}: the base ellipse. Its bottom centre is the pivot of every pose.
  eyes, mouth        expression cells. The mouth is drawn for every expression except 7 (yawn) and 8 (blank).
  yawnMouth          [x, y, w, h] for expression 7.
  roles, idlePool    copied to animations.json.
  actions            name -> {fps, loop, blink?, returnTo?, paint?, frames}. Each item of `frames` is one frame.
                     `paint`, if set, is drawn on every frame, under that frame's own paint.
    frame.pose       {dx, dy, sx, sy}; defaults 0, 0, 1, 1. sx and sy squash the body about the pivot. dx and dy
                     move the whole pet; they become anchors.body, so the body sheet itself is drawn in place.
    frame.expression index into EYE_PATTERNS; default 0.
    frame.shapes     extra shapes, applied in order to the body: {op: add|discard, shape: ellipse|rect, ...}.
                     An ellipse takes cx, cy, rx, ry. A rect takes cols and rows, inclusive cell bounds.
    frame.paint      effect-layer items: {at: [x, y], colour, art | glyph}. In art, '#' is a cell.
  accessories        [{id, slot: head|face|body, except?: [action, ...], paint}]. Drawn once, in base coordinates.
                     Each action not in `except` goes into supportedActions.

Anchors come from the pose, one set per frame. Three points move with it: head (top of the body), face (mean of the
eyes) and body (the pivot). An anchor is the move of its point, snapped to whole cells, so accessories stay on the grid.
"""

import argparse
import json
import math
import pathlib
import sys

from PIL import Image, ImageDraw

sys.stdout.reconfigure(encoding="utf-8")

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CANVAS = 88
CELL = 3
ORIGIN = 30  # canvas px of cell (0, 0)
DEFAULT_POSE = {"dx": 0, "dy": 0, "sx": 1, "sy": 1}
NEIGHBOURS = [(1, 0), (-1, 0), (0, 1), (0, -1)]
SLOTS = {"head": ("headwear", 5), "face": ("glasses", 4), "body": ("clothing", 3)}
PALETTE = {
    "ink": (26, 26, 31, 255),
    "fur": (251, 251, 253, 255),
    "grey": (74, 78, 86, 255),
    "rose": (236, 98, 140, 255),
    "sky": (82, 160, 236, 255),
    "green": (52, 170, 96, 255),
    "lilac": (160, 116, 232, 255),
}
INK = PALETTE["ink"]
FUR = PALETTE["fur"]

# Copied from scripts/pet/dog/build-assets.py (not imported): the eye patterns, and the glyphs a
# recipe may name with "glyph". '#' is ink, or one cell for a glyph.
EYE_PATTERNS = [
    ["###", "###", "###"],  # 0 open
    ["...", "###", "###"],  # 1 half
    ["...", "###", "..."],  # 2 closed
    ["##.", "##.", "##."],  # 3 looking left
    [".##", ".##", ".##"],  # 4 looking right
    [".#.", "#.#", "..."],  # 5 happy, a ^ arc
    ["###", "###", "..."],  # 6 looking up
    ["...", "###", "..."],  # 7 yawn: eyes shut, mouth opened below
    None,  # 8 nothing: the sleeping body draws its own eyes
]
GLYPHS = {
    "question": ["###", "..#", ".##", "...", ".#."],
    "zz": ["###", ".#.", "###"],
}


def snap(value):
    """Round half up, so a move of exactly half a cell always goes the same way."""
    return math.floor(value + 0.5)


def covers(shape, x, y):
    """Whether a shape covers the point (x, y), in cell space."""
    if shape["shape"] == "ellipse":
        return ((x - shape["cx"]) / shape["rx"]) ** 2 + ((y - shape["cy"]) / shape["ry"]) ** 2 <= 1.0
    (x0, x1), (y0, y1) = shape["cols"], shape["rows"]
    return x0 <= x < x1 + 1 and y0 <= y < y1 + 1


def body_cells(body, pivot, pose, shapes):
    """The cells of the body sheet: the base ellipse squashed about the pivot, then the frame's shapes in order.

    Each cell centre is mapped back through the squash to the base shape, so a squash never has to rebuild the shape.
    """
    cells = set()
    for y in range(-4, 20):
        for x in range(-4, 22):
            px, py = x + 0.5, y + 0.5
            qx = pivot[0] + (px - pivot[0]) / pose["sx"]
            qy = pivot[1] + (py - pivot[1]) / pose["sy"]
            on = covers(body, qx, qy)
            for shape in shapes:
                if covers(shape, px, py):
                    on = shape["op"] == "add"
            if on:
                cells.add((x, y))
    return cells


def new_frame():
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def fill_cell(draw, x, y, colour):
    left, top = ORIGIN + CELL * x, ORIGIN + CELL * y
    draw.rectangle((left, top, left + CELL - 1, top + CELL - 1), fill=colour)


def paint(draw, items):
    for item in items:
        rows = GLYPHS[item["glyph"]] if "glyph" in item else item["art"]
        colour = PALETTE[item["colour"]]
        ax, ay = item["at"]
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch == "#":
                    fill_cell(draw, ax + i, ay + j, colour)


def body_frame(body, pivot, pose, shapes):
    cells = body_cells(body, pivot, pose, shapes)
    img, draw = new_frame()
    for x, y in {(x + dx, y + dy) for x, y in cells for dx, dy in NEIGHBOURS} - cells:
        fill_cell(draw, x, y, INK)
    for x, y in cells:
        fill_cell(draw, x, y, FUR)
    return img


def face_frame(index, eyes, mouth, yawn):
    img, draw = new_frame()
    pattern = EYE_PATTERNS[index]
    if pattern is None:
        return img
    if index == 7:
        x, y, w, h = yawn
        for j in range(h):
            for i in range(w):
                fill_cell(draw, x + i, y + j, INK)
    else:
        for x, y in mouth:
            fill_cell(draw, x, y, INK)
    for x, y in eyes:
        fill_cell(draw, x, y, FUR)
        left, top = ORIGIN + CELL * x, ORIGIN + CELL * y
        for j, row in enumerate(pattern):
            for i, ch in enumerate(row):
                if ch == "#":
                    draw.point((left + i, top + j), fill=INK)
    return img


def paint_frame(items):
    img, draw = new_frame()
    paint(draw, items)
    return img


def anchor(point, pivot, pose):
    """Where a body point is drawn in this frame, relative to base, in canvas px, snapped to whole cells."""
    moved_x = pivot[0] + (point[0] - pivot[0]) * pose["sx"] + pose["dx"]
    moved_y = pivot[1] + (point[1] - pivot[1]) * pose["sy"] + pose["dy"]
    return [CELL * snap(moved_x - point[0]), CELL * snap(moved_y - point[1])]


def sheet(frames):
    out = Image.new("RGBA", (CANVAS * len(frames), CANVAS), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        out.paste(frame, (i * CANVAS, 0))
    return out


def save(path, img, frames):
    """Write a sheet, read it back, and refuse it if it is not frames x 88 by 88."""
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    with Image.open(path) as written:
        size = written.size
    if size != (frames * CANVAS, CANVAS):
        raise SystemExit(f"{path}: {size[0]}x{size[1]}, expected {frames * CANVAS}x{CANVAS}")


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("id", help="pet id: reads recipes/<id>.json, writes data/pet/<id>/")
    args = parser.parse_args()

    recipe = json.loads((HERE / "recipes" / f"{args.id}.json").read_text(encoding="utf-8"))
    out = ROOT / "data" / "pet" / args.id
    body = dict(recipe["body"], op="add", shape="ellipse")
    pivot = (body["cx"], body["cy"] + body["ry"])
    head = (body["cx"], body["cy"] - body["ry"])
    eyes = [tuple(p) for p in recipe["eyes"]]
    mouth = [tuple(p) for p in recipe["mouth"]]
    face = (sum(x + 0.5 for x, _ in eyes) / len(eyes), sum(y + 0.5 for _, y in eyes) / len(eyes))

    animations = {"roles": recipe["roles"], "idlePool": recipe["idlePool"], "actions": {}}
    anchors = {"actions": {}}
    for name, spec in recipe["actions"].items():
        frames = spec["frames"]
        entry = {"frames": len(frames), "fps": spec["fps"], "loop": spec["loop"]}
        entry.update({key: spec[key] for key in ("returnTo", "blink") if key in spec})
        entry["expressions"] = [f.get("expression", 0) for f in frames]
        animations["actions"][name] = entry
        if name == "blink":
            continue

        poses = [{**DEFAULT_POSE, **f.get("pose", {})} for f in frames]
        bodies = [body_frame(body, pivot, p, f.get("shapes", [])) for p, f in zip(poses, frames)]
        body_path = f"sprites/body/{name}.png"
        save(out / body_path, sheet(bodies), len(frames))
        entry["sheet"] = body_path
        if spec.get("paint") or any("paint" in f for f in frames):
            effects = [paint_frame(spec.get("paint", []) + f.get("paint", [])) for f in frames]
            effect_path = f"sprites/effects/{name}.png"
            save(out / effect_path, sheet(effects), len(frames))
            entry["effect"] = effect_path
        anchors["actions"][name] = [
            {"head": anchor(head, pivot, p), "face": anchor(face, pivot, p), "body": anchor(pivot, pivot, p)}
            for p in poses
        ]

    expression_frames = [face_frame(i, eyes, mouth, recipe["yawnMouth"]) for i in range(len(EYE_PATTERNS))]
    save(out / "sprites/expressions/eyes.png", sheet(expression_frames), len(EYE_PATTERNS))

    # The bare cell is idle's first frame, so it carries the same ears and tail as the sheet.
    idle = recipe["actions"]["idle"]["frames"][0]
    bare = Image.alpha_composite(body_frame(body, pivot, {**DEFAULT_POSE, **idle.get("pose", {})}, idle.get("shapes", [])), expression_frames[0])
    save(out / "sprites/reference/bare.png", bare, 1)

    catalogue = []
    actions = [name for name in recipe["actions"] if name != "blink"]
    for acc in recipe["accessories"]:
        folder, z = SLOTS[acc["slot"]]
        asset = f"sprites/accessories/{folder}/{acc['id']}.png"
        save(out / asset, paint_frame(acc["paint"]), 1)
        catalogue.append({
            "id": acc["id"],
            "slot": acc["slot"],
            "asset": asset,
            "zIndex": z,
            "supportedActions": [name for name in actions if name not in acc.get("except", [])],
        })

    write_json(out / "pet.json", {
        "id": args.id,
        "canvas": CANVAS,
        "height": CANVAS,
        "scale": recipe["scale"],
        "layers": {"body": 0, "expression": 2, "effect": 6},
        "expression": "sprites/expressions/eyes.png",
    })
    write_json(out / "animations.json", animations)
    write_json(out / "anchors.json", anchors)
    write_json(out / "accessories.json", {"accessories": catalogue})
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
