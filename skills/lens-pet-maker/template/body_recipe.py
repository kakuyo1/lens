# -*- coding: utf-8 -*-
"""Template: a full-body official render -> a full-parity image-source pet.

For characters whose official art is a *whole figure* at normal proportions
(the head is a sixth of the height, the legs are visible) rather than a
big-head portrait.  The render is fitted to a 128 px canvas and becomes the body
sheet; every frame is re-posed by translate-only anchors.

Why 128 and not 88: at 88 px a figure like this has a 2-pixel eye and no mouth
at all, so the sprite loses the face that makes the character recognisable.
128 x scale 2 = 256 px on screen, which is the bundled pets' 88 x 3 = 264, so
the pet still sits in the same size family.  The app sizes everything from
`pet.canvas` and a package may use any 16-128 (the app's own scenes and the
qtest fixture assume 88, but neither is on a package's path).

The face layer owns the pupils and the eyelids.  The mouth usually survives at
128 px, so it stays in the bitmap and only the yawn overlays it -- check your
render before switching to a redraw.

Usage:
    BODY_REF=<official.png> BODY_ID=<id> BODY_NAME=<Name> \
        python -I body_recipe.py <out-dir>

    BODY_FIT=0.96      how much of the canvas the figure fills
    BODY_BOOK=hand|front|none
                       study holds a book: in the free hand (screen-left edge),
                       hanging off the one hand that is not crossed in front,
                       or not at all
    BODY_WALK=trot|glide|hop   the movement style the pack declares
    BODY_ZZZ=1|0       the sleep effect (the kit's zzz is anchored to an 88 px
                       canvas, so this template draws its own beside the head)
    BODY_ANGRY=3       clicks before the pet snaps
    BODY_MOUTH_AT=x,y  pin the yawn's anchor when the mouth is drawn into the
                       muzzle and cannot be measured (a chin arc)
    BODY_MEASURE=1     head zoom + every face cell, no package

The script prints the measured eye band, pupils, mouth and skin; check those
numbers before trusting a run.  Only the library and the art are third-party:
keep the render with you, do not commit or redistribute it.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

SKILL_SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts")
sys.path.insert(0, os.path.normpath(SKILL_SCRIPTS))
import pet_kit

CANVAS = int(os.environ.get("BODY_CANVAS", "128"))
SCALE = int(os.environ.get("BODY_SCALE", "2"))
COLORS = 96

REF = os.environ.get("BODY_REF", "")
PID = os.environ.get("BODY_ID", "body")
PNAME = os.environ.get("BODY_NAME", "Body")
FIT = float(os.environ.get("BODY_FIT", "0.96"))
BOOK_MODE = os.environ.get("BODY_BOOK", "none")
if BOOK_MODE not in ("hand", "front", "none"):
    sys.exit("BODY_BOOK must be hand, front or none")
WALK_STYLE = os.environ.get("BODY_WALK", "glide")
if WALK_STYLE not in ("glide", "float", "hop", "trot", "pounce"):
    sys.exit("BODY_WALK must be glide, float, hop, trot or pounce")
ZZZ = os.environ.get("BODY_ZZZ", "1") != "0"
ANGRY_CLICKS = int(os.environ.get("BODY_ANGRY", "3"))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
SRC = os.path.join(OUT, f"source_{PID}.png")

if not REF:
    sys.exit("set BODY_REF to the official transparent render first")


def prep_source(path, dst):
    """Keep the figure's own alpha, drop the soft halo, bleed edge colours into
    the transparent field (LANCZOS reads them next), binary mask so no ghost
    pixels survive the downscale.

    Holes are NOT filled: the arm and coat enclose a 21k-pixel gap and
    the legs enclose a 28k one, and filling them ships those gaps as black
    patches (the RGB under a transparent pixel is black in these renders).  Only
    pinholes from antialiasing are closed."""
    im = Image.open(path).convert("RGBA")
    arr = np.array(im)
    rgb = arr[..., :3].copy()
    a = arr[..., 3].astype(np.int32)
    m = a > 60
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    m = lab == (np.argmax(sizes) + 1)
    holes = ndimage.binary_fill_holes(m) & ~m
    if holes.any():
        hlab, hn = ndimage.label(holes)
        for i in range(1, hn + 1):
            if int((hlab == i).sum()) <= 400:
                m |= hlab == i
    glyph = m.copy()
    for _ in range(40):
        if glyph.all():
            break
        known = glyph.copy()
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            srgb = np.roll(np.roll(rgb, dy, 0), dx, 1)
            skn = np.roll(np.roll(known, dy, 0), dx, 1)
            take = (~known) & skn
            rgb[take] = srgb[take]
            known[take] = True
        glyph = known
    arr[..., :3] = rgb
    arr[..., 3] = np.where(m, 255, 0).astype(np.uint8)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    Image.fromarray(arr, "RGBA").save(dst)


if not os.path.exists(SRC):
    prep_source(REF, SRC)

pet_kit.CANVAS = CANVAS
fitted = pet_kit.fitted_image(SRC, colors=COLORS, fit=FIT)
raw = pet_kit.fitted_image(SRC, fit=FIT)
rp, fp = raw.load(), fitted.load()


def brightness(p):
    return (p[0] + p[1] + p[2]) / 3


def white_components(mask, min_area):
    lab, n = ndimage.label(mask, structure=np.ones((3, 3)))
    out = []
    for i in range(1, n + 1):
        yy, xx = np.where(lab == i)
        if len(xx) < min_area:
            continue
        w = int(xx.max() - xx.min() + 1)
        h = int(yy.max() - yy.min() + 1)
        if len(xx) / (w * h) < 0.3:
            continue
        out.append({"mask": lab == i, "x0": int(xx.min()), "x1": int(xx.max()),
                    "y0": int(yy.min()), "y1": int(yy.max()),
                    "area": int(len(xx)), "w": w, "h": h})
    return out


def eye_cluster(mask, min_area, fig_box):
    """The eye whites as the topmost pair of white blobs: the lab coat and
    the shoes are white too, but they sit lower and are far wider."""
    comps = white_components(mask, min_area)
    fx0, fy0, fx1, fy1 = fig_box
    fw, fh = fx1 - fx0 + 1, fy1 - fy0 + 1
    clusters = []
    for i, a in enumerate(comps):
        if a.get("used"):
            continue
        group = [a]
        a["used"] = True
        for b in comps[i + 1:]:
            if b.get("used"):
                continue
            vov = min(a["y1"], b["y1"]) - max(a["y0"], b["y0"]) + 1
            gap = max(a["x0"], b["x0"]) - min(a["x1"], b["x1"]) - 1
            # two eyes are the same height; a hair highlight or a coat edge is
            # a tall sliver and must not swallow one eye before the pair meets
            same_height = abs(a["h"] - b["h"]) <= max(2, 0.4 * max(a["h"], b["h"]))
            if same_height and vov >= 0.5 * min(a["h"], b["h"]) and gap <= max(a["w"], b["w"]):
                group.append(b)
                b["used"] = True
        x0 = min(c["x0"] for c in group)
        x1 = max(c["x1"] for c in group)
        y0 = min(c["y0"] for c in group)
        y1 = max(c["y1"] for c in group)
        w, h = x1 - x0 + 1, y1 - y0 + 1
        if y0 > fy0 + 0.45 * fh:
            continue
        if w > 0.6 * fw or h < 2:
            continue
        if not (1.0 <= w / h <= 3.2):
            continue
        clusters.append((y0, -sum(c["area"] for c in group),
                         {"x0": x0, "x1": x1, "y0": y0, "y1": y1, "w": w, "h": h,
                          "mask": [c["mask"] for c in group]}))
    assert clusters, "eye whites not found"
    clusters.sort(key=lambda c: (c[0], c[1]))
    chosen = clusters[0][2]
    m = np.zeros_like(mask)
    for part in chosen.pop("mask"):
        m |= part
    chosen["mask"] = m
    return chosen


# --- eye band ---------------------------------------------------------------
_ink = [(x, y) for y in range(CANVAS) for x in range(CANVAS) if rp[x, y][3] > 128]
FIG = (min(x for x, _ in _ink), min(y for _, y in _ink),
       max(x for x, _ in _ink), max(y for _, y in _ink))
wmask = np.zeros((CANVAS, CANVAS), dtype=bool)
for y in range(CANVAS):
    for x in range(CANVAS):
        p = rp[x, y]
        if p[3] > 128 and min(p[:3]) > 225 and max(p[:3]) - min(p[:3]) < 18:
            wmask[y, x] = True
eye = eye_cluster(wmask, 12, FIG)
EX0, EY0, EX1, EY1 = eye["x0"], eye["y0"], eye["x1"], eye["y1"]
EYW, EYH = eye["w"], eye["h"]
SEAM = (EX0 + EX1) // 2
EYC = (EY0 + EY1) // 2
eye_fill = ndimage.binary_fill_holes(eye["mask"])

# --- pupils, measured on the full-resolution render --------------------------
# At canvas size one figure's pupils are two smeared pixels and the other's are gone, so
# both are located in the source (crisp), then mapped through the eye band.
fa = np.array(Image.open(SRC).convert("RGBA")).astype(int)
farm, faa = fa[..., :3], fa[..., 3]
fy_, fx_ = np.where(faa > 128)
ffig = (int(fx_.min()), int(fy_.min()), int(fx_.max()), int(fy_.max()))
fwhite = (faa > 128) & (farm.min(2) > 225) & (farm.max(2) - farm.min(2) < 18)
feye = eye_cluster(fwhite, 300, ffig)
fx0, fx1, fy0, fy1 = feye["x0"], feye["x1"], feye["y0"], feye["y1"]
fw, fh = feye["w"], feye["h"]
fseam = (fx0 + fx1) // 2
s = EYW / fw

# --- pupils: one per eye, centred ------------------------------------------
# The renders have a sideways glance baked in (the pupils sit at the far
# edge of both eyes).  At sprite size that reads as a squint, and the app moves
# the pupils with the gaze anyway, so each eye gets a centred dot and the
# look_* cells carry the offset.
eye_white = tuple(int(np.median([rp[x, y][i] for y in range(CANVAS)
                                 for x in range(CANVAS) if eye["mask"][y, x]]))
                  for i in range(3))
_ew = eye["mask"]
_left = np.where(_ew[:, EX0:SEAM].any(axis=0))[0] + EX0
_right = np.where(_ew[:, SEAM:EX1 + 1].any(axis=0))[0] + SEAM
EYE_L = (int(_left.min()), int(_left.max()))
EYE_R = (int(_right.min()), int(_right.max()))
PUPX = [int(round((EYE_L[0] + EYE_L[1]) / 2)), int(round((EYE_R[0] + EYE_R[1]) / 2))]
PUPY = EYC
# the official pupil is ~15% of the eye's width; at 5-8 px per eye that is one
# pixel, and a 2x2 block reads as a square bead instead of a dot
PUP = 1

# --- mouth, measured on the source, redrawn on the face layer ---------------
MOUTH_W = max(5, round(0.5 * EYW))
MOUTH_CX = (EX0 + EX1) // 2
MOUTH_CY = EYC + round(0.8 * EYH) + 2
mouth_src = "proportional"
band0 = fy1 + max(2, round(0.15 * fh))
band1 = min(farm.shape[0] - 1, fy1 + round(1.7 * fh))
xlo = max(0, fx0 - round(0.25 * fw))
xhi = min(farm.shape[1] - 1, fx1 + round(0.25 * fw))
dark = (faa > 128) & (farm.max(2) < 120)
dark[:band0, :] = False
dark[band1 + 1:, :] = False
dark[:, :xlo] = False
dark[:, xhi + 1:] = False
lab, n = ndimage.label(dark, structure=np.ones((3, 3)))
best = None
for i in range(1, n + 1):
    yy, xx = np.where(lab == i)
    if len(xx) < 20:
        continue
    w = xx.max() - xx.min() + 1
    h = yy.max() - yy.min() + 1
    cx = (xx.min() + xx.max()) / 2
    # The mouth is joined to the jaw wrinkles, so the component may be as
    # tall as the eye band; the mouth line itself is read from its top rows
    if not (0.25 * fw <= w <= 1.1 * fw and h <= 1.0 * fh):
        continue
    if not (fx0 + 0.12 * fw <= cx <= fx1 - 0.12 * fw):
        continue
    # the biggest candidate, not the topmost: the cheek wrinkles are separate
    # strokes above the mouth and would win a topmost race
    if best is None or len(xx) > best[0]:
        best = (len(xx), xx.min(), yy.min(), xx.max(), yy.max(), i)
mouth_box = None
if best:
    _, bx0, by0, bx1, by1, bidx = best
    comp = lab == bidx
    top = max(3, round(0.3 * (by1 - by0 + 1)))
    sub = comp.copy()
    sub[by0 + top + 1:, :] = False
    ys2, xs2 = np.where(sub)
    mx, my, mw = (xs2.min() + xs2.max()) / 2, ys2.mean(), xs2.max() - xs2.min() + 1
    MOUTH_CX = round(EX0 + (mx - fx0) * s)
    MOUTH_CY = round(EY0 + (my - fy0) * s)
    MOUTH_W = int(max(5, min(round(mw * s) + 1, round(0.85 * EYW))))
    mouth_src = "component"
    mouth_box = (round(EX0 + (xs2.min() - fx0) * s) - 2, round(EY0 + (ys2.min() - fy0) * s) - 2,
                 round(EX0 + (xs2.max() - fx0) * s) + 2, round(EY0 + (ys2.max() - fy0) * s) + 2)

# A mouth that is part of the muzzle (a chin arc) has no separate
# component to measure, and the proportional fallback lands the yawn on the
# wrong row: BODY_MOUTH_AT=<x>,<y> pins it by hand after one look at the sprite.
if os.environ.get("BODY_MOUTH_AT"):
    MOUTH_CX, MOUTH_CY = (int(v) for v in os.environ["BODY_MOUTH_AT"].split(","))
    mouth_src = "override"

# --- skin colour: the cheek, never the hair ---------------------------------
# Pale-blue hair is brighter than grey-tan skin, so "brightest probe
# wins" paints the lids blue.  The probes below sit on cheek only: outside the
# eye ends and under them, away from the nose and the mouth.
probe_pts = [(EX0 - 2, EYC), (EX1 + 2, EYC), (EX0 - 1, EY1 + 2),
             (EX1 + 1, EY1 + 2), (EX0 + 1, EY1 + 3), (EX1 - 1, EY1 + 3)]
# sampled on the QUANTISED body, not the raw one: the patch has to match the
# pixels the reader sees, and a raw-vs-palette difference of six units shows up
# as a lighter rectangle over the closed eyes
probes = [fp[x, y][:3] for x, y in probe_pts
          if 0 <= x < CANVAS and 0 <= y < CANVAS and fp[x, y][3] > 128]
SKIN = tuple(int(np.median([c[i] for c in probes])) for i in range(3))
SKIN_LINE = tuple(max(0, round(c * 0.55)) for c in SKIN)
INK = "#2A2420"

# erase whatever pupil the render baked in (one survives the downscale,
# the other does not): every dark pixel inside the filled whites goes white, so
# the face layer owns the only pupil and the gaze can move it
for y in range(max(0, EY0 - 1), min(CANVAS, EY1 + 2)):
    for x in range(max(0, EX0 - 1), min(CANVAS, EX1 + 2)):
        if eye_fill[y, x] and fp[x, y][3] > 128 and max(fp[x, y][:3]) < 150:
            fp[x, y] = (*eye_white, 255)

# The official mouth stays in the body bitmap: at 128 px the downturned
# line and the worried curve both survive the downscale and read better
# than any 1 px redraw of them (measured: a redraw lands as a dashed bar).  So
# only the yawn overlays a mouth, and that oval must cover the official line
# completely or its corners peek out and read as a second mouth.


# --- face cells --------------------------------------------------------------
def cell():
    return Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))


def pupils(dx=0, dy=0):
    img = cell()
    d = ImageDraw.Draw(img)
    for px in PUPX:
        d.rectangle([px + dx, PUPY - PUP + 1 + dy, px + dx + PUP - 1, PUPY + dy], fill=INK)
    return img


def lids(rows_to=None, crease=True, happy=False):
    """Close the eyes: the whites AND their own outline go to skin (a blank
    outlined oval reads as an empty eye, not a shut one), then one lid line
    across each eye.  The patch is the white mask dilated by two, so it stops
    at the cheek instead of eating the brow or the head's silhouette."""
    img = cell()
    p = img.load()
    pad = 2
    patch = ndimage.binary_dilation(eye_fill, iterations=pad)
    for y in range(max(0, EY0 - pad - 1), min(CANVAS, EY1 + pad + 2)):
        if rows_to is not None and y > rows_to:
            continue
        for x in range(max(0, EX0 - pad - 1), min(CANVAS, EX1 + pad + 2)):
            if patch[y, x] and fp[x, y][3] > 128:
                p[x, y] = (*SKIN, 255)
    if crease:
        d = ImageDraw.Draw(img)
        cy = EYC if rows_to is None else rows_to
        for ex0, ex1 in (EYE_L, EYE_R):
            d.arc([ex0, cy - 2, ex1, cy + 2], 200, 340, fill=(*SKIN_LINE, 255), width=1)
    return img


