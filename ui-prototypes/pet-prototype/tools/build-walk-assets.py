"""Build the walk-cycle sprites and the walk demo's action data.

Demo scaffolding only: this writes the new walk body/effect sheets into data/pet/<id>/sprites, and the action
specs (with anchors) into ui-prototypes/pet-prototype/walk/walk.json. The app's animations.json, anchors.json and
recipes are intentionally untouched until the demo has been reviewed.

The rendered pets reuse render_pet.py's painters, so a walk frame is a recipe-like frame: pose, expression,
frame shapes and effect paint. The dog keeps its old grid pipeline and gets a small custom renderer here.

    python -I ui-prototypes/pet-prototype/tools/build-walk-assets.py
"""

import importlib.util
import json
import math
import pathlib
import sys

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[3]
PROTO = ROOT / "ui-prototypes" / "pet-prototype"
PETS = ROOT / "data" / "pet"
PREVIEW = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("/tmp/walk-preview")

RENDERER_PATH = ROOT / "scripts" / "pet" / "render_pet.py"
spec = importlib.util.spec_from_file_location("render_pet", RENDERER_PATH)
render = importlib.util.module_from_spec(spec)
spec.loader.exec_module(render)

CELL = render.CELL
CANVAS = render.CANVAS
DEFAULT_POSE = render.DEFAULT_POSE
INK = render.INK
FUR = render.FUR
PALETTE = render.PALETTE


def frame(**kwargs):
    return kwargs


def paint(at, colour, art):
    return {"at": at, "colour": colour, "art": art}


OWL_BASE_SHAPES = [
    {"op": "add", "shape": "rect", "cols": [4, 4], "rows": [0, 2]},
    {"op": "add", "shape": "rect", "cols": [12, 12], "rows": [0, 2]},
    {"op": "add", "shape": "rect", "cols": [2, 14], "rows": [8, 10]},
]
OWL_WINGS = [
    {"op": "add", "shape": "ellipse", "cx": 2.5, "cy": 6.5, "rx": 2.0, "ry": 2.5},
    {"op": "add", "shape": "ellipse", "cx": 14.5, "cy": 6.5, "rx": 2.0, "ry": 2.5},
]


def contact_marks(left=True):
    """Two small rose marks under a squashed landing, one pair per side."""
    x = 5 if left else 11
    return [paint((x, 16), "rose", ["#", "#"]), paint((x + 6, 16), "rose", ["#", "#"])]


