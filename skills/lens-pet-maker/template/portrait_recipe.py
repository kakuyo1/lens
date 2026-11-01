# -*- coding: utf-8 -*-
"""Template: a full-parity pet built from one official portrait render.

For characters whose official art already contains the whole body on one
portrait sheet (the South Park cast's Fandom renders are like this: big head,
torso, shorts and shoes, no legs).  The render is cleaned of its backing and
becomes the body sheet, and the face layer is rebuilt from the render's own
geometry because at 88 px the official pupils and mouth vanish: the bitmap's
pupils and mouth are erased, and pupils, mouth and lids are drawn on the face
layer at the measured positions.

Usage:
    PORTRAIT_REF=<official.png> PORTRAIT_ID=butters PORTRAIT_NAME=Butters \
        python -I portrait_recipe.py <out-dir>
    PORTRAIT_FIT=0.9 (default), PORTRAIT_SCALE=2 (default),
    PORTRAIT_MOUTH=auto|none (none for a hooded face with no visible mouth)

The script prints the measured eye band, pupils and mouth; check those numbers
before trusting a run.  Only the library and the art are third-party: keep the
render with you, do not commit or redistribute it.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

SKILL_SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts")
sys.path.insert(0, os.path.normpath(SKILL_SCRIPTS))
import pet_kit

REF = os.environ.get("PORTRAIT_REF", "")
PID = os.environ.get("PORTRAIT_ID", "portrait")
PNAME = os.environ.get("PORTRAIT_NAME", "Portrait")
FIT = float(os.environ.get("PORTRAIT_FIT", "0.9"))
SCALE = int(os.environ.get("PORTRAIT_SCALE", "2"))
MOUTH_MODE = os.environ.get("PORTRAIT_MOUTH", "auto")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
CANVAS = 88
COLORS = 96
SRC = os.path.join(OUT, f"source_{PID}.png")


def prep_source(path, dst):
    """Keep the figure's own alpha, drop the soft halo, bleed edge colours into
    the transparent field (LANCZOS reads them next), binary mask so no ghost
    pixels survive the downscale."""
    im = Image.open(path).convert("RGBA")
    arr = np.array(im)
    rgb = arr[..., :3].copy()
    a = arr[..., 3].astype(np.int32)
    m = a > 60
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    m = ndimage.binary_fill_holes(lab == (np.argmax(sizes) + 1))
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
    if not REF:
        sys.exit("set PORTRAIT_REF to the official render first")
    prep_source(REF, SRC)

fitted = pet_kit.fitted_image(SRC, colors=COLORS, fit=FIT)
raw = pet_kit.fitted_image(SRC, fit=FIT)
rp, fp = raw.load(), fitted.load()


def brightness(p):
    return (p[0] + p[1] + p[2]) / 3


def eye_points(mask, min_area):
    """Keep only oval-like white regions: antialiased silhouette edges are
    thin lines, and a 1 px line on the hat otherwise widens the eye band."""
    lab, n = ndimage.label(mask, structure=np.ones((3, 3)))
    keep = np.zeros_like(mask)
    for i in range(1, n + 1):
        yy, xx = np.where(lab == i)
        if len(xx) < min_area:
            continue
        w = xx.max() - xx.min() + 1
        h = yy.max() - yy.min() + 1
        if len(xx) / (w * h) < 0.35:
            continue
        keep[yy, xx] = True
    return keep


# --- eye band ----------------------------------------------------------------
wmask = np.zeros((CANVAS, CANVAS), dtype=bool)
for y in range(CANVAS):
    for x in range(CANVAS):
        p = rp[x, y]
        if p[3] > 128 and min(p[:3]) > 225 and max(p[:3]) - min(p[:3]) < 18:
            wmask[y, x] = True
wmask = eye_points(wmask, 40)
white = [(x, y) for y, x in zip(*np.where(wmask))]
assert len(white) > 60, f"eye whites not found ({len(white)} px)"
EX0, EY0 = min(p[0] for p in white), min(p[1] for p in white)
EX1, EY1 = max(p[0] for p in white), max(p[1] for p in white)
EYW = EX1 - EX0 + 1
SEAM = (EX0 + EX1) // 2
EYC = (EY0 + EY1) // 2

# --- pupils: darkest pixel inside each white oval ----------------------------
found = []
for side, half in ((-1, (EX0, SEAM - 1)), (1, (SEAM + 1, EX1))):
    pts = [(x, y) for x, y in white if half[0] <= x <= half[1]]
    assert pts, "empty eye half"
    hx0 = min(x for x, _ in pts) + 2   # the rim holds the seam and the hood
    hx1 = max(x for x, _ in pts) - 2
    hy0 = min(y for _, y in pts) + 2
    hy1 = max(y for _, y in pts) - 2
    best = None
    for y in range(max(0, hy0), min(CANVAS, hy1 + 1)):
        for x in range(max(0, hx0), min(CANVAS, hx1 + 1)):
            if rp[x, y][3] > 128 and (best is None or brightness(rp[x, y]) < best[0]):
                best = (brightness(rp[x, y]), x, y)
    if best and best[0] < 170:   # a real pupil dot, not just shading
        found.append((best[1], best[2]))
    else:                        # fall back to the official proportion
        found.append(((EX0 + EX1) // 2 + side * round(0.133 * EYW), EYC))
PUPX = [p[0] for p in found]
PUPY = round(sum(p[1] for p in found) / len(found))

# --- mouth: dark line below the eyes, unless the band itself is dark ---------
MOUTH_W = max(5, round(0.26 * EYW))
MOUTH_CX = (EX0 + EX1) // 2
MOUTH_CY = EYC + round(0.56 * EYW)
MOUTH_ON = MOUTH_MODE != "none"

if MOUTH_ON:
    # Measure the mouth on the full-resolution render and map it through the
    # eye band: the 88 px downscale turns the thin line into scattered dark
    # specks, while the source keeps a clean, measurable stroke.
    fa = np.array(Image.open(SRC).convert("RGBA")).astype(int)
    farm, faa = fa[..., :3], fa[..., 3]
    fwhite = (faa > 128) & (farm.min(2) > 225) & (farm.max(2) - farm.min(2) < 18)
    fwhite = eye_points(fwhite, 400)
    fy, fx = np.where(fwhite)
    if len(fx) > 50:
        fx0, fx1, fy0, fy1 = fx.min(), fx.max(), fy.min(), fy.max()
        fw, fh = fx1 - fx0 + 1, fy1 - fy0 + 1
        dark = (faa > 128) & (farm.max(2) < 120)
        dark[:fy1 + 3, :] = False           # only strokes below the eyes
        lab, n = ndimage.label(dark, structure=np.ones((3, 3)))
        best = None
        for i in range(1, n + 1):
            yy, xx = np.where(lab == i)
            if len(xx) < 20:
                continue
            w = xx.max() - xx.min() + 1
            h = yy.max() - yy.min() + 1
            cx = (xx.min() + xx.max()) / 2
            # a mouth is a wide, short stroke near the face centre; buttons,
            # zip lines and the jacket outline fail one of these
            if not (0.15 * fw <= w <= 0.6 * fw and h <= 0.3 * fh):
                continue
            if not (fx0 + 0.2 * fw <= cx <= fx1 - 0.2 * fw):
                continue
            if best is None or yy.min() < best[0]:
                best = (yy.min(), xx.min(), yy.min(), xx.max(), yy.max())
        if best:
            _, bx0, by0, bx1, by1 = best
            s = EYW / fw
            MOUTH_CX = round(EX0 + ((bx0 + bx1) / 2 - fx0) * s)
            MOUTH_CY = round(EY0 + ((by0 + by1) / 2 - fy0) * s)
            MOUTH_W = int(max(5, min(round((bx1 - bx0 + 1) * s), round(0.55 * EYW))))

# lid fill colour: the brightest probe around the eye band, so a hat brim or
# a fur hood on the sides never wins over the face.  (Kenny's eye whites sit in
# a dark hood with a bright face slot; the lids must be skin, never fur.)
probes = [rp[EX0 - 3, EYC][:3], rp[EX1 + 3, EYC][:3],
          rp[(EX0 + EX1) // 2, EY0 - 3][:3], rp[(EX0 + EX1) // 2, EY1 + 3][:3]]
SKIN = max(probes, key=brightness)

# eye region for the lids: bright pixels in the band (whites + the face slot),
# holes filled so the enclosed pupils and the seam are covered too
lid_mask = np.zeros((CANVAS, CANVAS), dtype=bool)
for y in range(EY0 - 3, EY1 + 4):
    for x in range(EX0 - 3, EX1 + 4):
        if rp[x, y][3] > 128 and brightness(rp[x, y]) > 180:
            lid_mask[y, x] = True
lid_mask = ndimage.binary_fill_holes(lid_mask)
MOUTH_SKIN = max((rp[MOUTH_CX - 8, MOUTH_CY][:3], rp[MOUTH_CX + 8, MOUTH_CY][:3],
                  rp[MOUTH_CX, MOUTH_CY - 4][:3]), key=brightness)
MOUTH_COL = "#3A241C"

# erase the bitmap pupils and the faint bitmap mouth; the face layer owns both.
# The fill colour is the whites' own median: a bbox corner can be skin or a hat.
eye_white = tuple(int(np.median([rp[x, y][i] for x, y in white])) for i in range(3))
for px in PUPX:
    for y in range(PUPY - 2, PUPY + 3):
        for x in range(px - 2, px + 3):
            if (x - px) ** 2 + (y - PUPY) ** 2 <= 5:
                fp[x, y] = (*eye_white, 255)
if MOUTH_ON:
    for y in range(MOUTH_CY - 3, MOUTH_CY + 4):
        for x in range(MOUTH_CX - MOUTH_W // 2 - 2, MOUTH_CX + MOUTH_W // 2 + 3):
            if 0 <= x < CANVAS and 0 <= y < CANVAS:
                fp[x, y] = (*MOUTH_SKIN, 255)


# --- face cells --------------------------------------------------------------
def pupils(dx=0, dy=0):
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for px in PUPX:
        d.rectangle([px + dx, PUPY - 1 + dy, px + dx + 1, PUPY + dy], fill="#1A1A1A")
    return img


def mouth(kind):
    """1 px mouth on a transparent cell; the body's own line is covered."""
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    if not MOUTH_ON:
        return img
    d = ImageDraw.Draw(img)
    w = MOUTH_W
    x0 = MOUTH_CX - w // 2
    if kind == "frown":  # ends down: the official worried mouth
        for i in range(w):
            y = MOUTH_CY - 1 if 1 <= i <= w - 2 else MOUTH_CY
            d.point((x0 + i, y), fill=MOUTH_COL)
    elif kind == "sad":
        for i in range(w):
            inner = min(i, w - 1 - i)
            y = MOUTH_CY - 2 if inner >= 2 else (MOUTH_CY - 1 if inner == 1 else MOUTH_CY)
            d.point((x0 + i, y), fill=MOUTH_COL)
    elif kind == "smile":  # ends up
        for i in range(w):
            y = MOUTH_CY + 1 if 1 <= i <= w - 2 else MOUTH_CY
            d.point((x0 + i, y), fill=MOUTH_COL)
    elif kind == "line":
        d.line([(x0 + 1, MOUTH_CY), (x0 + w - 2, MOUTH_CY)], fill=MOUTH_COL)
    elif kind == "wave":
        for i in range(w):
            d.point((x0 + i, MOUTH_CY + (1 if i % 2 else 0)), fill=MOUTH_COL)
    return img


