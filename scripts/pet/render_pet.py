"""Render one desktop pet from its recipe into data/pet/<id>/.

    python -I scripts/pet/render_pet.py mochi

Reads scripts/pet/recipes/<id>.json and writes pet.json, animations.json, anchors.json, accessories.json and every
sprite under data/pet/<id>/. The app reads that folder as it is; nothing is staged anywhere else. Each sheet is read
back and checked the way pet_assets.cpp checks it: frames x 88 wide and 88 tall, and the expression sheet is a row of
88 x 88 cells.

Recipe schema. Units are grid cells. Cell (x, y) covers canvas px (30 + 3x) to (32 + 3x), the pet canvas mapping. A shape
point is a cell centre, (x + 0.5, y + 0.5) for cell (x, y).

  id, scale          folder name and the whole-number scale the app shows the pet at (3).
  body               {cx, cy, rx, ry}: the base ellipse. Its bottom centre is the pivot of every pose.
  pivot, head        Optional [x, y] overrides for the pose pivot and the head anchor. The defaults are the ellipse
                     bottom centre and top centre. A silhouette mask can sit far from that ellipse, so name both
                     explicitly when the art is not ellipse-shaped.
  bodyPaint          [{at, colour, art}]: cells coloured on the body sheet, only where the body covers them. The
                     recipe's list is the default; an action sets it for every frame; a frame's own list replaces it.
                     Display space, like frame.shapes, so the cells move with anchors.body.
  silhouette         Optional mask shape, applied to every frame before that frame's shapes. Its rows use '#' for a
                     body cell and any other character for empty space. Use it for a shape that a stack of ellipses
                     and rectangles cannot express (a jellyfish's tentacles, a cat's ears).
  facePaint          [{at, colour, art | glyph}]: expression-layer items drawn on every expression cell, under the eyes
                     and mouth. The layer moves with anchors.face, so use it for whiskers or a permanent marking.
  eyes, mouth        expression cells. The mouth is drawn for every expression except 7 (yawn), 8 (blank)
                     and 9 (angry).
  eyeSize            Optional 2: use the two-cell (six-pixel) SMALL_EYE_PATTERNS instead of the default
                     3x3-pixel eyes. The dog uses it; omit it for every other pet.
  yawnMouth          [x, y, w, h] for expression 7.
  angryMouth         cells for expression 9, the frown. Omit it and the angry face keeps the smile.
  roles, idlePool    copied to animations.json.
  hover              Optional: {notice, dwell, noticeDelayMs, dwellDelayMs, cooldownMs}. The two action names are copied
                     to animations.json; the app uses them for the proximity greeting and fuller reaction.
  movement           Optional: {style, walk, peek?, range?}. Copied to animations.json. Both action names must loop;
                     the pet walks while idle and hides at a screen edge only when `peek` is present.
  actions            name -> {fps, loop, blink?, returnTo?, duration?, bodyFrom?, paint?, frames}. Each item of
                     `frames` is one frame. `duration`, in milliseconds, ends a looping action when it runs out.
                     `bodyFrom` names an action already earlier in the document whose body sheet and anchors this
                     action reuses, cycled to its own frame count: the scene actions use it to stand still in the
                     pet's idle pose, and the angry action overrides a frame's anchors with its own `pose`.
                     `paint`, if set, is drawn on every frame, under that frame's own paint.
    frame.pose       {dx, dy, sx, sy}; defaults 0, 0, 1, 1. sx and sy squash the body about the pivot. dx and dy
                     move the whole pet; they become anchors.body, so the body sheet itself is drawn in place.
    frame.expression index into EYE_PATTERNS; default 0.
    frame.shapes     extra shapes, applied in order to the body: {op: add|discard, shape: ellipse|rect|mask, ...}.
                     An ellipse takes cx, cy, rx, ry. A rect takes cols and rows, inclusive cell bounds. A mask takes
                     at: [x, y] and rows, where '#' is inside.
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
    "charcoal": (47, 50, 58, 255),
    "silver": (143, 151, 166, 255),
    "mist": (211, 216, 226, 255),
    "aqua": (175, 201, 199, 255),
    "dust": (143, 166, 184, 255),
}
INK = PALETTE["ink"]
FUR = PALETTE["fur"]

# The shared eye patterns and the glyphs a recipe may name with "glyph". The retired 1.2.0 dog
# renderer used the same table; the dog now reads them here (ADR 0081).
#
# A pattern is drawn from its eye cell's top-left corner, except that EYE_OFFSETS moves it up or
# down in canvas pixels: the angry eyes carry a brow one canvas unit above the cell, so their
# pattern is four rows tall and starts one unit higher.
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
    ["#..", "###", "###", "###"],  # 9 angry: a scowl, its brow above; the right eye mirrors this
]
EYE_OFFSETS = [0, 0, 0, 0, 0, 0, 0, 0, 0, -1]
# Patterns whose second eye is the first one mirrored: the angry brow slopes inwards, and the
# eye's notch is on the inner corner. The directional looks (3, 4) are not mirrored.
EYE_MIRRORS = [False, False, False, False, False, False, False, False, False, True]
# A recipe may ask for two-cell eyes with "eyeSize": 2. The same ten expressions, drawn at half
# the width; the dog's design uses them so its face keeps the cat-like dot rather than a 3x3 block.
SMALL_EYE_PATTERNS = [
    ["######"] * 6,  # 0 open, two cells
    ["......"] * 3 + ["######"] * 3,  # 1 half
    ["......"] * 5 + ["######"],  # 2 closed
    ["####.."] * 6,  # 3 looking left
    ["..####"] * 6,  # 4 looking right
    ["......"] * 4 + ["######"] * 2,  # 5 happy, a low line
    ["######"] * 4 + ["......"] * 2,  # 6 looking up
    ["......"] * 5 + ["######"],  # 7 yawn: eyes shut, mouth opened below
    None,  # 8 nothing
    ["######"] * 4 + ["......"] * 2,  # 9 angry, a narrowed eye
]
SMALL_EYE_OFFSETS = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
SMALL_EYE_MIRRORS = [False, False, False, False, False, False, False, False, False, True]
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
    if shape["shape"] == "mask":
        ax, ay = shape["at"]
        # floor, not int: cell centres are negative on the left and above the origin, and int truncates toward zero.
        row, col = math.floor(y) - ay, math.floor(x) - ax
        rows = shape["rows"]
        return 0 <= row < len(rows) and 0 <= col < len(rows[row]) and rows[row][col] == "#"
    (x0, x1), (y0, y1) = shape["cols"], shape["rows"]
    return x0 <= x < x1 + 1 and y0 <= y < y1 + 1


def body_cells(body, pivot, pose, shapes):
    """The cells of the body sheet: the base ellipse squashed about the pivot, then the frame's shapes in order.

    Each cell centre is mapped back through the squash to the base shape, so a squash never has to rebuild the shape.
    """
    cells = set()
    # Room for art that reaches past the usual pet box, such as ears, tails and a jellyfish's tentacles. The sheet
    # itself clips whatever falls outside it.
    for y in range(-10, 30):
        for x in range(-10, 30):
            px, py = x + 0.5, y + 0.5
            qx = pivot[0] + (px - pivot[0]) / pose["sx"]
            qy = pivot[1] + (py - pivot[1]) / pose["sy"]
            on = covers(body, qx, qy)
            for shape in shapes:
                # A frame shape is display space by default. A silhouette named at the recipe level follows the
                # pose, so a squash deforms the art instead of adding an upright copy over it.
                sx_, sy_ = (qx, qy) if shape.get("followPose") else (px, py)
                if covers(shape, sx_, sy_):
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
                if ch != "#":
                    continue
                if item.get("pixel"):
                    # One canvas unit per character, as the eyes are drawn: a glyph finer than a cell.
                    draw.point((ORIGIN + CELL * ax + i, ORIGIN + CELL * ay + j), fill=colour)
                else:
                    fill_cell(draw, ax + i, ay + j, colour)


# ponytail: bodyPaint is in display space, so a squashed pose works its cells out in the recipe. Map them through the
# squash here, as body_cells does, once the recipe starts computing them by hand for many frames.
def cell_colours(items):
    """The colour of each cell a paint list names, so body_frame can tint the body cells it covers."""
    colours = {}
    for item in items:
        ax, ay = item["at"]
        for j, row in enumerate(item["art"]):
            for i, ch in enumerate(row):
                if ch == "#":
                    colours[(ax + i, ay + j)] = PALETTE[item["colour"]]
    return colours


def body_tints(spec, frame, default=()):
    """The body-layer colours of one frame: its own bodyPaint, else the action's, else the recipe's."""
    return cell_colours(frame.get("bodyPaint", spec.get("bodyPaint", default)))


def body_frame(body, pivot, pose, shapes, tints):
    cells = body_cells(body, pivot, pose, shapes)
    img, draw = new_frame()
    for x, y in {(x + dx, y + dy) for x, y in cells for dx, dy in NEIGHBOURS} - cells:
        fill_cell(draw, x, y, INK)
    for x, y in cells:
        fill_cell(draw, x, y, tints.get((x, y), FUR))
    return img


def face_frame(index, eyes, mouth, yawn, angry_mouth=(), face_paint=(),
               patterns=EYE_PATTERNS, offsets=EYE_OFFSETS, mirrors=EYE_MIRRORS):
    img, draw = new_frame()
    pattern = patterns[index]
    if pattern is None:
        paint(draw, face_paint)
        return img
    if index == 7:
        x, y, w, h = yawn
        for j in range(h):
            for i in range(w):
                fill_cell(draw, x + i, y + j, INK)
    else:
        cells = angry_mouth if index == 9 else mouth
        for x, y in cells:
            fill_cell(draw, x, y, INK)
    centre = sum(x for x, _ in eyes) / len(eyes)
    for x, y in eyes:
        fill_cell(draw, x, y, FUR)
        left, top = ORIGIN + CELL * x, ORIGIN + CELL * y + offsets[index]
        rows = [row[::-1] for row in pattern] if mirrors[index] and x > centre else pattern
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch == "#":
                    draw.point((left + i, top + j), fill=INK)
    paint(draw, face_paint)
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
    pivot = tuple(recipe.get("pivot", (body["cx"], body["cy"] + body["ry"])))
    head = tuple(recipe.get("head", (body["cx"], body["cy"] - body["ry"])))
    silhouette = [{**recipe["silhouette"], "op": "add", "followPose": True}] if "silhouette" in recipe else []
    face_paint = recipe.get("facePaint", [])
    eyes = [tuple(p) for p in recipe["eyes"]]
    mouth = [tuple(p) for p in recipe["mouth"]]
    face = (sum(x + 0.5 for x, _ in eyes) / len(eyes), sum(y + 0.5 for _, y in eyes) / len(eyes))

    animations = {"roles": recipe["roles"], "idlePool": recipe["idlePool"]}
    if "hover" in recipe:
        animations["hover"] = recipe["hover"]
    if "movement" in recipe:
        animations["movement"] = recipe["movement"]
    animations["actions"] = {}
    anchors = {"actions": {}}
    rendered = {}
    default_paint = recipe.get("bodyPaint", [])
    for name, spec in recipe["actions"].items():
        frames = spec["frames"]
        entry = {"frames": len(frames), "fps": spec["fps"], "loop": spec["loop"]}
        entry.update({key: spec[key] for key in ("returnTo", "blink", "duration") if key in spec})
        entry["expressions"] = [f.get("expression", 0) for f in frames]
        animations["actions"][name] = entry
        if name == "blink":
            continue

        poses = [{**DEFAULT_POSE, **f.get("pose", {})} for f in frames]
        body_from = spec.get("bodyFrom")
        if body_from:
            if body_from not in rendered:
                raise SystemExit(f"{name}: bodyFrom names an action that is not rendered yet: {body_from}")
            source_bodies, source_anchors = rendered[body_from]
            bodies = [source_bodies[i % len(source_bodies)] for i in range(len(frames))]
        else:
            bodies = [
                body_frame(body, pivot, p, silhouette + f.get("shapes", []), body_tints(spec, f, default_paint))
                for p, f in zip(poses, frames)
            ]
        body_path = f"sprites/body/{name}.png"
        save(out / body_path, sheet(bodies), len(frames))
        entry["sheet"] = body_path
        if spec.get("paint") or any("paint" in f for f in frames):
            effects = [paint_frame(spec.get("paint", []) + f.get("paint", [])) for f in frames]
            effect_path = f"sprites/effects/{name}.png"
            save(out / effect_path, sheet(effects), len(frames))
            entry["effect"] = effect_path
        if body_from:
            # A frame that names its own pose overrides the reused one; the angry shake is exactly that.
            per_frame = [
                {"head": anchor(head, pivot, p), "face": anchor(face, pivot, p), "body": anchor(pivot, pivot, p)}
                if "pose" in f
                else source_anchors[i % len(source_anchors)]
                for i, (p, f) in enumerate(zip(poses, frames))
            ]
        else:
            per_frame = [
                {"head": anchor(head, pivot, p), "face": anchor(face, pivot, p), "body": anchor(pivot, pivot, p)}
                for p in poses
            ]
        anchors["actions"][name] = per_frame
        rendered[name] = (bodies, per_frame)

    angry_mouth = [tuple(p) for p in recipe.get("angryMouth", [])]
    if recipe.get("eyeSize", 3) == 2:
        patterns, offsets, mirrors = SMALL_EYE_PATTERNS, SMALL_EYE_OFFSETS, SMALL_EYE_MIRRORS
    else:
        patterns, offsets, mirrors = EYE_PATTERNS, EYE_OFFSETS, EYE_MIRRORS
    expression_frames = [
        face_frame(i, eyes, mouth, recipe["yawnMouth"], angry_mouth, face_paint, patterns, offsets, mirrors)
        for i in range(len(patterns))
    ]
    save(out / "sprites/expressions/eyes.png", sheet(expression_frames), len(patterns))

    # The bare cell is idle's first frame, so it carries the same ears and tail as the sheet.
    idle_spec = recipe["actions"]["idle"]
    idle = idle_spec["frames"][0]
    bare_body = body_frame(
        body,
        pivot,
        {**DEFAULT_POSE, **idle.get("pose", {})},
        silhouette + idle.get("shapes", []),
        body_tints(idle_spec, idle, default_paint),
    )
    bare = Image.alpha_composite(bare_body, expression_frames[0])
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
        "format": 1,
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