# The walk cycles. Poses are recipe cells: dx/dy are whole cells, sx/sy are around the pivot.
# Effect paint is in cells too. Each cycle loops back to its first frame.
WALKS = {
    "ghost": {
        "style": "float",
        "range": 40,
        "fps": 6,
        "frames": [
            frame(expression=0),
            frame(
                pose={"dx": -1, "dy": -1, "sx": 1.02, "sy": 0.98},
                expression=0,
                paint=[paint((0, 16), "silver", ["####"]), paint((15, 13), "dust", ["##"])],
            ),
            frame(
                pose={"dx": -2, "dy": -2, "sx": 1.03, "sy": 0.97},
                expression=5,
                paint=[paint((1, 17), "silver", ["#####"]), paint((16, 12), "dust", ["#"])],
            ),
            frame(pose={"dx": 0, "dy": -1, "sx": 1.01, "sy": 0.99}, expression=0),
            frame(
                pose={"dx": 2, "dy": -1, "sx": 1.02, "sy": 0.98},
                expression=0,
                paint=[paint((15, 16), "silver", ["####"]), paint((0, 13), "dust", ["##"])],
            ),
            frame(
                pose={"dx": 1, "dy": 0, "sx": 1.01, "sy": 0.99},
                expression=5,
                paint=[paint((14, 17), "silver", ["#####"]), paint((0, 12), "dust", ["#"])],
            ),
        ],
    },
    "mochi": {
        "style": "hop",
        "range": 30,
        "fps": 9,
        "frames": [
            frame(pose={"sx": 1.10, "sy": 0.86}, expression=0, paint=contact_marks(True)),
            frame(pose={"sx": 0.96, "sy": 1.06, "dy": -1}, expression=0),
            frame(pose={"sx": 0.94, "sy": 1.08, "dy": -2}, expression=5),
            frame(pose={"sx": 0.98, "sy": 1.02, "dy": -1, "dx": 1}, expression=5),
            frame(pose={"sx": 1.10, "sy": 0.86, "dx": 1}, expression=0, paint=contact_marks(False)),
            frame(pose={"sx": 0.98, "sy": 1.02}, expression=0),
        ],
    },
    "owl": {
        "style": "hop",
        "range": 24,
        "fps": 8,
        "frames": [
            frame(
                pose={"sx": 1.08, "sy": 0.90},
                expression=0,
                shapes=OWL_BASE_SHAPES,
                paint=[paint((5, 17), "ink", ["##"]), paint((10, 17), "ink", ["##"])],
            ),
            frame(
                pose={"sx": 0.98, "sy": 1.04, "dy": -1},
                expression=5,
                shapes=OWL_BASE_SHAPES,
            ),
            frame(
                pose={"sx": 0.94, "sy": 1.06, "dy": -3},
                expression=5,
                shapes=OWL_BASE_SHAPES + OWL_WINGS,
                paint=[paint((5, 18), "grey", ["#######"])],
            ),
            frame(
                pose={"sx": 0.94, "sy": 1.06, "dy": -4},
                expression=5,
                shapes=OWL_BASE_SHAPES + OWL_WINGS,
                paint=[paint((5, 18), "grey", ["#######"])],
            ),
            frame(
                pose={"sx": 0.98, "sy": 1.04, "dy": -1},
                expression=0,
                shapes=OWL_BASE_SHAPES,
            ),
            frame(
                pose={"sx": 1.08, "sy": 0.90},
                expression=0,
                shapes=OWL_BASE_SHAPES,
                paint=[paint((5, 17), "ink", ["##"]), paint((10, 17), "ink", ["##"])],
            ),
        ],
    },
    "cat": {
        "style": "pounce",
        "range": 36,
        "fps": 7,
        "frames": [
            frame(expression=3, paint=[paint((4, 17), "dust", ["##"]), paint((11, 17), "dust", ["##"])]),
            frame(pose={"dx": -1, "dy": -1, "sx": 1.02, "sy": 0.98}, expression=3),
            frame(pose={"dx": -2, "sx": 1.00, "sy": 1.00}, expression=4, paint=[paint((11, 17), "dust", ["###"])]),
            frame(pose={"dy": -1, "sx": 1.02, "sy": 0.98}, expression=4),
            frame(pose={"dx": 2, "sx": 1.00, "sy": 1.00}, expression=3, paint=[paint((4, 17), "dust", ["###"])]),
            frame(pose={"dx": 1, "dy": -1, "sx": 1.01, "sy": 0.99}, expression=3),
        ],
    },
    "cloud": {
        "style": "float",
        "range": 46,
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(pose={"dx": -2, "sx": 1.02, "sy": 0.98}, expression=0, paint=[paint((0, 12), "dust", ["#"])]),
            frame(
                pose={"dx": -3, "dy": -1, "sx": 1.03, "sy": 0.97},
                expression=3,
                paint=[paint((17, 12), "dust", ["#"]), paint((0, 10), "dust", ["##"])],
            ),
            frame(expression=0),
            frame(
                pose={"dx": 3, "dy": -1, "sx": 1.03, "sy": 0.97},
                expression=4,
                paint=[paint((0, 12), "dust", ["#"]), paint((17, 10), "dust", ["##"])],
            ),
            frame(pose={"dx": 2, "sx": 1.02, "sy": 0.98}, expression=0, paint=[paint((17, 12), "dust", ["#"])]),
        ],
    },
    "jelly": {
        "style": "float",
        "range": 30,
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(pose={"dy": -1, "sx": 0.98, "sy": 1.03}, expression=0, paint=[paint((16, 5), "aqua", ["#"])]),
            frame(
                pose={"dy": -2, "sx": 0.96, "sy": 1.06},
                expression=5,
                paint=[paint((15, 4), "aqua", ["#"]), paint((16, 7), "aqua", ["#"])],
            ),
            frame(pose={"dy": -1, "sx": 0.98, "sy": 1.03}, expression=5),
            frame(pose={"sx": 1.03, "sy": 0.97}, expression=0, paint=[paint((3, 10), "aqua", ["#"])]),
            frame(expression=0),
        ],
    },
}



