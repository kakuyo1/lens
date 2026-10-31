# -*- coding: utf-8 -*-
"""Recipe-driven renderer for Lens desktop-pet packages (format v1).

The recipe is pure data: a rig (species shape in 3px-grid cells), an
expression list, per-action frame poses, and the optional blocks the format
doc defines. The kit draws every sheet, derives anchors.json from the poses
(translation only; squash is baked into the art), and guarantees by
construction that the face lands on the head and every sheet matches
frames x canvas. After writing it runs validate_package on the result.

Usage:
    python -I -c "
        import pet_kit; pet_kit.render(RECIPE, r'out_dir')"

Recipe schema (grid coords are cells; cell (x, y) covers pixels
(30+3x, 30+3y)..(+3, +3) on the 88 px canvas, x/y within 0..18):

Start from pet_kit.PRESETS["dog"] and tweak; "rig"."scale" resizes the
whole animal about its feet line with one number.

{
  "id": "annoying_dog",            # ^[a-z0-9_]{1,32}$, must be the folder name
  "name": "Annoying Dog",          # optional display name
  "rig": {
    "scale": 1.0,                    # optional one-knob resize (0.8-1.2 safe; see _scaled_rig)
    "body":  {"cx": 9, "cy": 10, "rx": 6, "ry": 5.5},
    "head":  None,                   # or {"cx","cy","rx","ry"} for two-blob species
    "ears":  {"style": "pointy",     # pointy | round | floppy
              "at": [3.5, 1.5], "w": 2.5, "h": 4, "inner": "#F7B6C2"},
    "tail":  {"style": "up",         # stub | curl | up
              "at": [15, 11], "len": 3},
    "feet":  {"y": 15.5, "w": 2.5, "h": 1.8, "gap": 3},
    "eyes":  {"at": [6, 9], "size": 2, "style": "oval"},   # left eye; right mirrored
                                                   # keep >=2 cells between the eyes at size 2
    "mouth": {"at": [9, 12.5], "w": 4, "style": "grin"},   # grin | smile | fangs
    "colors": {"body": "#FFFFFF", "outline": "#1A1A1A", "eye": "#1A1A1A",
               "mouth": "#1A1A1A", "inner_ear": "#F7B6C2", "blush": None}
  },
  "expressions": ["default", "blink", "closed", "happy", "sleepy", "squint",
                  "surprised", "angry", "hearts", "dizzy", "yawn", "half",
                  "sad", "wink", "smug", "flat"],   # index 0 MUST be "default"
  "blink": {"fps": 10, "pattern": [1, 2, 1]},       # optional override
  "actions": {
    "<name>": {
      "fps": 6, "loop": True,
      "duration": None, "returnTo": None, "blink": True,
      "effect": None,                                # or {"kind": "hearts|stars|zzz|notes|dust",
                                                     #     "color": "#F1948A"}
      "frames": [
        {"pose": {"dx": 0, "dy": 0, "sx": 1.0, "sy": 1.0}, "expression": "default"},
        ...
      ]
    }
  },
  "roles": {"idle": "idle", ...},    # optional; default maps role -> same-named action
  "idlePool": None,                  # optional; default = one-shot actions no role names
  "hover": None,                     # passthrough, e.g. {"notice": "perk_up", "dwell": "tail_wag"}
  "movement": None,                  # passthrough, e.g. {"style": "trot", "walk": "walk"}
  "behavior": None,                  # passthrough temperament block
  "accessories": [                   # optional
    {"id": "hat", "slot": "head", "zIndex": 12,
     "art": [{"op": "rows", "at": [35, 8], "rows": ["..#####.."], "color": "#E74C3C"}],
     "except": ["sleep"]}            # supportedActions defaults to all actions minus except
  ]
}

Art ops (accessories): at is in CELLS for cell/rect/ellipse; for "rows" at is
the PIXEL top-left of the block and one char = one pixel (scale > 1 switches
"at" to cells with one char = scale px). Ops: cell, rect, ellipse, rows.
"""
import json
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

CANVAS = 88
ORIGIN = 30
CELL = 3
GRID_MAX = 18  # inclusive cell index on either axis