def open_mouth(half_w, half_h):
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    if not MOUTH_ON:
        return img
    ImageDraw.Draw(img).rounded_rectangle(
        [MOUTH_CX - half_w, MOUTH_CY - half_h, MOUTH_CX + half_w, MOUTH_CY + half_h],
        radius=max(1, min(half_w, half_h)), fill="#4A1F14")
    return img


def lid_cell(rows_to, crease=True, crease_off=0, happy=False):
    """Skin patch over the measured eye region. rows_to limits it from the top
    (None = full close). Only that region is painted; every other pixel stays
    transparent so no hair, hat or hood pixels ghost over the body layer.
    Near-white body pixels in the band are covered too: the eye whites' top
    edge is antialiased, and the band filter leaves that sliver outside the
    mask, where a closed eye would show it as leftover white."""
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    p = img.load()
    for y in range(EY0 - 3, EY1 + 4):
        if rows_to is not None and y > rows_to:
            continue
        for x in range(EX0 - 3, EX1 + 4):
            # the quantized body is what the reader sees: a raw pixel just under
            # the white threshold can still ship as white
            if lid_mask[y, x] or (fp[x, y][3] > 128 and min(fp[x, y][:3]) > 225):
                p[x, y] = (*SKIN, 255)
    if crease:
        d = ImageDraw.Draw(img)
        col = tuple(round(c * 0.62) for c in SKIN)
        cy = (rows_to if rows_to is not None else EY1 - 2) + crease_off
        for ex0, ex1 in ((EX0 + 2, SEAM - 2), (SEAM + 2, EX1 - 2)):
            box = [ex0, cy - 1, ex1, cy + 2]
            d.arc(box, 200 if happy else 25, 340 if happy else 155, fill=col, width=1)
    return img