# The edge-peek loops. These play while the pet is hidden at a screen edge; the window itself slides in and out
# a little in the demo, so the action only has to carry the duck-and-look. Expressions are neutral here; the demo
# overrides the gaze toward the screen centre, because one action cannot look both ways.
PEEKS = {
    "ghost": {
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(
                pose={"dy": 1, "sx": 1.01, "sy": 0.98},
                expression=2,
                paint=[paint((16, 16), "silver", ["##"])],
            ),
            frame(
                pose={"dy": -1, "sx": 0.99, "sy": 1.02},
                expression=0,
                paint=[paint((16, 17), "silver", ["###"]), paint((0, 17), "dust", ["##"])],
            ),
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.01, "sy": 0.98}, expression=2),
            frame(
                pose={"dy": -1, "sx": 0.99, "sy": 1.02},
                expression=0,
                paint=[paint((0, 17), "silver", ["###"]), paint((16, 17), "dust", ["##"])],
            ),
        ],
    },
    "mochi": {
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.04, "sy": 0.94}, expression=2),
            frame(pose={"dy": -1, "sx": 0.98, "sy": 1.04}, expression=0),
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.04, "sy": 0.94}, expression=2),
            frame(pose={"dy": -1, "sx": 0.98, "sy": 1.04}, expression=0),
        ],
    },
    "owl": {
        "fps": 5,
        "frames": [
            frame(expression=0, shapes=OWL_BASE_SHAPES),
            frame(pose={"dy": 1, "sx": 1.04, "sy": 0.94}, expression=2, shapes=OWL_BASE_SHAPES),
            frame(pose={"dy": -1, "sx": 0.96, "sy": 1.04}, expression=0, shapes=OWL_BASE_SHAPES + OWL_WINGS),
            frame(expression=0, shapes=OWL_BASE_SHAPES),
            frame(pose={"dy": 1, "sx": 1.04, "sy": 0.94}, expression=2, shapes=OWL_BASE_SHAPES),
            frame(pose={"dy": -1, "sx": 0.96, "sy": 1.04}, expression=0, shapes=OWL_BASE_SHAPES + OWL_WINGS),
        ],
    },
    "cat": {
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.02, "sy": 0.98}, expression=2),
            frame(pose={"dy": -1, "sx": 0.98, "sy": 1.02}, expression=0),
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.02, "sy": 0.98}, expression=2),
            frame(pose={"dy": -1, "sx": 0.98, "sy": 1.02}, expression=0),
        ],
    },
    "cloud": {
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 0.98, "sy": 0.98}, expression=2, paint=[paint((17, 12), "dust", ["##"])]),
            frame(
                pose={"dy": -1, "sx": 1.02, "sy": 1.02},
                expression=0,
                paint=[paint((0, 12), "dust", ["##"]), paint((17, 10), "dust", ["#"])],
            ),
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 0.98, "sy": 0.98}, expression=2),
            frame(
                pose={"dy": -1, "sx": 1.02, "sy": 1.02},
                expression=0,
                paint=[paint((0, 12), "dust", ["##"]), paint((17, 10), "dust", ["#"])],
            ),
        ],
    },
    "jelly": {
        "fps": 5,
        "frames": [
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.02, "sy": 0.96}, expression=2, paint=[paint((16, 5), "aqua", ["#"])]),
            frame(
                pose={"dy": -1, "sx": 0.98, "sy": 1.04},
                expression=0,
                paint=[paint((15, 4), "aqua", ["#"]), paint((16, 7), "aqua", ["#"])],
            ),
            frame(expression=0),
            frame(pose={"dy": 1, "sx": 1.02, "sy": 0.96}, expression=2),
            frame(
                pose={"dy": -1, "sx": 0.98, "sy": 1.04},
                expression=0,
                paint=[paint((3, 10), "aqua", ["#"]), paint((0, 7), "aqua", ["#"])],
            ),
        ],
    },
}