DEFAULT_COLORS = {
    "body": "#FFFFFF", "outline": "#1A1A1A", "eye": "#1A1A1A",
    "mouth": "#1A1A1A", "inner_ear": "#F7B6C2", "blush": None,
}

# Species presets; copy one into the recipe and tweak.
PRESETS = {
    "dog": {
        "body": {"cx": 9, "cy": 9.77, "rx": 7.68, "ry": 7.81},
        "ears": {"style": "pointy", "at": [4.5, 0.0], "w": 3.84, "h": 3.84, "inner": None},
        "tail": {"style": "stub", "at": [16.1, 10.4], "len": 2.6},
        "feet": {"y": 17.32, "w": 3.33, "h": 2.56, "gap": 4.35},
        "eyes": {"at": [5.2, 5.7], "size": 2.56, "style": "dot"},
        "mouth": {"at": [9, 11.7], "w": 5.12, "style": "smile"},
        "nose": {"at": [56, 57], "rows": ["###", "###", "###", "###"]},
        "colors": {"body": "#FFFFFF", "outline": "#000000", "eye": "#000000",
                   "mouth": "#000000", "blush": None},
    },
}


_SIZE_KEYS = {"rx", "ry", "w", "h", "len", "gap", "size"}


def _scaled_rig(rig):
    """Apply rig.scale (default 1.0): zoom measures about the feet-line centre so one
    number resizes the pet without touching other fields. Positions clamp to the grid
    with a warning -- the dog preset's ear tips already touch the canvas top, so large
    upscales (>1.1) clip them; edit the rig numbers directly for big jumps."""
    s = rig.get("scale", 1.0)
    if s == 1.0:
        return rig
    body = rig["body"]
    ax = body["cx"]
    ay = rig.get("feet", {}).get("y", body["cy"] + body["ry"])
    clamped = []

    def zoom(v, axis):
        out = (ax if axis == 0 else ay) + (v - (ax if axis == 0 else ay)) * s
        if out < 0 or out > GRID_MAX:
            clamped.append(round(out, 2))
            return max(0.0, min(GRID_MAX, out))
        return round(out, 3)

    def zoom_dict(d, pixel=False):
        out = dict(d)
        for k, v in out.items():
            if k in _SIZE_KEYS and isinstance(v, (int, float)) and not isinstance(v, bool):
                out[k] = round(v * s, 3)
            elif k == "at":
                if pixel:  # nose rows: pixel-space [px, py], anchor on the face centre
                    out[k] = [round(CANVAS / 2 + (v[0] - CANVAS / 2) * s, 1),
                              round(ORIGIN + body["cy"] * CELL + (v[1] - (ORIGIN + body["cy"] * CELL)) * s, 1)]
                elif isinstance(v, (list, tuple)):
                    out[k] = [zoom(v[0], 0), zoom(v[1], 1)]
        return out

    out = {}
    for key, val in rig.items():
        if key in ("scale", "colors"):
            out[key] = val
        elif key == "nose":
            out[key] = zoom_dict(val, pixel=True)
        elif isinstance(val, dict):
            out[key] = zoom_dict(val)
        else:
            out[key] = val
    if clamped:
        print(f"WARN rig.scale {s}: clamped positions {clamped} to the grid")
    return out


# --- geometry ---------------------------------------------------------------

class Xform:
    """Pose transform: cell translate + squash about the feet line."""

    def __init__(self, pose, pivot):
        self.dx = pose.get("dx", 0) * CELL
        self.dy = pose.get("dy", 0) * CELL
        self.sx = pose.get("sx", 1.0)
        self.sy = pose.get("sy", 1.0)
        self.pivot = pivot  # pixel point the squash is centred on

    def pt(self, px, py):
        return (self.pivot[0] + (px - self.pivot[0]) * self.sx + self.dx,
                self.pivot[1] + (py - self.pivot[1]) * self.sy + self.dy)

    def box(self, cx, cy, rx, ry):
        """Ellipse bounding box in pixels for a centre/radii given in cells."""
        x0, y0 = self.pt(ORIGIN + (cx - rx) * CELL, ORIGIN + (cy - ry) * CELL)
        x1, y1 = self.pt(ORIGIN + (cx + rx) * CELL, ORIGIN + (cy + ry) * CELL)
        return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]