def compose(*layers):
    out = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    for layer in layers:
        out.alpha_composite(layer)
    return out


BLINK_FACE = compose(lid_cell(None), mouth("frown"))
HAPPY_FACE = compose(lid_cell(None, happy=True), mouth("smile"))
SLEEPY_FACE = compose(lid_cell(None, crease_off=1), mouth("line"))
HALF_FACE = compose(pupils(), lid_cell(EYC - 1, crease=False), mouth("frown"))
ANGRY_FACE = compose(pupils(), lid_cell(EYC - 2, crease=False), mouth("sad"))
YAWN_FACE = compose(lid_cell(None), open_mouth(3, 3))
YAWN_HALF = compose(lid_cell(None), open_mouth(2, 2))
SURPRISED_FACE = compose(pupils(), open_mouth(2, 2))
DIZZY_FACE = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
d = ImageDraw.Draw(DIZZY_FACE)
for px in PUPX:
    d.line([(px - 2, PUPY - 2), (px + 2, PUPY + 2)], fill="#1A1A1A", width=1)
    d.line([(px - 2, PUPY + 2), (px + 2, PUPY - 2)], fill="#1A1A1A", width=1)
DIZZY_FACE.alpha_composite(mouth("wave"))

# angry brows only where there is forehead skin above the eyes; under a hat
# brim or a hood they would draw on the garment, so the lids carry the anger
_mid = (EX0 + EX1) // 2
_p = rp[_mid, EY0 - 3]
if _p[3] > 128 and brightness(_p) > 150:
    d = ImageDraw.Draw(ANGRY_FACE)
    d.line([(EX0 + 2, EY0 - 4), (SEAM - 3, EY0 - 2)], fill="#1A1A1A", width=1)
    d.line([(SEAM + 3, EY0 - 2), (EX1 - 2, EY0 - 4)], fill="#1A1A1A", width=1)