def build_rendered_action(pet_id, name, spec):
    """Build one rendered-pet action and return the action spec for the demo."""
    recipe = json.loads((ROOT / "scripts" / "pet" / "recipes" / f"{pet_id}.json").read_text(encoding="utf-8"))
    body = dict(recipe["body"], op="add", shape="ellipse")
    pivot = tuple(recipe.get("pivot", (body["cx"], body["cy"] + body["ry"])))
    head = tuple(recipe.get("head", (body["cx"], body["cy"] - body["ry"])))
    silhouette = [{**recipe["silhouette"], "op": "add", "followPose": True}] if "silhouette" in recipe else []
    eyes = [tuple(p) for p in recipe["eyes"]]
    face = (sum(x + 0.5 for x, _ in eyes) / len(eyes), sum(y + 0.5 for _, y in eyes) / len(eyes))
    default_paint = recipe.get("bodyPaint", [])

    bodies, effects, anchors, expressions = [], [], [], []
    has_effect = any("paint" in f for f in spec["frames"])
    for item in spec["frames"]:
        pose = {**DEFAULT_POSE, **item.get("pose", {})}
        bodies.append(render.body_frame(body, pivot, pose, silhouette + item.get("shapes", []), render.body_tints({}, item, default_paint)))
        effects.append(render.paint_frame(item.get("paint", [])))
        anchors.append({
            "head": render.anchor(head, pivot, pose),
            "face": render.anchor(face, pivot, pose),
            "body": render.anchor(pivot, pivot, pose),
        })
        expressions.append(item.get("expression", 0))

    out = PETS / pet_id / "sprites"
    render.save(out / "body" / f"{name}.png", render.sheet(bodies), len(bodies))
    if has_effect:
        render.save(out / "effects" / f"{name}.png", render.sheet(effects), len(effects))

    action = {
        "frames": len(bodies),
        "fps": spec["fps"],
        "loop": True,
        "expressions": expressions,
        "sheet": f"sprites/body/{name}.png",
        "anchors": anchors,
    }
    if has_effect:
        action["effect"] = f"sprites/effects/{name}.png"

    preview = Image.new("RGBA", (CANVAS * len(bodies), CANVAS), (245, 240, 230, 255))
    for i, (body_img, effect_img) in enumerate(zip(bodies, effects)):
        layer = Image.alpha_composite(body_img, effect_img)
        preview.alpha_composite(layer, (i * CANVAS, 0))
    preview = preview.resize((preview.width * 3, preview.height * 3), Image.NEAREST)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    preview.convert("RGB").save(PREVIEW / f"{pet_id}-{name}.png")
    animation = [
        Image.alpha_composite(body_img, effect_img)
        .resize((CANVAS * 3, CANVAS * 3), Image.NEAREST)
        .convert("RGB")
        for body_img, effect_img in zip(bodies, effects)
    ]
    animation[0].save(PREVIEW / f"{pet_id}-{name}.gif", save_all=True, append_images=animation[1:], duration=int(1000 / spec["fps"]), loop=0)
    return action


def build_rendered(pet_id):
    walk = WALKS[pet_id]
    return {
        "gait": walk["style"],
        "range": walk["range"],
        "action": build_rendered_action(pet_id, "walk", walk),
    }


def build_peek(pet_id):
    return build_rendered_action(pet_id, "edge_peek", PEEKS[pet_id])


# Dog: the old grid pipeline. The legs are rows 15-16 at x 4-6 and 11-13. A lifted leg loses its bottom
# row; the body bob lives in the anchors so the dust effect stays on the ground.
DOG_OFFSET_X = 24
DOG_OFFSET = 24
DOG_W = 88
DOG_H = 88
DOG_VIEW = {"x": 6, "y": 6, "scale": 3}