def _colors(rig):
    return {**DEFAULT_COLORS, **rig.get("colors", {})}


def _pivot(rig):
    feet = rig.get("feet") or {"y": rig["body"]["cy"] + rig["body"]["ry"], "h": 0}
    return (CANVAS / 2.0, ORIGIN + (feet["y"] + feet.get("h", 0) / 2 + 0.5) * CELL)


# --- rig drawing ------------------------------------------------------------

def draw_rig(draw, rig, xf):
    c = _colors(rig)
    body = rig["body"]
    draw.ellipse(xf.box(body["cx"], body["cy"], body["rx"], body["ry"]),
                 fill=c["body"], outline=c["outline"], width=1)
    head = rig.get("head")
    if head:
        draw.ellipse(xf.box(head["cx"], head["cy"], head["rx"], head["ry"]),
                     fill=c["body"], outline=c["outline"], width=1)
    _ears(draw, rig, xf, c)
    _tail(draw, rig, xf, c)
    feet = rig.get("feet")
    if feet:
        w, gap = feet["w"], feet.get("gap", 3)
        left_cx = body["cx"] - gap / 2 - w / 2
        right_cx = body["cx"] + gap / 2 + w / 2
        for cx in (left_cx, right_cx):
            draw.ellipse(xf.box(cx, feet["y"], w / 2, feet["h"] / 2),
                         fill=c["body"], outline=c["outline"], width=1)


def _ears(draw, rig, xf, c):
    ears, body = rig.get("ears"), rig["body"]
    if not ears:
        return
    style, (ex, ey) = ears["style"], ears["at"]
    w, h, inner = ears["w"], ears["h"], ears.get("inner", c["inner_ear"])
    for sign in (1, -1):  # 1 = left ear at "at"; -1 = mirrored about the body axis
        x0 = ex if sign > 0 else 2 * body["cx"] - (ex + w)
        if style == "pointy":
            tip_y = ey
            bx0, bx1 = x0, x0 + w

            def _on_arc(bx):
                return body["cy"] - body["ry"] * math.sqrt(
                    max(0.0, 1 - ((bx - body["cx"]) / body["rx"]) ** 2))

            pts = [xf.pt(ORIGIN + bx0 * CELL, ORIGIN + _on_arc(bx0) * CELL),
                   xf.pt(ORIGIN + (bx0 + w / 2) * CELL, ORIGIN + tip_y * CELL),
                   xf.pt(ORIGIN + bx1 * CELL, ORIGIN + _on_arc(bx1) * CELL)]
            draw.polygon(pts, fill=c["body"])
            draw.line([pts[0], pts[1]], fill=c["outline"], width=1)
            draw.line([pts[1], pts[2]], fill=c["outline"], width=1)
            if inner:
                draw.polygon([xf.pt(ORIGIN + (x0 + w * 0.32) * CELL, ORIGIN + (base_y - h * 0.1) * CELL),
                              xf.pt(ORIGIN + (x0 + w / 2) * CELL, ORIGIN + (tip_y + h * 0.3) * CELL),
                              xf.pt(ORIGIN + (x0 + w * 0.68) * CELL, ORIGIN + (base_y - h * 0.1) * CELL)],
                             fill=inner)
        else:
            draw.ellipse(xf.box(x0 + w / 2, ey + h / 2, w / 2, h / 2),
                         fill=c["body"], outline=c["outline"], width=1)
            if inner:
                draw.ellipse(xf.box(x0 + w / 2, ey + h * 0.6, w / 4, h / 4), fill=inner)


def _tail(draw, rig, xf, c):
    tail = rig.get("tail")
    if not tail:
        return
    style, (tx, ty), ln = tail["style"], tail["at"], tail.get("len", 3)
    if style == "stub":
        draw.ellipse(xf.box(tx + ln / 2, ty, ln / 2, ln / 3),
                     fill=c["body"], outline=c["outline"], width=1)
    elif style == "curl":
        draw.arc(xf.box(tx + ln / 2, ty - ln / 3, ln / 2, ln / 3), 180, 520,
                 fill=c["outline"], width=2)
    else:  # up
        draw.line([xf.pt(ORIGIN + tx * CELL, ORIGIN + (ty + 1) * CELL),
                   xf.pt(ORIGIN + (tx + ln * 0.35) * CELL, ORIGIN + (ty - ln) * CELL)],
                  fill=c["outline"], width=2)