def open_mouth(rx, ry):
    img = cell()
    ImageDraw.Draw(img).rounded_rectangle(
        [MOUTH_CX - rx, MOUTH_CY - ry, MOUTH_CX + rx, MOUTH_CY + ry],
        radius=max(1, min(rx, ry)), fill="#3A1C14")
    return img


def compose(*layers):
    out = cell()
    for layer in layers:
        out.alpha_composite(layer)
    return out


# the open mouth must cover the official line, but it is a mouth, not a hole:
# the measured width includes the corner wrinkles, so it is capped
YAWN_RX = min(4, max(3, MOUTH_W // 2))
BLINK_FACE = lids()
SLEEPY_FACE = lids(crease=False)
HAPPY_FACE = lids(happy=True)
ANGRY_FACE = compose(pupils(), lids(EYC - 1, crease=False))
HALF_FACE = compose(pupils(), lids(EYC, crease=False))
YAWN_FACE = compose(lids(), open_mouth(max(3, YAWN_RX), 3))
YAWN_HALF = compose(lids(), open_mouth(max(2, YAWN_RX - 1), 2))
SURPRISED_FACE = compose(pupils(), open_mouth(max(2, YAWN_RX - 1), 2))
DIZZY_FACE = cell()
_d = ImageDraw.Draw(DIZZY_FACE)
_xr = 1 if EYW <= 12 else 2
for px in PUPX:
    _d.line([(px - _xr, PUPY - _xr), (px + _xr, PUPY + _xr)], fill=INK, width=1)
    _d.line([(px - _xr, PUPY + _xr), (px + _xr, PUPY - _xr)], fill=INK, width=1)

FACE = {
    "default": pupils(),
    "blink": BLINK_FACE,
    "sleepy": SLEEPY_FACE,
    "happy": HAPPY_FACE,
    "look_up": pupils(0, -1),
    "look_down": pupils(0, 1),
    "look_l": pupils(-1, 0),
    "look_r": pupils(1, 0),
    "yawn": YAWN_FACE,
    "yawn_half": YAWN_HALF,
    "angry": ANGRY_FACE,
    "dizzy": DIZZY_FACE,
    "surprised": SURPRISED_FACE,
    "half": HALF_FACE,
}

# --- the book ---------------------------------------------------------------
# The effect layer never follows the body anchors, so the study frames keep
# their pose and only the face moves.
BOOK_AT = None
if BOOK_MODE == "hand":
    _iy0, _iy1 = FIG[1], FIG[3]
    _lo, _hi = _iy0 + round(0.55 * (_iy1 - _iy0)), _iy0 + round(0.85 * (_iy1 - _iy0))
    _hand_edge = min(x for y in range(_lo, _hi + 1) for x in range(CANVAS)
                     if rp[x, y][3] > 128)
    BOOK_AT = (_hand_edge + 4 - 9, round(_iy1 - 0.3 * (_iy1 - _iy0)) - 9)
elif BOOK_MODE == "front":
    # the arms are crossed over the waist, so the only free hand is the one
    # hanging at his side: take the LOWEST skin cluster of the waist band (the
    # crossed forearms sit above it) and hang the book off its fingers, the same
    # way the free hand carries it
    _iy0, _iy1 = FIG[1], FIG[3]
    _band = range(round(_iy0 + 0.40 * (_iy1 - _iy0)), round(_iy0 + 0.66 * (_iy1 - _iy0)))
    _skin = [(x, y) for y in _band for x in range(CANVAS)
             if rp[x, y][3] > 128 and rp[x, y][0] > 200 and rp[x, y][2] > 110]
    if _skin:
        _ys = [p[1] for p in _skin]
        _hand = [p for p in _skin if p[1] >= max(_ys) - 3]
        BOOK_AT = (min(p[0] for p in _hand) - 2, max(p[1] for p in _hand) - 3)


def src_action(fps, loop, frames, **extra):
    spec = {"fps": fps, "loop": loop, "source": SRC, "colors": COLORS, "fit": FIT,
            "frames": frames}
    spec.update(extra)
    return spec


# --- effects ----------------------------------------------------------------
# The kit's own effects are laid out for an 88 px canvas: at 128 the zzz lands
# beside the neck, the stars clip at the top edge and the dust sits at knee
# height.  Only zzz is needed here, so it is re-anchored to the head's own right
# edge and every other kind still goes to the kit.
_KIT_EFFECT = pet_kit.draw_effect
Z_GLYPH = ["####.", "...#.", "..#..", ".#...", "####."]


def draw_effect(draw, eff, frame, frames):
    if eff.get("kind") != "zzz":
        return _KIT_EFFECT(draw, eff, frame, frames)
    color = eff.get("color", "#9AA0A6")
    band = range(max(0, EY0 - 8), min(CANVAS, EY1 + 12))
    bx = max(x for y in band for x in range(CANVAS) if rp[x, y][3] > 128) + 2
    t = frame / max(1, frames)
    for k in range(3):
        pet_kit._rows(draw, Z_GLYPH, (bx + k * 8, int(EY0 + 3 - k * 8 - t * 6)),
                      color, 1)


pet_kit.draw_effect = draw_effect


RECIPE = {
    "id": PID,
    "name": PNAME,
    "canvas": CANVAS,
    "scale": SCALE,
    "rig": {
        "body": {"cx": 9, "cy": 14, "rx": 6, "ry": 3.5},
        "feet": {"y": 17.4, "h": 0.5},
        "eyes": {"at": [7.5, 5], "size": 1, "style": "dot"},
        "mouth": {"at": [9, 7], "w": 3, "style": "smile"},
    },
    "expressions": list(FACE),
    "face": FACE,
    "blink": {"fps": 8, "pattern": [1, 2, 1]},
    "actions": {
        "idle": src_action(4, True, [
            {"pose": {}}, {"pose": {"dy": -0.4}},
            {"pose": {}}, {"pose": {"dy": -0.15}}], blink=True),
        "study": src_action(3, True, [
            {"pose": {}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"}],
            blink=True,
            **({"effect": {"kind": "book", "at": BOOK_AT}} if BOOK_AT else {})),
        "thinking": src_action(3, False, [
            {"pose": {"dy": -0.4}, "expression": "look_up"},
            {"pose": {"dy": -0.4}, "expression": "look_up"},
            {"pose": {}, "expression": "look_l"}]),
        "celebrate": src_action(6, False, [
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.2}, "expression": "happy"},
            {"pose": {"dy": -1.7}, "expression": "happy"},
            {"pose": {"dy": -0.5}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.2}, "expression": "happy"},
            {"pose": {"dy": -1.7}, "expression": "happy"},
            {"pose": {"dy": -0.5}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.2}, "expression": "happy"},
            {"pose": {"dy": -1.7}, "expression": "happy"},
            {"pose": {"dy": -0.5}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {}, "expression": "default"},
            {"pose": {}, "expression": "default"}]),
        "encourage": src_action(5, False, [
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
            {"pose": {"dy": 0.4}, "expression": "blink"}]),
        "sleep": src_action(3, True, [
            {"pose": {}, "expression": "sleepy"},
            {"pose": {"dy": 0.4}, "expression": "sleepy"},
            {"pose": {}, "expression": "sleepy"}],
            **({"effect": {"kind": "zzz", "color": "#8E9AA6"}} if ZZZ else {})),
        "click_react": src_action(10, False, [
            {"pose": {"dx": -0.6}, "expression": "default"},
            {"pose": {"dy": -0.7}, "expression": "look_up"},
            {"pose": {"dx": 0.6}, "expression": "default"}]),
        "pickup": src_action(4, True, [
            {"pose": {"dx": -0.5}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"},
            {"pose": {"dx": 0.5}, "expression": "look_down"}]),
        "look_around": src_action(4, False, [
            {"pose": {"dx": -0.4}, "expression": "look_l"},
            {"pose": {"dx": -0.4}, "expression": "look_l"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dx": 0.4}, "expression": "look_r"},
            {"pose": {"dx": 0.4}, "expression": "look_r"}], blink=True),
        "yawn": src_action(4, False, [
            {"pose": {}, "expression": "default"},
            {"pose": {}, "expression": "yawn_half"},
            {"pose": {}, "expression": "yawn"},
            {"pose": {}, "expression": "yawn"},
            {"pose": {}, "expression": "yawn_half"},
            {"pose": {}, "expression": "sleepy"}], returnTo="idle"),
        "stretch": src_action(4, False, [
            {"pose": {"dy": -0.8}, "expression": "look_up"},
            {"pose": {"dy": -1.3}, "expression": "look_up"},
            {"pose": {"dy": -0.4}, "expression": "sleepy"}], returnTo="idle"),
        "angry": src_action(8, False, [
            {"pose": {"dx": -0.4}, "expression": "angry"},
            {"pose": {"dx": 0.4}, "expression": "angry"},
            {"pose": {"dx": -0.4}, "expression": "angry"},
            {"pose": {}, "expression": "angry"}]),
        "ask": src_action(3, True, [
            {"pose": {}, "expression": "look_l"},
            {"pose": {}, "expression": "default"},
            {"pose": {}, "expression": "look_r"}]),
        "dizzy": src_action(6, False, [
            {"pose": {}, "expression": "dizzy"},
            {"pose": {"dx": -0.6}, "expression": "dizzy"},
            {"pose": {}, "expression": "dizzy"},
            {"pose": {"dx": 0.6}, "expression": "dizzy"},
            {"pose": {}, "expression": "dizzy"},
            {"pose": {}, "expression": "blink"}]),
        "walk": src_action(6, True, [
            {"pose": {"dx": -0.7}, "expression": "default"},
            {"pose": {"dy": -0.7}, "expression": "default"},
            {"pose": {"dx": 0.7}, "expression": "default"},
            {"pose": {"dy": -0.7}, "expression": "default"}]),
        "peek": src_action(4, True, [
            {"pose": {"dx": -0.8}, "expression": "look_l"},
            {"pose": {"dx": -0.8}, "expression": "blink"},
            {"pose": {"dx": -0.8}, "expression": "look_l"}], blink=True),
        "wiggle": src_action(6, False, [
            {"pose": {"dx": -0.6}, "expression": "default"},
            {"pose": {"dx": 0.6}, "expression": "default"},
            {"pose": {"dx": -0.4}, "expression": "look_l"},
            {"pose": {}, "expression": "default"}]),
        "hop": src_action(5, False, [
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.6}, "expression": "happy"},
            {"pose": {"dy": -0.7}, "expression": "happy"},
            {"pose": {}, "expression": "default"}]),
        "sigh": src_action(3, False, [
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.4}, "expression": "sleepy"},
            {"pose": {"dy": 0.6}, "expression": "sleepy"},
            {"pose": {"dy": 0.2}, "expression": "default"}]),
    },
    "roles": {
        "idle": "idle", "study": "study", "thinking": "thinking", "celebrate": "celebrate",
        "encourage": "encourage", "sleep": "sleep", "click_react": "click_react",
        "pickup": "pickup", "look_around": "look_around", "yawn": "yawn",
        "stretch": "stretch", "angry": "angry", "ask": "ask", "dizzy": "dizzy",
    },
    "idlePool": ["wiggle", "hop", "sigh"],
    "hover": {"notice": "look_around", "dwell": "yawn"},
    "movement": {"style": WALK_STYLE, "walk": "walk", "peek": "peek", "range": 30},
    "behavior": {"idleBaseMs": 12000, "idleSpreadMs": 40000, "napChance": 0.05,
                 "angryClicks": ANGRY_CLICKS,
                 "wanderWaitMinMs": 90000, "wanderWaitMaxMs": 200000,
                 "strollSteps": 3, "hideChance": 0.5, "gazeRadius": 40},
    "accessories": [],
}