def dog_cell_box(x, y, w=1, h=1):
    s = DOG_VIEW["scale"]
    left = DOG_VIEW["x"] + x * s
    top = DOG_VIEW["y"] + y * s
    return left, top, left + w * s - 1, top + h * s - 1


def dog_grid_cells(rows):
    return {(x, y) for y, row in enumerate(rows) for x, ch in enumerate(row) if ch == "#"}


class DogFrame:
    def __init__(self):
        self.img = Image.new("RGBA", (DOG_W, DOG_H), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.img)

    def box(self, x0, y0, x1, y1, colour):
        self.draw.rectangle((x0 + DOG_OFFSET_X, y0 + DOG_OFFSET, x1 + DOG_OFFSET_X, y1 + DOG_OFFSET), fill=colour)

    def cell(self, x, y, w, h, colour):
        self.box(*dog_cell_box(x, y, w, h), colour)


def build_dog():
    sprite = json.loads((ROOT / "scripts" / "pet" / "dog" / "sprite.json").read_text(encoding="utf-8"))
    head = dog_grid_cells(sprite["head"])
    body = dog_grid_cells(sprite["body"])
    eyes = {tuple(p) for p in sprite["eyes"]}
    mouth = {tuple(p) for p in sprite["mouth"]}
    base = head | body | eyes | mouth
    left_leg = {(x, y) for x in range(4, 7) for y in (15, 16)}
    right_leg = {(x, y) for x in range(11, 14) for y in (15, 16)}

    # contact-left, passing, contact-right, passing, contact-left, passing.
    # dx/dy are canvas pixels for the anchors; the body art itself only changes its legs.
    cycle = [
        {"lift": "right", "dx": -2, "dy": 0, "expression": 0, "dust": "left"},
        {"lift": None, "dx": 0, "dy": -3, "expression": 5},
        {"lift": "left", "dx": 2, "dy": 0, "expression": 0, "dust": "right"},
        {"lift": None, "dx": 0, "dy": -3, "expression": 5},
        {"lift": "right", "dx": 2, "dy": 0, "expression": 0, "dust": "left"},
        {"lift": None, "dx": 0, "dy": -3, "expression": 5},
    ]

    bodies, effects, anchors, expressions = [], [], [], []
    for item in cycle:
        cells = set(base)
        cells -= left_leg | right_leg
        cells |= left_leg if item["lift"] != "left" else {(x, y) for x, y in left_leg if y == 15}
        cells |= right_leg if item["lift"] != "right" else {(x, y) for x, y in right_leg if y == 15}

        frame_img = DogFrame()
        outline = {(x + dx, y + dy) for x, y in cells for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))} - cells
        for x, y in outline:
            frame_img.cell(x, y, 1, 1, INK)
        for x, y in cells:
            frame_img.cell(x, y, 1, 1, FUR)

        effect = DogFrame()
        if item.get("dust") == "left":
            for x, y in ((8, 56), (11, 54), (14, 57)):
                effect.box(x, y, x + 2, y + 1, PALETTE["grey"])
        elif item.get("dust") == "right":
            for x, y in ((50, 56), (53, 54), (56, 57)):
                effect.box(x, y, x + 2, y + 1, PALETTE["grey"])

        bodies.append(frame_img.img)
        effects.append(effect.img)
        anchors.append({
            "head": [item["dx"], item["dy"]],
            "face": [item["dx"], item["dy"]],
            "body": [item["dx"], item["dy"]],
        })
        expressions.append(item["expression"])

    out = PETS / "dog" / "sprites"
    render.save(out / "body" / "walk.png", render.sheet(bodies), len(bodies))
    render.save(out / "effects" / "walk.png", render.sheet(effects), len(effects))

    action = {
        "frames": len(bodies),
        "fps": 8,
        "loop": True,
        "expressions": expressions,
        "sheet": "sprites/body/walk.png",
        "effect": "sprites/effects/walk.png",
        "anchors": anchors,
    }

    preview = Image.new("RGBA", (CANVAS * len(bodies), CANVAS), (245, 240, 230, 255))
    for i, (body_img, effect_img) in enumerate(zip(bodies, effects)):
        preview.alpha_composite(Image.alpha_composite(body_img, effect_img), (i * CANVAS, 0))
    preview = preview.resize((preview.width * 3, preview.height * 3), Image.NEAREST)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    preview.convert("RGB").save(PREVIEW / "dog-walk.png")
    animation = [
        Image.alpha_composite(body_img, effect_img).resize((CANVAS * 3, CANVAS * 3), Image.NEAREST).convert("RGB")
        for body_img, effect_img in zip(bodies, effects)
    ]
    animation[0].save(PREVIEW / "dog-walk.gif", save_all=True, append_images=animation[1:], duration=125, loop=0)

    return {"gait": "trot", "range": 32, "action": action}