# --- face --------------------------------------------------------------------

HEART = [".#.#.", "#####", "#####", ".###.", "..#.."]


def _rows(draw, rows, at, color, scale=1):
    for r, row in enumerate(rows):
        for cc, ch in enumerate(row):
            if ch != "#":
                continue
            if scale == 1:
                draw.point((at[0] + cc, at[1] + r), fill=color)
            else:
                draw.rectangle([at[0] + cc * scale, at[1] + r * scale,
                                at[0] + (cc + 1) * scale - 1, at[1] + (r + 1) * scale - 1],
                               fill=color)


def draw_face(draw, rig, name):
    """Draw one expression into a cell with the rig at neutral pose."""
    c = _colors(rig)
    eyes, mouth = rig["eyes"], rig["mouth"]
    esz = eyes.get("size", 1)
    body_cx = rig["body"]["cx"]
    eye_c, mouth_c, body_c = c["eye"], c["mouth"], c["body"]
    mw = mouth.get("w", 3)
    mouth_l = ORIGIN + (mouth["at"][0] - mw / 2) * CELL
    mouth_r = ORIGIN + (mouth["at"][0] + mw / 2) * CELL
    mouth_cy = ORIGIN + mouth["at"][1] * CELL
    style = mouth.get("style", "smile")
    nose = rig.get("nose")
    if nose:
        _rows(draw, nose["rows"], tuple(nose["at"]), nose.get("color", eye_c), 1)

    def eye_cell(sign):
        x = eyes["at"][0] if sign < 0 else 2 * body_cx - eyes["at"][0] - esz
        return (x, eyes["at"][1])

    def each_eye(fn):
        for sign in (-1, 1):
            fn(eye_cell(sign), sign)

    def dot(cell, _sign):
        x, y = cell
        draw.ellipse([ORIGIN + x * CELL, ORIGIN + y * CELL,
                      ORIGIN + (x + esz) * CELL, ORIGIN + (y + esz) * CELL], fill=eye_c)

    def oval(cell, _sign):
        x, y = cell
        draw.ellipse([ORIGIN + x * CELL, ORIGIN + y * CELL - 1,
                      ORIGIN + (x + esz) * CELL - 1, ORIGIN + (y + esz + 1) * CELL], fill=eye_c)

    def lid(cell, _sign):
        x, y = cell
        draw.rectangle([ORIGIN + x * CELL, ORIGIN + (y + esz / 2) * CELL,
                        ORIGIN + (x + esz) * CELL - 1, ORIGIN + (y + esz / 2) * CELL + 1],
                       fill=eye_c)

    def happy_lid(cell, _sign):
        x, y = cell
        cxp, cyp = ORIGIN + (x + esz / 2) * CELL, ORIGIN + (y + esz / 2) * CELL
        draw.line([(cxp - CELL, cyp + 1), (cxp, cyp - 2), (cxp + CELL, cyp + 1)],
                  fill=eye_c, width=1)

    def x_eye(cell, _sign):
        cxp, cyp = ORIGIN + (cell[0] + esz / 2) * CELL, ORIGIN + (cell[1] + esz / 2) * CELL
        r = esz * CELL / 2
        draw.line([(cxp - r, cyp - r), (cxp + r, cyp + r)], fill=eye_c, width=1)
        draw.line([(cxp - r, cyp + r), (cxp + r, cyp - r)], fill=eye_c, width=1)

    def heart_eye(cell, _sign):
        _rows(draw, HEART, (ORIGIN + cell[0] * CELL - 1, ORIGIN + cell[1] * CELL - 1),
              "#E74C3C", 1)

    def brow(cell, sign):
        cxp, cyp = ORIGIN + (cell[0] + esz / 2) * CELL, ORIGIN + cell[1] * CELL - 3
        tilt = 1 if sign < 0 else -1  # inner ends down: angry
        draw.line([(cxp - 3, cyp - tilt), (cxp + 3, cyp + tilt)], fill=eye_c, width=1)

    def zigzag(open_px):
        teeth = max(4, int(mw * 2))
        depth = open_px + 3
        pts = [(mouth_l, mouth_cy - open_px), (mouth_r, mouth_cy - open_px)]
        step = (mouth_r - mouth_l) / teeth
        for i in range(teeth + 1):
            pts.append((mouth_l + i * step, mouth_cy + (depth if i % 2 else open_px)))
        draw.polygon(pts, fill=mouth_c)

    def smile(_open_px=0):
        draw.arc([mouth_l, mouth_cy - 3, mouth_r, mouth_cy + 5], 10, 170,
                 fill=mouth_c, width=2)

    def line_mouth(_open_px=0):
        draw.line([(mouth_l + 2, mouth_cy), (mouth_r - 2, mouth_cy)], fill=mouth_c, width=2)
        for cxp in (mouth_l, mouth_r):
            draw.rectangle([cxp, mouth_cy - 3, cxp + 1, mouth_cy - 2], fill=eye_c)

    def o_mouth(rx, ry):
        cxp = (mouth_l + mouth_r) / 2
        draw.ellipse([cxp - rx, mouth_cy - ry, cxp + rx, mouth_cy + ry],
                     outline=mouth_c, width=2, fill=body_c)

    open_mouth = zigzag if style == "grin" else (line_mouth if style == "line" else smile)
    if name == "default":
        each_eye(oval if eyes.get("style") == "oval" else dot)
        open_mouth(0)
    elif name == "blink":
        each_eye(lid)
        open_mouth(0)
    elif name == "closed":
        each_eye(happy_lid)
        smile()
    elif name == "happy":
        each_eye(happy_lid)
        zigzag(1)
    elif name == "sleepy":
        each_eye(lid)
        o_mouth(2, 2)
    elif name == "squint":
        each_eye(lid)
        open_mouth(0)
    elif name == "surprised":
        each_eye(dot)
        o_mouth(3, 3)
    elif name == "angry":
        each_eye(dot)
        each_eye(brow)
        zigzag(1)
    elif name == "hearts":
        each_eye(heart_eye)
        smile()
    elif name == "dizzy":
        each_eye(x_eye)
        draw.arc([mouth_l, mouth_cy - 2, mouth_r, mouth_cy + 4], 200, 340,
                 fill=mouth_c, width=2)
    elif name == "yawn":
        each_eye(lid)
        o_mouth(4, 5)
    elif name == "half":
        each_eye(dot)
        each_eye(lid)  # lid over the dot reads as half-closed
        smile()
    elif name == "sad":
        each_eye(dot)
        draw.arc([mouth_l, mouth_cy - 2, mouth_r, mouth_cy + 5], 190, 350,
                 fill=mouth_c, width=2)
    elif name == "wink":
        each_eye(lambda cell, s: lid(cell, s) if s < 0 else dot(cell, s))
        smile()
    elif name == "smug":
        each_eye(lid)
        draw.arc([mouth_l, mouth_cy - 4, mouth_r, mouth_cy + 4], 300, 80,
                 fill=mouth_c, width=2)
    else:  # flat
        each_eye(dot)
        draw.line([(mouth_l + 2, mouth_cy), (mouth_r - 2, mouth_cy)], fill=mouth_c, width=2)

    if c.get("blush") and name in ("default", "happy", "smug"):
        my = mouth["at"][1]
        for sign in (-1, 1):
            bx = body_cx + sign * (rig["body"]["rx"] - 1)
            draw.ellipse([ORIGIN + (bx - 1) * CELL, ORIGIN + (my - 1) * CELL,
                          ORIGIN + (bx + 1) * CELL, ORIGIN + (my + 1) * CELL], fill=c["blush"])