print(f"{PID}: fig {FIG} eyes x{EX0}-{EX1} y{EY0}-{EY1} ({EYW}x{EYH}) seam {SEAM} "
      f"pupils {PUPX},{PUPY} {PUP}x{PUP} "
      f"mouth {mouth_src} ({MOUTH_CX},{MOUTH_CY}) w{MOUTH_W} box {mouth_box} "
      f"skin {SKIN} book {BOOK_AT} fit {FIT} canvas {CANVAS}")

if os.environ.get("BODY_MEASURE"):
    hx0, hy0 = max(0, EX0 - 6), max(0, EY0 - 8)
    hx1, hy1 = min(CANVAS, EX1 + 8), min(CANVAS, MOUTH_CY + 10)
    crop = Image.new("RGBA", (hx1 - hx0, hy1 - hy0), (235, 235, 235, 255))
    crop.alpha_composite(fitted.crop((hx0, hy0, hx1, hy1)))
    crop.alpha_composite(FACE["default"].crop((hx0, hy0, hx1, hy1)))
    Z = 12
    head = crop.convert("RGB").resize((crop.width * Z, crop.height * Z), Image.NEAREST)

    names = list(FACE)
    cz = 5
    tiles = []
    for n in names:
        t = Image.new("RGBA", (CANVAS, CANVAS), (255, 255, 255, 255))
        t.alpha_composite(fitted)
        t.alpha_composite(FACE[n])
        tiles.append((n, t.crop((hx0, hy0, hx1, hy1)).convert("RGB")
                      .resize(((hx1 - hx0) * cz, (hy1 - hy0) * cz), Image.NEAREST)))
    per_row = 7
    rows = (len(tiles) + per_row - 1) // per_row
    tw, th = tiles[0][1].width, tiles[0][1].height
    sheet = Image.new("RGB", (per_row * (tw + 6) + 6, rows * (th + 16) + 6), (255, 255, 255))
    d = ImageDraw.Draw(sheet)
    for i, (n, t) in enumerate(tiles):
        x = 6 + (i % per_row) * (tw + 6)
        y = 6 + (i // per_row) * (th + 16)
        sheet.paste(t, (x, y + 12))
        d.text((x, y), n, fill=(0, 0, 0))
    out = Image.new("RGB", (max(head.width, sheet.width), head.height + 8 + sheet.height),
                    (255, 255, 255))
    out.paste(head, (0, 0))
    out.paste(sheet, (0, head.height + 8))
    out.save(f"measure_{PID}.png")
    print("wrote", f"measure_{PID}.png")
    sys.exit(0)

sys.exit(pet_kit.render(RECIPE, OUT))