def build_dog_peek():
    """The dog's edge peek: two duck frames lower the whole head by one cell, the rest peek up or rest."""
    sprite = json.loads((ROOT / "scripts" / "pet" / "dog" / "sprite.json").read_text(encoding="utf-8"))
    head = dog_grid_cells(sprite["head"])
    body = dog_grid_cells(sprite["body"])
    eyes = {tuple(p) for p in sprite["eyes"]}
    mouth = {tuple(p) for p in sprite["mouth"]}

    def ducked(cells):
        return {(x, y + 1) for x, y in cells}

    cycle = [
        {"duck": False, "dy": 0, "expression": 0},
        {"duck": True, "dy": 0, "expression": 2},
        {"duck": False, "dy": -2, "expression": 0},
        {"duck": False, "dy": 0, "expression": 0},
        {"duck": True, "dy": 0, "expression": 2},
        {"duck": False, "dy": -2, "expression": 0},
    ]

    bodies, anchors, expressions = [], [], []
    for item in cycle:
        if item["duck"]:
            cells = body | ducked(head) | ducked(eyes) | ducked(mouth)
            head_dy, body_dy = 3, 0
        else:
            cells = head | body | eyes | mouth
            head_dy, body_dy = item["dy"], item["dy"]

        frame_img = DogFrame()
        outline = {(x + dx, y + dy) for x, y in cells for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))} - cells
        for x, y in outline:
            frame_img.cell(x, y, 1, 1, INK)
        for x, y in cells:
            frame_img.cell(x, y, 1, 1, FUR)

        bodies.append(frame_img.img)
        anchors.append({
            "head": [0, head_dy],
            "face": [0, head_dy],
            "body": [0, body_dy],
        })
        expressions.append(item["expression"])

    out = PETS / "dog" / "sprites"
    render.save(out / "body" / "edge_peek.png", render.sheet(bodies), len(bodies))

    action = {
        "frames": len(bodies),
        "fps": 5,
        "loop": True,
        "expressions": expressions,
        "sheet": "sprites/body/edge_peek.png",
        "anchors": anchors,
    }

    preview = Image.new("RGBA", (CANVAS * len(bodies), CANVAS), (245, 240, 230, 255))
    for i, body_img in enumerate(bodies):
        preview.alpha_composite(body_img, (i * CANVAS, 0))
    preview = preview.resize((preview.width * 3, preview.height * 3), Image.NEAREST)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    preview.convert("RGB").save(PREVIEW / "dog-edge_peek.png")
    animation = [body_img.resize((CANVAS * 3, CANVAS * 3), Image.NEAREST).convert("RGB") for body_img in bodies]
    animation[0].save(PREVIEW / "dog-edge_peek.gif", save_all=True, append_images=animation[1:], duration=200, loop=0)
    return action


def main():
    result = {}
    for pet_id in WALKS:
        result[pet_id] = {**build_rendered(pet_id), "peek": build_peek(pet_id)}
        print("walk + peek:", pet_id)
    result["dog"] = {**build_dog(), "peek": build_dog_peek()}
    print("walk + peek: dog")

    output = {"pets": result}
    path = PROTO / "walk" / "walk.json"
    path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("wrote", path)
    print("previews:", PREVIEW)


if __name__ == "__main__":
    main()