# --- effects ------------------------------------------------------------------

def draw_effect(draw, kind, color, frame, frames):
    t = frame / max(1, frames)
    if kind == "hearts":
        for k in range(3):
            x = 14 + (k % 2) * 56 + (frame * 5) % 18
            y = int(36 - t * 24 - k * 5) % CANVAS
            _rows(draw, HEART, (x, y), color, 2 if k == 0 else 1)
    elif kind == "stars":
        for k in range(4):
            ang = t * 2 * math.pi + k * math.pi / 2
            x = int(44 + 30 * math.cos(ang))
            y = int(28 + 12 * math.sin(ang))
            draw.line([(x - 4, y), (x + 4, y)], fill=color, width=2)
            draw.line([(x, y - 4), (x, y + 4)], fill=color, width=2)
    elif kind == "zzz":
        for k in range(3):
            _rows(draw, ["#.##", "..#.", ".#..", "#.##"],
                  (58 + k * 9, int(18 + k * 9 - t * 8)), color, 1)
    elif kind == "notes":
        for k in range(2):
            _rows(draw, ["..##", "..##", "..#.", ".##.", ".#.."],
                  (16 + k * 46, int(40 - t * 20 - k * 10)), color, 2)
    else:  # dust
        for k in range(3):
            x = 18 + k * 24 + (frame * 7) % 12
            y = 78 - (frame * 3 + k * 5) % 10
            draw.ellipse([x, y, x + 4, y + 3], fill=color)