FACE = {
    "default": compose(pupils(), mouth("frown")),
    "blink": BLINK_FACE,
    "sleepy": SLEEPY_FACE,
    "look_up": compose(pupils(0, -1), mouth("frown")),
    "look_down": compose(pupils(0, 1), mouth("frown")),
    "look_l": compose(pupils(-1, 0), mouth("frown")),
    "look_r": compose(pupils(1, 0), mouth("frown")),
    "yawn": YAWN_FACE,
    "yawn_half": YAWN_HALF,
    "angry": ANGRY_FACE,
    "dizzy": DIZZY_FACE,
    "happy": HAPPY_FACE,
    "surprised": SURPRISED_FACE,
    "half": HALF_FACE,
}

# The book is held in the (screen-)left hand: the lower body's leftmost edge is
# the hand, and the effect layer never follows the body's anchors, so study
# frames stay still and only the face changes.
_ink = [(x, y) for y in range(CANVAS) for x in range(CANVAS) if rp[x, y][3] > 128]
_iy0, _iy1 = min(y for _, y in _ink), max(y for _, y in _ink)
_lo, _hi = _iy0 + round(0.62 * (_iy1 - _iy0)), _iy0 + round(0.88 * (_iy1 - _iy0))
_hand_edge = min(x for y in range(_lo, _hi + 1) for x in range(CANVAS) if rp[x, y][3] > 128)
_hand_cy = _iy1 - 0.24 * (_iy1 - _iy0)
BOOK_AT = (_hand_edge + 5 - 8, round(_hand_cy) - 8)


