# -*- coding: utf-8 -*-
"""Template: a full-parity image-source pet, built from one official render.

This is the worked example the lens-pet-maker skill points to (a flat-cartoon
kid, "cartman"): an official transparent render is fitted to the 88 px canvas
and re-posed per frame, pupils are lifted onto the face layer so gaze follow
moves them, closed eyes return the eye region to skin with a soft crease, and
each role gets its own action driven by translate-only poses plus effects.

Adapting it:
  1. Point SRC at your official render (transparent PNG; set PET_REF_IMAGE),
     choose "id"/"name", and tweak the rig/expressions/actions below.
  2. Run:  PET_REF_IMAGE=<png> python -I cartman_recipe.py <out-dir>
  3. Validate + preview with scripts/validate_package.py / preview.py, then
     hand the frames to the reader (see template/README.md for the rules and
     the pitfalls this file already works around).

Third-party dependency: Pillow only. The official art itself stays with you --
do not commit or redistribute it.
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
import pet_kit

SRC = os.environ.get("PET_REF_IMAGE", "")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
FIT = 0.8  # the figure fills ~78% of the canvas, a bundled pet's proportion

if not SRC:
    sys.exit("set PET_REF_IMAGE to your official transparent render first")

fitted = pet_kit.fitted_image(SRC, colors=96, fit=FIT)
raw = pet_kit.fitted_image(SRC, fit=FIT)  # unquantised twin, same geometry
px = fitted.load()
CANVAS = pet_kit.CANVAS  # 88

# whites: measured on the crisp twin, central band keeps cap highlights out
rp = raw.load()
xs, ys = [], []
for y in range(CANVAS // 4, CANVAS * 3 // 4):
    for x in range(CANVAS // 3, CANVAS * 2 // 3):
        r, g, b, a = rp[x, y]
        if r > 235 and g > 235 and b > 235:
            xs.append(x); ys.append(y)
x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
cx = (x0 + x1) // 2

# pupils: dark pixels in the upper 70% of the whites (the mouth sits below),
# 2D connected components; tall thin comp = eye seam (stays), compact = pupil
dark = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
        if y <= y0 + round((y1 - y0) * 0.7) and max(rp[x, y][:3]) < 140]
darkset = set(dark)
comps = []
while darkset:
    seed = darkset.pop()
    comp, stack = [seed], [seed]
    while stack:
        cx0, cy0 = stack.pop()
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                pt = (cx0 + dx, cy0 + dy)
                if pt in darkset:
                    darkset.remove(pt)
                    comp.append(pt)
                    stack.append(pt)
    comps.append(comp)


def is_seam(comp):
    w = max(x for x, _ in comp) - min(x for x, _ in comp) + 1
    h = max(y for _, y in comp) - min(y for _, y in comp) + 1
    return h >= 3 * w or (h > 8 and w <= 2)


pupil_comps = sorted([c for c in comps if not is_seam(c)], key=lambda c: min(x for x, _ in c))
assert len(pupil_comps) == 2, f"expected 2 pupils, got {len(pupil_comps)} comps"


def centre(pts):
    return (round(sum(p[0] for p in pts) / len(pts)), round(sum(p[1] for p in pts) / len(pts)))


pupil_comps.sort(key=lambda c: centre(c)[0])
(lpx, lpy) = centre(pupil_comps[0])
(rpx, rpy) = centre(pupil_comps[1])
PR = 1
for cpx, cpy in ((lpx, lpy), (rpx, rpy)):  # erase from the bitmap (face layer owns them)
    for y in range(cpy - PR - 1, cpy + PR + 2):
        for x in range(cpx - PR - 1, cpx + PR + 2):
            if (x - cpx) ** 2 + (y - cpy) ** 2 <= (PR + 1) ** 2:
                px[x, y] = (*px[x, y0 + 1][:3], 255)  # column white above the pupil


def pupils_at(dx=0, dy=0):  # 2x2 beady dots; gaze-shifted variants share the shape
    return [
        {"op": "rows", "at": [lpx - 1 + dx, lpy - 1 + dy], "rows": ["##", "##"], "color": "#1A1A1A"},
        {"op": "rows", "at": [rpx - 1 + dx, rpy - 1 + dy], "rows": ["##", "##"], "color": "#1A1A1A"},
    ]


PUPILS = pupils_at()

# the bitmap mouth sits centred below the whites (from the official render);
# the yawn overlay is a small open-mouth oval right on that line
cxm = cx + 1


def lids(fill_to=None, crease=True):
    """Closed eyes, two-phase: per-row colours interpolated between the two
    cheek points (brim rows borrow the anchor row's colours, since their own
    endpoints are the cap), then painted PER COLUMN starting just below that
    column's brim. The cell carries ONLY the eye patch: everything above a
    column's paint start and below/outside the patch is transparent, so no
    cap, chin or collar pixels ghost over the body layer. crease adds one
    soft darker-skin arc per eye -- the closed-lid read."""
    img = fitted.copy()
    p = img.load()
    lx, rx = x0 - 2, x1 + 2

    def is_brim(c):
        r, g, b = c[:3]
        return (b > 140 and g > 120 and r < 120) or (r > 200 and g > 160 and b < 120)

    def skin_like(c):
        r, g, b = c[:3]
        return r > 195 and r >= g and g >= b - 10 and b > 115

    anchor = y0
    while not (skin_like(p[lx, anchor][:3]) and skin_like(p[rx, anchor][:3])):
        anchor += 1
    al, ar = p[lx, anchor][:3], p[rx, anchor][:3]
    crease_col = tuple(round(c * 0.72) for c in al)
    last = y1 + 2 if fill_to is None else fill_to
    rowcol = {}
    for y in range(y0 - 3, last + 1):
        lc, rc = p[lx, y][:3], p[rx, y][:3]
        if is_brim(lc) or is_brim(rc):
            lc, rc = al, ar
        rowcol[y] = (lc, rc)
    start = {}
    for x in range(x0 - 2, x1 + 3):
        y = y0 - 3
        while y <= last and is_brim(p[x, y][:3]):
            y += 1
        start[x] = y
        for yy in range(y, last + 1):
            lc, rc = rowcol[yy]
            t = (x - lx) / (rx - lx)
            p[x, yy] = (round(lc[0] + (rc[0] - lc[0]) * t),
                        round(lc[1] + (rc[1] - lc[1]) * t),
                        round(lc[2] + (rc[2] - lc[2]) * t), 255)
    # the cell carries only the patch: clear above each column's paint start,
    # and everything outside the column range or below the patch
    for y in range(CANVAS):
        for x in range(CANVAS):
            inside = (x0 - 2 <= x <= x1 + 2 and start.get(x, CANVAS) <= y <= last)
            if not inside:
                p[x, y] = (0, 0, 0, 0)
    if crease:
        d = ImageDraw.Draw(img)
        for ex0, ex1 in ((x0 + 1, cx - 1), (cx + 1, x1 - 1)):
            cy = y0 + round((y1 - y0) * 0.55)
            d.arc([ex0, cy - 2, ex1, cy + 3], start=25, end=155, fill=crease_col, width=1)
    return img


BLINK_FACE = lids()                                     # full close + crease
ANGRY_FACE = lids(fill_to=y0 + round((y1 - y0) * 0.45), crease=False)  # droopy glare
# the open-mouth ovals must fully cover the bitmap's own mouth line, or its
# corners peek out and read as a second mouth; measure the line on your render
cxm = cx + 1
YAWN_FACE = BLINK_FACE.copy()
ImageDraw.Draw(YAWN_FACE).rounded_rectangle([cxm - 6, y1 + 4, cxm + 5, y1 + 11],
                                            radius=3, fill="#4A1F14")
YAWN_HALF = BLINK_FACE.copy()
ImageDraw.Draw(YAWN_HALF).rounded_rectangle([cxm - 5, y1 + 5, cxm + 4, y1 + 11],
                                            radius=2, fill="#4A1F14")
DIZZY_FACE = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
d = ImageDraw.Draw(DIZZY_FACE)
for cpx, cpy in ((lpx, lpy), (rpx, rpy)):               # X eyes, nothing else
    d.line([(cpx - 1, cpy - 1), (cpx + 1, cpy + 1)], fill="#1A1A1A", width=1)
    d.line([(cpx - 1, cpy + 1), (cpx + 1, cpy - 1)], fill="#1A1A1A", width=1)

FACE = {
    "default": PUPILS,
    "blink": BLINK_FACE,
    "sleepy": BLINK_FACE,
    "look_up": pupils_at(0, -1),
    "look_down": pupils_at(0, 1),
    "look_l": pupils_at(-1, 0),
    "look_r": pupils_at(1, 0),
    "yawn": YAWN_FACE,
    "yawn_half": YAWN_HALF,
    "angry": ANGRY_FACE,
    "dizzy": DIZZY_FACE,
}

RECIPE = {
    "id": "cartman",
    "name": "Cartman",
    "canvas": 88,
    "scale": 2,
    "rig": {
        "body": {"cx": 9, "cy": 14, "rx": 6, "ry": 3.5},
        "feet": {"y": 17.4, "h": 0.5},
        "eyes": {"at": [7.5, 5], "size": 1, "style": "dot"},
        "mouth": {"at": [9, 7], "w": 3, "style": "smile"},
    },
    "expressions": ["default", "blink", "sleepy", "look_up", "look_down",
                    "look_l", "look_r", "yawn", "angry", "dizzy"],
    "face": FACE,
    "blink": {"fps": 8, "pattern": [1, 2, 1]},
    "actions": {
        "idle": {
            "fps": 4, "loop": True, "blink": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [{"pose": {}} for _ in range(4)],
        },
        "study": {
            "fps": 3, "loop": True, "blink": True,
            "source": SRC, "colors": 96, "fit": FIT,
            # the book's "at" is the hand position of YOUR character: measure it
            # from the fitted render before shipping (cartman's was (35, 62))
            "effect": {"kind": "book", "at": (35, 62)},
            "frames": [
                {"pose": {"dy": 0.3}, "expression": "look_down"},
                {"pose": {}, "expression": "look_down"},
                {"pose": {"dy": 0.3}, "expression": "look_down"},
                {"pose": {}, "expression": "look_down"},
            ],
        },
        "thinking": {
            "fps": 3, "loop": False,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dy": -0.3}, "expression": "look_up"},
                {"pose": {"dy": -0.3}, "expression": "look_up"},
                {"pose": {}, "expression": "look_l"},
            ],
        },
        "celebrate": {
            "fps": 6, "loop": False,
            "source": SRC, "colors": 96, "fit": FIT,
            "effect": {"kind": "snack", "at": (46, 2)},
            "frames": [
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": -1.2}, "expression": "default"},
                {"pose": {"dy": -1.6}, "expression": "default"},
                {"pose": {"dy": -0.6}, "expression": "default"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": -1.2}, "expression": "default"},
                {"pose": {"dy": -1.6}, "expression": "default"},
                {"pose": {"dy": -0.6}, "expression": "default"},
                {"pose": {}, "expression": "default"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": -1.2}, "expression": "default"},
                {"pose": {"dy": -1.6}, "expression": "default"},
                {"pose": {"dy": -0.6}, "expression": "default"},
                {"pose": {}, "expression": "default"},
                {"pose": {}, "expression": "default"},
                {"pose": {}, "expression": "default"},
            ],
        },
        "encourage": {
            "fps": 5, "loop": False,
            "source": SRC, "colors": 96, "fit": FIT,
            "effect": {"kind": "cash", "color": "#6FAF46"},
            "frames": [
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": 0.4}, "expression": "blink"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": 0.4}, "expression": "blink"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": 0.4}, "expression": "blink"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": 0.4}, "expression": "blink"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": 0.4}, "expression": "blink"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dy": 0.4}, "expression": "blink"},
            ],
        },
        "sleep": {
            "fps": 3, "loop": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "effect": {"kind": "zzz", "color": "#9AA0A6"},
            "frames": [{"pose": {}, "expression": "sleepy"},
                       {"pose": {"dy": 0.3}, "expression": "sleepy"},
                       {"pose": {}, "expression": "sleepy"}],
        },
        "click_react": {
            "fps": 10, "loop": False,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dx": -0.5}, "expression": "default"},
                {"pose": {"dy": -0.5}, "expression": "look_up"},
                {"pose": {"dx": 0.5}, "expression": "default"},
            ],
        },
        "pickup": {
            "fps": 4, "loop": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dx": -0.4}, "expression": "look_down"},
                {"pose": {}, "expression": "look_down"},
                {"pose": {"dx": 0.4}, "expression": "look_down"},
            ],
        },
        "look_around": {
            "fps": 4, "loop": False, "blink": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dx": -0.3}, "expression": "look_l"},
                {"pose": {"dx": -0.3}, "expression": "look_l"},
                {"pose": {}, "expression": "default"},
                {"pose": {"dx": 0.3}, "expression": "look_r"},
                {"pose": {"dx": 0.3}, "expression": "look_r"},
            ],
        },
        "yawn": {
            "fps": 4, "loop": False, "returnTo": "idle",
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {}, "expression": "default"},
                {"pose": {}, "expression": "yawn_half"},
                {"pose": {}, "expression": "yawn"},
                {"pose": {}, "expression": "yawn"},
                {"pose": {}, "expression": "yawn_half"},
                {"pose": {}, "expression": "sleepy"},
            ],
        },
        "stretch": {
            "fps": 4, "loop": False, "returnTo": "idle",
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dy": -0.8}, "expression": "look_up"},
                {"pose": {"dy": -1.2}, "expression": "look_up"},
                {"pose": {"dy": -0.4}, "expression": "sleepy"},
            ],
        },
        "angry": {
            "fps": 8, "loop": False,
            "source": SRC, "colors": 96, "fit": FIT,
            "effect": {"kind": "dust", "color": "#B0A79C"},
            "frames": [
                {"pose": {"dx": -0.3}, "expression": "angry"},
                {"pose": {"dx": 0.3}, "expression": "angry"},
                {"pose": {"dx": -0.3}, "expression": "angry"},
                {"pose": {}, "expression": "angry"},
            ],
        },
        "ask": {
            "fps": 3, "loop": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {}, "expression": "look_l"},
                {"pose": {}, "expression": "default"},
                {"pose": {}, "expression": "look_r"},
            ],
        },
        "dizzy": {
            "fps": 6, "loop": False,
            "source": SRC, "colors": 96, "fit": FIT,
            "effect": {"kind": "stars", "color": "#F7D117"},
            "frames": [
                {"pose": {}, "expression": "dizzy"},
                {"pose": {"dx": -0.5}, "expression": "dizzy"},
                {"pose": {}, "expression": "dizzy"},
                {"pose": {"dx": 0.5}, "expression": "dizzy"},
                {"pose": {}, "expression": "dizzy"},
                {"pose": {}, "expression": "blink"},
            ],
        },
        "walk": {
            "fps": 6, "loop": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dx": -0.6}, "expression": "default"},
                {"pose": {"dy": -0.6}, "expression": "default"},
                {"pose": {"dx": 0.6}, "expression": "default"},
                {"pose": {"dy": -0.6}, "expression": "default"},
            ],
        },
        "peek": {
            "fps": 4, "loop": True, "blink": True,
            "source": SRC, "colors": 96, "fit": FIT,
            "frames": [
                {"pose": {"dx": -0.6}, "expression": "look_l"},
                {"pose": {"dx": -0.6}, "expression": "blink"},
                {"pose": {"dx": -0.6}, "expression": "look_l"},
            ],
        },
    },
    "roles": {
        "idle": "idle", "study": "study", "thinking": "thinking", "celebrate": "celebrate",
        "encourage": "encourage", "sleep": "sleep", "click_react": "click_react",
        "pickup": "pickup", "look_around": "look_around", "yawn": "yawn",
        "stretch": "stretch", "angry": "angry", "ask": "ask", "dizzy": "dizzy",
    },
    "idlePool": [],
    "hover": {"notice": "look_around", "dwell": "yawn"},
    "movement": {"style": "glide", "walk": "walk", "peek": "peek", "range": 30},
    "behavior": {"idleBaseMs": 12000, "idleSpreadMs": 40000, "napChance": 0.05,
                 "angryClicks": 4, "wanderWaitMinMs": 90000, "wanderWaitMaxMs": 200000,
                 "strollSteps": 3, "hideChance": 0.5, "gazeRadius": 30},
    "accessories": [],
}

sys.exit(pet_kit.render(RECIPE, OUT))