# --- render --------------------------------------------------------------------

def render_frame(rig, pose):
    xf = Xform(pose, _pivot(rig))
    body = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw_rig(ImageDraw.Draw(body), rig, xf)
    return body, (int(round(xf.dx)), int(round(xf.dy)))


def render(recipe, out_dir):
    out = Path(out_dir)
    pkg = out / "pets" / recipe["id"]
    sprites = pkg / "sprites"
    body_dir, fx_dir = sprites / "body", sprites / "effects"
    expr_dir, acc_dir = sprites / "expressions", sprites / "accessories"
    for d in (body_dir, fx_dir, expr_dir, acc_dir):
        d.mkdir(parents=True, exist_ok=True)

    rig = _scaled_rig(recipe["rig"])
    names = list(recipe["expressions"])
    assert names and names[0] == "default", "expressions[0] must be 'default'"
    # auto-append expression names used by actions but missing from the sheet
    used = {fr.get("expression", "default")
            for spec in recipe["actions"].values() for fr in spec["frames"]}
    for n in list(names) + sorted(used):
        assert n in FACE_NAMES, f"unknown expression name: {n}"
    names += [n for n in sorted(used) if n not in names]

    sheet = Image.new("RGBA", (CANVAS * len(names), CANVAS), (0, 0, 0, 0))
    for i, name in enumerate(names):
        cell_img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
        draw_face(ImageDraw.Draw(cell_img), rig, name)
        sheet.alpha_composite(cell_img, (i * CANVAS, 0))
    sheet.save(expr_dir / "eyes.png")

    name_to_idx = {n: i for i, n in enumerate(names)}
    actions, anchors = {}, {}
    for a_name, spec in recipe["actions"].items():
        anch, expr_idx = [], []
        for fr in spec["frames"]:
            _, off = render_frame(rig, fr.get("pose", {}))
            anch.append({"head": list(off), "face": list(off), "body": list(off)})
            expr_idx.append(name_to_idx[fr.get("expression", "default")])
        body_imgs = [render_frame(rig, f.get("pose", {}))[0] for f in spec["frames"]]
        strip = Image.new("RGBA", (CANVAS * len(body_imgs), CANVAS), (0, 0, 0, 0))
        for i, img in enumerate(body_imgs):
            strip.alpha_composite(img, (i * CANVAS, 0))
        strip.save(body_dir / f"{a_name}.png")
        if spec.get("effect"):
            fx = Image.new("RGBA", (CANVAS * len(body_imgs), CANVAS), (0, 0, 0, 0))
            d = ImageDraw.Draw(fx)
            for i in range(len(body_imgs)):
                draw_effect(d, spec["effect"]["kind"], spec["effect"].get("color", "#F1948A"),
                            i, len(body_imgs))
            fx.save(fx_dir / f"{a_name}.png")
        actions[a_name] = {
            "frames": len(spec["frames"]), "fps": spec["fps"], "loop": bool(spec["loop"]),
            "expressions": expr_idx, "sheet": f"sprites/body/{a_name}.png",
        }
        if spec.get("effect"):
            actions[a_name]["effect"] = f"sprites/effects/{a_name}.png"
        for key in ("duration", "returnTo", "blink"):
            if spec.get(key):
                actions[a_name][key] = spec[key]
        anchors[a_name] = anch

    blink_cfg = recipe.get("blink", {})
    blink_pattern = blink_cfg.get("pattern", [1, 2, 1])
    assert all(i < len(names) for i in blink_pattern), "blink pattern out of range"
    actions["blink"] = {"frames": len(blink_pattern), "fps": blink_cfg.get("fps", 10),
                        "loop": False, "expressions": blink_pattern}

    role_names = {"idle", "study", "thinking", "celebrate", "encourage", "sleep",
                  "click_react", "pickup", "look_around", "yawn", "stretch",
                  "angry", "ask", "dizzy"}
    roles = recipe.get("roles") or {r: r for r in recipe["actions"] if r in role_names}
    mapped = set(roles.values())
    idle_pool = recipe.get("idlePool")
    if idle_pool is None:
        idle_pool = [a for a, s in recipe["actions"].items()
                     if a not in mapped and not s["loop"]]

    doc = {"format": 1, "id": recipe["id"], "canvas": CANVAS, "scale": 3,
           "layers": {"body": 0, "expression": 2, "effect": 6},
           "expression": "sprites/expressions/eyes.png"}
    if recipe.get("name"):
        doc["name"] = recipe["name"]
    (pkg / "pet.json").write_text(json.dumps(doc, indent=2), encoding="utf-8")

    anim = {"roles": roles, "idlePool": idle_pool, "actions": actions}
    if recipe.get("hover"):
        anim["hover"] = recipe["hover"]
    if recipe.get("movement"):
        anim["movement"] = recipe["movement"]
    if recipe.get("behavior"):
        anim["behavior"] = recipe["behavior"]
    (pkg / "animations.json").write_text(json.dumps(anim, indent=2), encoding="utf-8")
    (pkg / "anchors.json").write_text(json.dumps({"actions": anchors}, indent=2), encoding="utf-8")

    all_actions = [a for a in actions if a != "blink"]
    acc_list = []
    for acc in recipe.get("accessories", []):
        img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
        _draw_ops(ImageDraw.Draw(img), acc["art"])
        img.save(acc_dir / f"{acc['id']}.png")
        acc_list.append({
            "id": acc["id"], "slot": acc["slot"],
            "asset": f"sprites/accessories/{acc['id']}.png",
            "zIndex": acc.get("zIndex", {"head": 12, "face": 15, "body": 8}[acc["slot"]]),
            "supportedActions": [a for a in all_actions if a not in acc.get("except", [])],
        })
    (pkg / "accessories.json").write_text(json.dumps({"accessories": acc_list}, indent=2),
                                          encoding="utf-8")

    print("package written to", pkg)
    sys.path.insert(0, str(Path(__file__).parent))
    import validate_package
    ok = validate_package.run(str(pkg))
    return 0 if ok else 1


