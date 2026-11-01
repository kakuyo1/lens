# -*- coding: utf-8 -*-
"""Template: a full-parity pet built from one official portrait render.

For characters whose official art already contains the whole body on one
portrait sheet (many flat-cartoon renders are like this: big head,
torso, shorts and shoes, no legs).  The render is cleaned of its backing and
becomes the body sheet, and the face layer is rebuilt from the render's own
geometry because at 88 px the official pupils and mouth vanish: the bitmap's
pupils and mouth are erased, and pupils, mouth and lids are drawn on the face
layer at the measured positions.

Usage:
    PORTRAIT_REF=<official.png> PORTRAIT_ID=sample PORTRAIT_NAME=Sample \
        python -I portrait_recipe.py <out-dir>
    PORTRAIT_FIT=0.9 (default), PORTRAIT_SCALE=2 (default),
    PORTRAIT_MOUTH=auto|none (none when the mouth is already in the body art,
        e.g. a chin arc, or hidden by a hood; the face then only
        moves eyes/lids),
    PORTRAIT_MOUTH_STYLE=frown|smile|line (default frown; use smile for art that smirks up),
    PORTRAIT_BOOK=auto|none (none when no hand is free to hold it),
    PORTRAIT_BROWS=auto|none (none for a character drawn without eyebrows),
    PORTRAIT_SEAM=auto|draw|keep (auto redraws the inner eye seam when the
        eye band is 14 px or narrower; draw/keep force or suppress it),
    PORTRAIT_SHADOW=keep|drop (drop a gray ground shadow, e.g. promo art)
    PORTRAIT_LID_PAD=0|1 (1 for faces drawn with dark eye outlines -- the
        closed eye covers the outline and gets a lid line instead of a blank
        oval; 0 for outline-free eyes, where that outline is the hood or cheek)

The eye whites are picked as the topmost pair of white blobs, so a white shirt
below the face never wins; the pupils are the dark pixels inside the filled
whites; the mouth is a separate stroke when the art has one, otherwise the
longest horizontal dark run below the eyes.

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
MOUTH_STYLE = os.environ.get("PORTRAIT_MOUTH_STYLE", "frown")
if MOUTH_STYLE not in ("frown", "smile", "line"):
    sys.exit("PORTRAIT_MOUTH_STYLE must be frown, smile or line")
MOUTH_DRAW = os.environ.get("PORTRAIT_MOUTH_DRAW", "primitive")
if MOUTH_DRAW not in ("primitive", "native"):
    sys.exit("PORTRAIT_MOUTH_DRAW must be primitive or native")
BOOK_MODE = os.environ.get("PORTRAIT_BOOK", "auto")
BROWS_MODE = os.environ.get("PORTRAIT_BROWS", "auto")
SEAM_MODE = os.environ.get("PORTRAIT_SEAM", "auto")
SHADOW_MODE = os.environ.get("PORTRAIT_SHADOW", "keep")
if SHADOW_MODE not in ("keep", "drop"):
    sys.exit("PORTRAIT_SHADOW must be keep or drop")
# 1 for a face drawn with dark eye outlines: the closed eye then
# covers the outline and carries a lid line instead of a blank oval.  0 keeps
# the outline-free look that flat-colour faces need.
LID_PAD = int(os.environ.get("PORTRAIT_LID_PAD", "0"))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
CANVAS = 88
COLORS = 96
SRC = os.path.join(OUT, f"source_{PID}_{SHADOW_MODE}.png")


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
    m = lab == (np.argmax(sizes) + 1)
    # Only pinholes are closed.  A gap the drawing encloses (an arm against
    # the coat, the legs) is transparent in the render, and the RGB under a
    # transparent pixel is black -- filling it ships a black patch on the sprite.
    holes = ndimage.binary_fill_holes(m) & ~m
    if holes.any():
        hlab, hn = ndimage.label(holes)
        for i in range(1, hn + 1):
            if int((hlab == i).sum()) <= 400:
                m |= hlab == i
    if SHADOW_MODE == "drop":
        # Promo art often carries a gray ground shadow.  It sits at the bottom
        # and is the only low-chroma midtone there; removing it by colour
        # keeps black outlines and coloured shoes.  Opt-in because a gray prop
        # (a prop such as skateboard wheels) would be hit too.
        yy, xx = np.where(m)
        y0, y1 = int(yy.min()), int(yy.max())
        x0, x1 = int(xx.min()), int(xx.max())
        cut = y1 - round(0.18 * (y1 - y0))
        chroma = rgb.max(2) - rgb.min(2)
        bright = rgb.mean(2)
        gray = (chroma < 28) & (bright > 70) & (bright < 245)
        zone = np.zeros_like(m)
        zone[cut:y1 + 1, x0:x1 + 1] = True
        m &= ~(gray & zone)
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


def white_components(mask, min_area):
    """White blobs that could be an eye; specks and thin antialiased edges drop
    out, and the geometry the eye-band choice needs is attached."""
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
    """The eye whites, as the topmost pair of white blobs.  Two eyes either
    touch (one blob) or sit side by side (merged here).  A white shirt is one
    huge blob lower down and must never win; it fails the width limit anyway."""
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
            # two eyes are the same height; a tall sliver of highlight next to
            # one eye must not absorb it before the pair can meet
            same_height = abs(a["h"] - b["h"]) <= max(2, 0.4 * max(a["h"], b["h"]))
            if same_height and vov >= 0.5 * min(a["h"], b["h"]) and gap <= max(a["w"], b["w"]):
                group.append(b)
                b["used"] = True
        x0 = min(c["x0"] for c in group)
        x1 = max(c["x1"] for c in group)
        y0 = min(c["y0"] for c in group)
        y1 = max(c["y1"] for c in group)
        w, h = x1 - x0 + 1, y1 - y0 + 1
        if y0 > fy0 + 0.45 * fh:        # eyes sit in the upper part of the figure
            continue
        if w > 0.6 * fw or h < 2:      # a shirt is wider than this
            continue
        if not (1.0 <= w / h <= 2.8):  # an eye pair is wider than it is tall
            continue
        clusters.append((y0, -sum(c["area"] for c in group),
                         {"x0": x0, "x1": x1, "y0": y0, "y1": y1,
                          "w": w, "h": h,
                          "mask": [c["mask"] for c in group]}))
    assert clusters, "eye whites not found"
    clusters.sort(key=lambda c: (c[0], c[1]))
    chosen = clusters[0][2]
    m = np.zeros_like(mask)
    for part in chosen.pop("mask"):
        m |= part
    chosen["mask"] = m
    return chosen


# --- eye band ----------------------------------------------------------------
_ink = [(x, y) for y in range(CANVAS) for x in range(CANVAS) if rp[x, y][3] > 128]
FIG = (min(x for x, _ in _ink), min(y for _, y in _ink),
       max(x for x, _ in _ink), max(y for _, y in _ink))
wmask = np.zeros((CANVAS, CANVAS), dtype=bool)
for y in range(CANVAS):
    for x in range(CANVAS):
        p = rp[x, y]
        if p[3] > 128 and min(p[:3]) > 225 and max(p[:3]) - min(p[:3]) < 18:
            wmask[y, x] = True
eye = eye_cluster(wmask, 8, FIG)
white = [(x, y) for y, x in zip(*np.where(eye["mask"]))]
assert len(white) >= 8, f"eye whites not found ({len(white)} px)"
EX0, EY0, EX1, EY1 = eye["x0"], eye["y0"], eye["x1"], eye["y1"]
EYW = eye["w"]
SEAM = (EX0 + EX1) // 2
EYC = (EY0 + EY1) // 2

# --- pupils: dark pixels inside the filled eye whites ------------------------
eye_fill = ndimage.binary_fill_holes(eye["mask"])
dark_eye = [(x, y) for y, x in zip(*np.where(eye_fill))
            if brightness(rp[x, y]) < 110]
found = []
for side, half in ((-1, (EX0, SEAM)), (1, (SEAM + 1, EX1))):
    pts = [(x, y) for x, y in dark_eye if half[0] <= x <= half[1]]
    if pts:
        found.append((round(sum(x for x, _ in pts) / len(pts)),
                      round(sum(y for _, y in pts) / len(pts))))
    else:                        # fall back to the official proportion
        found.append(((EX0 + EX1) // 2 + side * max(1, round(0.133 * EYW)), EYC))
PUPX = [p[0] for p in found]
PUPY = round(sum(p[1] for p in found) / len(found))
if EYW <= 16:
    # At this size a source gaze offset reads as cross-eyed; the animated
    # gaze comes from the face anchor anyway, so centre each pupil instead.
    PUPX = [(EX0 + SEAM - 1) // 2, (SEAM + 1 + EX1) // 2]
    PUPY = EYC
# A 2x2 pupil fills half of a 4-5 px eye; only a very small eye gets one dot.
PUP_W = 1 if EYW <= 12 else 2
PUP_H = 1 if EYW <= 12 else 2

# --- mouth: a separate stroke, or the longest horizontal run below the eyes --
MOUTH_W = max(5, round(0.26 * EYW))
MOUTH_CX = (EX0 + EX1) // 2
MOUTH_CY = EYC + round(0.56 * EYW)
MOUTH_ON = MOUTH_MODE != "none"
mouth_box = None
mouth_src = "none"
OFFICIAL_PTS = []
OFFICIAL_COL = None
OFFICIAL_BOX = None

if MOUTH_ON:
    mouth_src = "default"
    # Measure the mouth on the full-resolution render and map it through the
    # eye band: the 88 px downscale turns the thin line into scattered dark
    # specks, while the source keeps a clean, measurable stroke.
    fa = np.array(Image.open(SRC).convert("RGBA")).astype(int)
    farm, faa = fa[..., :3], fa[..., 3]
    fy, fx = np.where(faa > 128)
    ffig = (int(fx.min()), int(fy.min()), int(fx.max()), int(fy.max()))
    fwhite = (faa > 128) & (farm.min(2) > 225) & (farm.max(2) - farm.min(2) < 18)
    feye = eye_cluster(fwhite, 300, ffig)
    fx0, fx1, fy0, fy1 = feye["x0"], feye["x1"], feye["y0"], feye["y1"]
    fw, fh = feye["w"], feye["h"]
    # Skip the sliver right under the eye band: it is the eyelid outline, not
    # the mouth.  A mouth joined to the muzzle is found by the run scan below.
    band0 = fy1 + max(2, round(0.2 * fh))
    band1 = min(farm.shape[0] - 1, fy1 + round(1.6 * fh))
    xlo = max(0, fx0 - round(0.2 * fw))
    xhi = min(farm.shape[1] - 1, fx1 + round(0.2 * fw))
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
        # a mouth is a wide, short stroke near the face centre; buttons,
        # zip lines and the jacket outline fail one of these
        if not (0.2 * fw <= w <= 0.9 * fw and h <= 0.6 * fh):
            continue
        if not (fx0 + 0.15 * fw <= cx <= fx1 - 0.15 * fw):
            continue
        if best is None or yy.min() < best[0]:
            best = (yy.min(), xx.min(), yy.min(), xx.max(), yy.max(), i)
    mouth_px = None
    mouth_mask = None
    if best:
        _, bx0, by0, bx1, by1, bidx = best
        mouth_px = ((bx0 + bx1) / 2, (by0 + by1) / 2, bx1 - bx0 + 1)
        mouth_src = "component"
        mouth_mask = lab == bidx
    else:
        # A thin mouth can be drawn joined to the muzzle's own outline, so no
        # separate component exists; scan rows for the longest centred run.
        run = None
        for y in range(band0, band1 + 1):
            x = xlo
            while x <= xhi:
                if not dark[y, x]:
                    x += 1
                    continue
                start = x
                while x + 1 <= xhi and dark[y, x + 1]:
                    x += 1
                rw = x - start + 1
                cx = (start + x) / 2
                if (0.25 * fw <= rw <= 1.2 * fw
                        and abs(cx - (fx0 + fx1) / 2) <= 0.35 * fw):
                    if run is None or rw > run[0]:
                        run = (rw, y, cx, start, x)
                x += 1
        if run is not None:
            rw, ry, rcx, rs, re = run
            mouth_px = (rcx, ry, rw)
            mouth_src = "run"
            # The run itself is one row; widen it so the corners/hook that
            # make the official stroke recognisable reach the expressions.
            wx0, wx1 = max(0, rs - 3), min(farm.shape[1] - 1, re + 3)
            wy0, wy1 = max(0, ry - 2), min(farm.shape[0] - 1, ry + 2)
            mouth_mask = np.zeros_like(dark)
            mouth_mask[wy0:wy1 + 1, wx0:wx1 + 1] = dark[wy0:wy1 + 1, wx0:wx1 + 1]
    if mouth_px is not None:
        mx, my, mw = mouth_px
        s = EYW / fw
        MOUTH_CX = round(EX0 + (mx - fx0) * s)
        MOUTH_CY = round(EY0 + (my - fy0) * s)
        MOUTH_W = int(max(5, min(round(mw * s), round(0.8 * EYW))))
        mouth_box = (MOUTH_CX - MOUTH_W // 2 - 2, MOUTH_CY - 2,
                     MOUTH_CX + MOUTH_W // 2 + 2, MOUTH_CY + 2)
        if MOUTH_DRAW == "native" and mouth_mask is not None:
            ys, xs = np.where(mouth_mask)
            # One point per source column, at the stroke's centre of mass; the
            # official mouth stays in the body bitmap, and the expression cells
            # animate from these points.
            by_col = {}
            for sx, sy in zip(xs, ys):
                by_col.setdefault(int(sx), []).append(float(sy))
            target = {}
            for sx in sorted(by_col):
                ym = sum(by_col[sx]) / len(by_col[sx])
                tx = round(EX0 + (sx - fx0) * s)
                ty = round(EY0 + (ym - fy0) * s)
                target.setdefault(tx, []).append(ty)
            pts = [(tx, round(sum(v) / len(v)))
                   for tx, v in sorted(target.items())]
            pts = [(x, y) for x, y in pts if 0 <= x < CANVAS and 0 <= y < CANVAS]
            if pts:
                OFFICIAL_PTS = pts
                sx0 = min(x for x, _ in pts)
                sx1 = max(x for x, _ in pts)
                sy0 = min(y for _, y in pts)
                sy1 = max(y for _, y in pts)
                # The erase box follows the full stroke bbox (not the
                # centreline's, which is thinner than the painted line) and is
                # widened for the downscale blur.  It must not climb into the
                # nose above the mouth.
                mbx0 = round(EX0 + (int(xs.min()) - fx0) * s)
                mbx1 = round(EX0 + (int(xs.max()) - fx0) * s)
                mby0 = round(EY0 + (int(ys.min()) - fy0) * s)
                mby1 = round(EY0 + (int(ys.max()) - fy0) * s)
                OFFICIAL_BOX = (min(mouth_box[0], mbx0 - 2),
                                min(mouth_box[1], mby0 - 2),
                                max(mouth_box[2], mbx1 + 2),
                                max(mouth_box[3], mby1 + 2))
                cols = np.array([farm[sy, sx] for sy, sx in zip(ys, xs)])
                brights = cols.mean(1)
                sel = cols[brights <= np.percentile(brights, 50)]
                base = np.median(sel if len(sel) else cols, axis=0)
                OFFICIAL_COL = tuple(int(v) for v in base)
                print(f"  official mouth x{sx0}-{sx1} y{sy0}-{sy1} col {OFFICIAL_COL}")
            else:
                print("warning: official mouth not measured; expressions fall back")

# lid fill colour: the brightest of the sides and above, so a hat brim or a
# fur hood on the sides never wins over the face.  (the eye whites sit in a
# dark hood with a bright face slot; the lids must be skin, never fur.)  The
# spot below is left out: it is the muzzle on a snouted face, and its
# tan can out-brighten the yellow skin.  Sampled on the QUANTISED body: a raw
# colour that is not in the palette shows up as a lighter patch over the lids.
# A face whose hair is brighter than its skin (pale blue spikes) breaks
# the "brightest wins" rule -- template/body_recipe.py handles those.
probes = [fp[EX0 - 3, EYC][:3], fp[EX1 + 3, EYC][:3],
          fp[(EX0 + EX1) // 2, EY0 - 3][:3]]
SKIN = max(probes, key=brightness)

# eye region for the lids: bright pixels in the band (whites + the face slot),
# holes filled so the enclosed pupils and the seam are covered too
lid_mask = np.zeros((CANVAS, CANVAS), dtype=bool)
for y in range(EY0 - 3, EY1 + 4):
    for x in range(EX0 - 3, EX1 + 4):
        if rp[x, y][3] > 128 and brightness(rp[x, y]) > 180:
            lid_mask[y, x] = True
lid_mask = ndimage.binary_fill_holes(lid_mask)
_mprobe = [(MOUTH_CX - 5, MOUTH_CY), (MOUTH_CX + 5, MOUTH_CY),
           (MOUTH_CX, MOUTH_CY - 3), (MOUTH_CX, MOUTH_CY + 2),
           (MOUTH_CX - 4, MOUTH_CY - 3), (MOUTH_CX + 4, MOUTH_CY - 3)]
_mcols = [rp[x, y][:3] for x, y in _mprobe
          if 0 <= x < CANVAS and 0 <= y < CANVAS
          and rp[x, y][3] > 128 and brightness(rp[x, y]) > 60]
MOUTH_SKIN = (tuple(int(np.median([c[i] for c in _mcols])) for i in range(3))
              if _mcols else SKIN)
MOUTH_COL = "#3A241C"

# erase the bitmap pupils and the faint bitmap mouth; the face layer owns both.
# The fill colour is the whites' own median: a bbox corner can be skin or a hat.
eye_white = tuple(int(np.median([rp[x, y][i] for x, y in white])) for i in range(3))
# A small circle around each measured pupil, clipped to the whites: erasing
# the whole filled mask would fill the inner seam between two eyes (South
# Park), and an unclipped circle would paint skin white for a sideways pupil.
for px, py in found:
    for y in range(py - 2, py + 3):
        for x in range(px - 2, px + 3):
            if (x - px) ** 2 + (y - py) ** 2 <= 5 and eye_fill[y, x]:
                fp[x, y] = (*eye_white, 255)
# The tight box (mouth width + 4, two rows either side) is small enough that a
# flat fill never reaches the muzzle's own outline; it clears the antialiased
# fringe a dark-only pass would leave.
if MOUTH_DRAW == "primitive" and MOUTH_ON and mouth_box:
    for y in range(max(0, mouth_box[1]), min(CANVAS - 1, mouth_box[3]) + 1):
        for x in range(max(0, mouth_box[0]), min(CANVAS - 1, mouth_box[2]) + 1):
            if fp[x, y][3] > 128:
                fp[x, y] = (*MOUTH_SKIN, 255)
elif MOUTH_DRAW == "native" and OFFICIAL_BOX is not None:
    # Remove the body copy of the official mouth.  The face layer moves with
    # gaze and poses, so a mouth left on the body would show as a second mouth
    # in any action that shifts the face.  The face layer redraws the official
    # stroke and every expression on top.
    for y in range(max(0, OFFICIAL_BOX[1]), min(CANVAS - 1, OFFICIAL_BOX[3]) + 1):
        for x in range(max(0, OFFICIAL_BOX[0]), min(CANVAS - 1, OFFICIAL_BOX[2]) + 1):
            if fp[x, y][3] > 128:
                fp[x, y] = (*MOUTH_SKIN, 255)

# At this size the inner outline of a small pair can vanish into one white
# bar; redraw it.  Large eyes keep their own seam.  A larger pair
# that still merges can force the seam with PORTRAIT_SEAM=draw.
MERGED_EYES = SEAM_MODE == "draw" or (SEAM_MODE != "keep" and EYW <= 14)


# --- face cells --------------------------------------------------------------
def pupils(dx=0, dy=0):
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for px in PUPX:
        d.rectangle([px + dx, PUPY - PUP_H + 1 + dy,
                     px + dx + PUP_W - 1, PUPY + dy], fill="#1A1A1A")
    if MERGED_EYES:
        d.line([(SEAM + dx, EY0 + dy), (SEAM + dx, EY1 + dy)], fill="#1A1A1A")
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


def mouth_sprite():
    """The official mouth centreline, the single face-layer default mouth."""
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    if not OFFICIAL_PTS or OFFICIAL_COL is None:
        return img
    d = ImageDraw.Draw(img)
    col = (*OFFICIAL_COL, 255)
    if len(OFFICIAL_PTS) == 1:
        d.point(OFFICIAL_PTS[0], fill=col)
    else:
        d.line(OFFICIAL_PTS, fill=col, width=1)
    return img


def official_mouth(kind="base"):
    """An expression mouth derived from the official centreline: the ends move
    one pixel or the line waves; never a generic arc at a guessed position."""
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    if not OFFICIAL_PTS or OFFICIAL_COL is None:
        return img
    pts = list(OFFICIAL_PTS)
    if len(pts) >= 2:
        if kind == "happy":
            pts[0] = (pts[0][0], pts[0][1] - 1)
            pts[-1] = (pts[-1][0], pts[-1][1] - 1)
        elif kind == "sad":
            pts[0] = (pts[0][0], pts[0][1] + 1)
            pts[-1] = (pts[-1][0], pts[-1][1] + 1)
        elif kind == "wave":
            pts = [(x, y + (1 if i % 2 else 0))
                   for i, (x, y) in enumerate(pts)]
        ImageDraw.Draw(img).line(pts, fill=(*OFFICIAL_COL, 255), width=1)
    elif len(pts) == 1:
        img.putpixel(pts[0], (*OFFICIAL_COL, 255))
    return img


def lid_cell(rows_to, crease=True, crease_off=0, happy=False):
    """Skin patch over the measured eye region. rows_to limits it from the top
    (None = full close). Only that region is painted; every other pixel stays
    transparent so no hair, hat or hood pixels ghost over the body layer.
    Near-white body pixels in the band are covered too: the eye whites' top
    edge is antialiased, and the band filter leaves that sliver outside the
    mask, where a closed eye would show it as leftover white.

    PORTRAIT_LID_PAD>0 grows the patch so it covers the eye's own dark outline
    and moves the crease to the middle: a face drawn with outlined eyes
    otherwise blinks into two blank ovals.  Left at 0
    for outline-free eyes, where the outline is the hood or the
    cheek and must not be painted over."""
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    p = img.load()
    patch = ndimage.binary_dilation(lid_mask, iterations=LID_PAD) if LID_PAD else lid_mask
    for y in range(EY0 - 3 - LID_PAD, EY1 + 4 + LID_PAD):
        if rows_to is not None and y > rows_to:
            continue
        for x in range(EX0 - 3 - LID_PAD, EX1 + 4 + LID_PAD):
            if not (0 <= x < CANVAS and 0 <= y < CANVAS):
                continue
            # the quantized body is what the reader sees: a raw pixel just under
            # the white threshold can still ship as white
            if patch[y, x] or (fp[x, y][3] > 128 and min(fp[x, y][:3]) > 225):
                p[x, y] = (*SKIN, 255)
    if crease:
        d = ImageDraw.Draw(img)
        col = tuple(round(c * 0.62) for c in SKIN)
        if LID_PAD:
            # the outline is gone, so the lid line is the only thing left to
            # read the closed eye by: one arc across the middle of each eye
            cy = (rows_to if rows_to is not None else EYC) + crease_off
            for ex0, ex1 in ((EX0, SEAM - 1), (SEAM + 1, EX1)):
                if ex1 - ex0 >= 2:
                    d.arc([ex0, cy - 2, ex1, cy + 2], 200 if happy else 25,
                          340 if happy else 155, fill=col, width=1)
        else:
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


if MOUTH_DRAW == "native":
    # The body copy is erased; the official centreline is the single default
    # mouth and every expression is a deformation or opening of it.
    DEFAULT_MOUTH = mouth_sprite()
    BLINK_FACE = compose(lid_cell(None), DEFAULT_MOUTH)
    HAPPY_FACE = compose(lid_cell(None, happy=True), official_mouth("happy"))
    SLEEPY_FACE = compose(lid_cell(None, crease_off=1), DEFAULT_MOUTH)
    HALF_FACE = compose(pupils(), lid_cell(EYC - 1, crease=False), DEFAULT_MOUTH)
    ANGRY_FACE = compose(pupils(), lid_cell(EYC - 2, crease=False),
                         official_mouth("sad"))
    YAWN_FACE = compose(lid_cell(None), open_mouth(3, 3))
    YAWN_HALF = compose(lid_cell(None), open_mouth(2, 2))
    SURPRISED_FACE = compose(pupils(), open_mouth(2, 2))
    DIZZY_MOUTH = (official_mouth("wave"),)
else:
    DEFAULT_MOUTH = mouth(MOUTH_STYLE)
    BLINK_FACE = compose(lid_cell(None), DEFAULT_MOUTH)
    HAPPY_FACE = compose(lid_cell(None, happy=True), mouth("smile"))
    SLEEPY_FACE = compose(lid_cell(None, crease_off=1), mouth("line"))
    HALF_FACE = compose(pupils(), lid_cell(EYC - 1, crease=False), mouth("frown"))
    ANGRY_FACE = compose(pupils(), lid_cell(EYC - 2, crease=False), mouth("sad"))
    YAWN_FACE = compose(lid_cell(None), open_mouth(3, 3))
    YAWN_HALF = compose(lid_cell(None), open_mouth(2, 2))
    SURPRISED_FACE = compose(pupils(), open_mouth(2, 2))
    DIZZY_MOUTH = (mouth("wave"),)

DIZZY_FACE = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
d = ImageDraw.Draw(DIZZY_FACE)
_xr = 1 if EYW <= 12 else 2
for px in PUPX:
    d.line([(px - _xr, PUPY - _xr), (px + _xr, PUPY + _xr)], fill="#1A1A1A", width=1)
    d.line([(px - _xr, PUPY + _xr), (px + _xr, PUPY - _xr)], fill="#1A1A1A", width=1)
for layer in DIZZY_MOUTH:
    DIZZY_FACE.alpha_composite(layer)

# angry brows only where there is forehead skin above the eyes; under a hat
# brim or a hood they would draw on the garment, so the lids carry the anger
_mid = (EX0 + EX1) // 2
_p = rp[_mid, EY0 - 3]
if BROWS_MODE != "none" and _p[3] > 128 and brightness(_p) > 150:
    d = ImageDraw.Draw(ANGRY_FACE)
    d.line([(EX0 + 2, EY0 - 4), (SEAM - 3, EY0 - 2)], fill="#1A1A1A", width=1)
    d.line([(SEAM + 3, EY0 - 2), (EX1 - 2, EY0 - 4)], fill="#1A1A1A", width=1)

FACE = {
    "default": compose(pupils(), DEFAULT_MOUTH),
    "blink": BLINK_FACE,
    "sleepy": SLEEPY_FACE,
    "look_up": compose(pupils(0, -1), DEFAULT_MOUTH),
    "look_down": compose(pupils(0, 1), DEFAULT_MOUTH),
    "look_l": compose(pupils(-1, 0), DEFAULT_MOUTH),
    "look_r": compose(pupils(1, 0), DEFAULT_MOUTH),
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
# frames stay still and only the face changes.  A crossed-arms pose has no free
# hand; PORTRAIT_BOOK=none skips it.
BOOK_AT = None
if BOOK_MODE != "none":
    _iy0, _iy1 = FIG[1], FIG[3]
    _lo, _hi = _iy0 + round(0.62 * (_iy1 - _iy0)), _iy0 + round(0.88 * (_iy1 - _iy0))
    _hand_edge = min(x for y in range(_lo, _hi + 1) for x in range(CANVAS)
                     if rp[x, y][3] > 128)
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
            blink=True,
            **({"effect": {"kind": "book", "at": BOOK_AT}} if BOOK_AT else {})),
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

print(f"{PID}: eyes x{EX0}-{EX1} y{EY0}-{EY1} seam {SEAM} "
      f"pupils {PUPX},{PUPY} {PUP_W}x{PUP_H} "
      f"mouth {MOUTH_ON} {mouth_src} {MOUTH_DRAW}/{MOUTH_STYLE} "
      f"({MOUTH_CX},{MOUTH_CY}) "
      f"w{MOUTH_W} skin {SKIN} "
      f"book {BOOK_AT is not None} brows {BROWS_MODE != 'none'} lidpad {LID_PAD} "
      f"merged {MERGED_EYES} fit {FIT} scale {SCALE}")
sys.exit(pet_kit.render(RECIPE, OUT))