def src_action(fps, loop, frames, **extra):
    spec = {"fps": fps, "loop": loop, "source": SRC, "colors": COLORS, "fit": FIT,
            "frames": frames}
    spec.update(extra)
    return spec


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
    "expressions": ["default", "blink", "sleepy", "look_up", "look_down",
                    "look_l", "look_r", "yawn", "yawn_half", "angry", "dizzy",
                    "happy", "surprised", "half"],
    "face": FACE,
    "blink": {"fps": 8, "pattern": [1, 2, 1]},
    "actions": {
        "idle": src_action(4, True, [
            {"pose": {}}, {"pose": {"dy": -0.25}},
            {"pose": {}}, {"pose": {"dy": -0.1}}], blink=True),
        "study": src_action(3, True, [
            {"pose": {}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"}],
            blink=True, effect={"kind": "book", "at": BOOK_AT}),
        "thinking": src_action(3, False, [
            {"pose": {"dy": -0.25}, "expression": "look_up"},
            {"pose": {"dy": -0.25}, "expression": "look_up"},
            {"pose": {}, "expression": "look_l"}]),
        "celebrate": src_action(6, False, [
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.0}, "expression": "happy"},
            {"pose": {"dy": -1.4}, "expression": "happy"},
            {"pose": {"dy": -0.4}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.0}, "expression": "happy"},
            {"pose": {"dy": -1.4}, "expression": "happy"},
            {"pose": {"dy": -0.4}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.0}, "expression": "happy"},
            {"pose": {"dy": -1.4}, "expression": "happy"},
            {"pose": {"dy": -0.4}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {}, "expression": "happy"},
            {"pose": {}, "expression": "default"}]),
        "encourage": src_action(5, False, [
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "blink"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "blink"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "blink"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "blink"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "blink"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "blink"}]),
        "sleep": src_action(3, True, [
            {"pose": {}, "expression": "sleepy"},
            {"pose": {"dy": 0.3}, "expression": "sleepy"},
            {"pose": {}, "expression": "sleepy"}],
            effect={"kind": "zzz", "color": "#9AA0A6"}),
        "click_react": src_action(10, False, [
            {"pose": {"dx": -0.5}, "expression": "default"},
            {"pose": {"dy": -0.5}, "expression": "look_up"},
            {"pose": {"dx": 0.5}, "expression": "default"}]),
        "pickup": src_action(4, True, [
            {"pose": {"dx": -0.4}, "expression": "look_down"},
            {"pose": {}, "expression": "look_down"},
            {"pose": {"dx": 0.4}, "expression": "look_down"}]),
        "look_around": src_action(4, False, [
            {"pose": {"dx": -0.3}, "expression": "look_l"},
            {"pose": {"dx": -0.3}, "expression": "look_l"},
            {"pose": {}, "expression": "default"},
            {"pose": {"dx": 0.3}, "expression": "look_r"},
            {"pose": {"dx": 0.3}, "expression": "look_r"}], blink=True),
        "yawn": src_action(4, False, [
            {"pose": {}, "expression": "default"},
            {"pose": {}, "expression": "yawn_half"},
            {"pose": {}, "expression": "yawn"},
            {"pose": {}, "expression": "yawn"},
            {"pose": {}, "expression": "yawn_half"},
            {"pose": {}, "expression": "sleepy"}], returnTo="idle"),
        "stretch": src_action(4, False, [
            {"pose": {"dy": -0.7}, "expression": "look_up"},
            {"pose": {"dy": -1.1}, "expression": "look_up"},
            {"pose": {"dy": -0.3}, "expression": "sleepy"}], returnTo="idle"),
        "angry": src_action(8, False, [
            {"pose": {"dx": -0.3}, "expression": "angry"},
            {"pose": {"dx": 0.3}, "expression": "angry"},
            {"pose": {"dx": -0.3}, "expression": "angry"},
            {"pose": {}, "expression": "angry"}],
            effect={"kind": "dust", "color": "#C9B79E"}),
        "ask": src_action(3, True, [
            {"pose": {}, "expression": "look_l"},
            {"pose": {}, "expression": "default"},
            {"pose": {}, "expression": "look_r"}]),
        "dizzy": src_action(6, False, [
            {"pose": {}, "expression": "dizzy"},
            {"pose": {"dx": -0.5}, "expression": "dizzy"},
            {"pose": {}, "expression": "dizzy"},
            {"pose": {"dx": 0.5}, "expression": "dizzy"},
            {"pose": {}, "expression": "dizzy"},
            {"pose": {}, "expression": "blink"}],
            effect={"kind": "stars", "color": "#F7D117"}),
        "walk": src_action(6, True, [
            {"pose": {"dx": -0.6}, "expression": "default"},
            {"pose": {"dy": -0.6}, "expression": "default"},
            {"pose": {"dx": 0.6}, "expression": "default"},
            {"pose": {"dy": -0.6}, "expression": "default"}]),
        "peek": src_action(4, True, [
            {"pose": {"dx": -0.6}, "expression": "look_l"},
            {"pose": {"dx": -0.6}, "expression": "blink"},
            {"pose": {"dx": -0.6}, "expression": "look_l"}], blink=True),
        "wiggle": src_action(6, False, [
            {"pose": {"dx": -0.5}, "expression": "default"},
            {"pose": {"dx": 0.5}, "expression": "default"},
            {"pose": {"dx": -0.4}, "expression": "look_l"},
            {"pose": {}, "expression": "default"}]),
        "hop": src_action(5, False, [
            {"pose": {}, "expression": "happy"},
            {"pose": {"dy": -1.4}, "expression": "happy"},
            {"pose": {"dy": -0.6}, "expression": "happy"},
            {"pose": {}, "expression": "default"}]),
        "sigh": src_action(3, False, [
            {"pose": {}, "expression": "default"},
            {"pose": {"dy": 0.3}, "expression": "sleepy"},
            {"pose": {"dy": 0.5}, "expression": "sleepy"},
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
    "movement": {"style": "hop", "walk": "walk", "peek": "peek", "range": 30},
    "behavior": {"idleBaseMs": 12000, "idleSpreadMs": 40000, "napChance": 0.05,
                 "angryClicks": 4, "wanderWaitMinMs": 90000, "wanderWaitMaxMs": 200000,
                 "strollSteps": 3, "hideChance": 0.5, "gazeRadius": 30},
    "accessories": [],
}

print(f"{PID}: eyes x{EX0}-{EX1} y{EY0}-{EY1} seam {SEAM} pupils {PUPX},{PUPY} "
      f"mouth {MOUTH_ON} ({MOUTH_CX},{MOUTH_CY}) w{MOUTH_W} skin {SKIN} "
      f"fit {FIT} scale {SCALE}")
sys.exit(pet_kit.render(RECIPE, OUT))