def _draw_ops(draw, ops):
    for op in ops:
        color = op.get("color", "#1A1A1A")
        at, op_name = op["at"], op["op"]
        if op_name == "rows":
            scale = op.get("scale", 1)
            base = (ORIGIN + at[0] * CELL, ORIGIN + at[1] * CELL) if scale > 1 else tuple(at)
            _rows(draw, op["rows"], base, color, scale)
        elif op_name == "cell":
            draw.rectangle([ORIGIN + at[0] * CELL, ORIGIN + at[1] * CELL,
                            ORIGIN + at[0] * CELL + CELL - 1, ORIGIN + at[1] * CELL + CELL - 1],
                           fill=color)
        elif op_name == "rect":
            w, h = op["size"]
            draw.rectangle([ORIGIN + at[0] * CELL, ORIGIN + at[1] * CELL,
                            ORIGIN + (at[0] + w) * CELL - 1, ORIGIN + (at[1] + h) * CELL - 1],
                           fill=color)
        elif op_name == "ellipse":
            rx, ry = op["r"]
            draw.ellipse([ORIGIN + (at[0] - rx) * CELL, ORIGIN + (at[1] - ry) * CELL,
                          ORIGIN + (at[0] + rx) * CELL, ORIGIN + (at[1] + ry) * CELL], fill=color)


FACE_NAMES = {"default", "blink", "closed", "happy", "sleepy", "squint", "surprised",
              "angry", "hearts", "dizzy", "yawn", "half", "sad", "wink", "smug", "flat"}
