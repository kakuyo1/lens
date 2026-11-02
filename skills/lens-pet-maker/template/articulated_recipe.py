# -*- coding: utf-8 -*-
"""Template: articulated multi-frame actions from one flat official render.

A flat sprite has no pixels behind a limb, so "just move the arm" leaves a hole
and "just cut a polygon for the head" cracks the cheeks. This recipe is the
worked fix: parts are cut by colour/silhouette, their vacated footprint is
filled underneath, and the action is expressed by rotation plus a few pixels of
travel instead of a long slide.

What it writes (per action), ready to merge into a package:
    <out>/<action>.png            frames x CANVAS, one cell per frame
    <out>/<action>-anchors.json   [{"head":[x,y],"face":[x,y],"body":[x,y]}, ...]
    <out>/<action>-check.png      magenta montage, the acceptance view

Usage:
    PET_REF_IMAGE=<official transparent png> python -I articulated_recipe.py <out-dir>

Adapting it: edit SPEC (fit, mouth, head region/seed, gloves, parts) and the two
ACTIONS timelines. Every number below was measured on one official render; see
``references/articulated-actions.md`` for how to measure them on another one.

Third-party dependency: Pillow only. The official art itself stays with you --
do not commit or redistribute it.
"""

import json
import os
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

CANVAS = 88
SS = 4  # supersample factor; rotate and compose here, downsample once
OUT = CANVAS * SS

SOURCE = os.environ.get("PET_REF_IMAGE", "")
if not SOURCE:
    sys.exit("set PET_REF_IMAGE to your official transparent render first")
DEST = Path(sys.argv[1] if len(sys.argv) > 1 else os.getcwd())

# Mouth sizes in CANVAS pixels (see mouth_head for the source-pixel conversion).
MOUTHS = {
    "closed": None,
    "open": (3.8, 1.35, True),
    "chew": (2.6, 0.85, False),
    "shout": (4.7, 1.75, True),
    "grin": (4.0, 1.05, False),
}

SPEC = {
    "source": Path(SOURCE),
    "fit": 0.80,  # must equal the fit of the idle already shipped from this render
    "mouth": {"erase": (903, 682, 1024, 717), "center": (963, 699), "skin": (254, 220, 181)},
    "head": {
        "region": (575, 165, 1320, 800),  # the head's bounding box, in source pixels
        "seed": (960, 620),               # a point on the face, for the flood fill
        "grow": 8,                        # source px of garment taken with the head
        "pivot": (960, 706),              # neck root
    },
    # Mittens: colour region to cut, plus the flat colour to fill their footprint.
    "gloves": {
        "glove_left": {"region": (430, 650, 710, 890), "support": (0, 0, 0, 0, (212, 30, 60))},
        "glove_right": {"region": (1210, 650, 1440, 880), "support": (0, 0, 0, 0, (212, 30, 60))},
    },
    # Forearms: polygon + pivot + an under-layer ellipse of the garment colour.
    "parts": {},
    "feet_half": 150,   # half the foot line's width, for the rock pivot
    "bag_at": (15, 56), # a prop drawn in canvas pixels, carried by the left mitten
}


def yellow_mask(source, region):
    """A mitten: bright yellow grown two source pixels to take its dark outline."""
    x0, y0, x1, y1 = region
    mask = Image.new("L", source.size, 0)
    pixels = mask.load()
    colors = source.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = colors[x, y]
            if a > 96 and r > 180 and g > 150 and b < 120 and r - b > 90:
                pixels[x, y] = 255
    grown = mask.filter(ImageFilter.MaxFilter(7))
    return ImageChops.multiply(grown, source.getchannel("A"))


class Figure:
    def __init__(self, spec):
        self.spec = spec
        source = Image.open(spec["source"]).convert("RGBA")
        self.source = source
        self.box = source.getchannel("A").getbbox()
        width, height = self.box[2] - self.box[0], self.box[3] - self.box[1]
        scale = min((CANVAS - 4) / width, (CANVAS - 4) / height) * spec["fit"]
        self.size = (round(width * scale), round(height * scale))
        self.offset = ((CANVAS - self.size[0]) // 2, CANVAS - 2 - self.size[1])
        self.scale_x = self.size[0] / width
        self.scale_y = self.size[1] / height
        self.unit_x = 1 / self.scale_x  # source pixels per canvas pixel
        self.unit_y = 1 / self.scale_y

        masks = {"head": self.head_mask(spec["head"])}
        for name, glove in spec["gloves"].items():
            masks[name] = yellow_mask(source, glove["region"])
        for name, part in spec["parts"].items():
            masks[name] = self.polygon(part["poly"])
        self.masks = masks

        assigned = Image.new("L", source.size, 0)
        for mask in masks.values():
            assigned = ImageChops.lighter(assigned, mask)
        root = source.copy()
        root.putalpha(ImageChops.subtract(source.getchannel("A"), assigned))
        self.root = self.fit(root)
        self.parts = {name: self.fit(self.tinted(source, mask)) for name, mask in masks.items()}

        # Under-layers. A moved part vacates pixels the flat render never had, so
        # each one gets its own silhouette painted in the surrounding flat colour,
        # clipped to the body that remains (a small reach keeps it off the
        # background).
        self.supports = []
        self.pivots = {}
        body = ImageChops.subtract(source.getchannel("A"), assigned)
        for part in spec["parts"].values():
            if "support" in part:
                cx, cy, rx, ry, color = part["support"]
                self.supports.append(self.support_layer(body, cx, cy, rx, ry, color))
        for name, glove in spec["gloves"].items():
            grown = masks[name].filter(ImageFilter.MaxFilter(5))
            self.supports.append(self.fill_layer(grown, body, glove["support"][4], reach=16))
            box = masks[name].getbbox()
            self.pivots[name] = self.point(((box[0] + box[2]) / 2, (box[1] + box[3]) / 2))
        self.pivots["head"] = self.point(spec["head"]["pivot"])
        for name, part in spec["parts"].items():
            self.pivots[name] = self.point(part["pivot"])

        self.heads = {style: self.mouth_head(style) for style in MOUTHS}
        x0, x1 = self.box[0] + width * 0.35, self.box[2] - width * 0.35
        self.feet_center = (x0 + x1) / 2
        self.feet_half = spec["feet_half"]

    # -- masks ------------------------------------------------------------
    def polygon(self, points):
        mask = Image.new("L", self.source.size, 0)
        ImageDraw.Draw(mask).polygon(points, fill=255)
        return ImageChops.multiply(mask, self.source.getchannel("A"))

    def head_mask(self, spec):
        """The head's real silhouette, not a hand-drawn polygon: the non-garment
        blob around the face, grown into the garment so a tilt cannot cut a
        crescent at the cheek (the seam lands on same-coloured cloth)."""
        alpha = self.source.getchannel("A")
        x0, y0, x1, y1 = spec["region"]
        width, height = x1 - x0, y1 - y0
        keep = Image.new("L", (width, height), 0)
        keep_pixels = keep.load()
        alpha_pixels = alpha.crop(spec["region"]).load()
        colors = self.source.load()
        for y in range(height):
            for x in range(width):
                if alpha_pixels[x, y] < 128:
                    continue
                r, g, _b, _a = colors[x + x0, y + y0]
                if r - g <= 90:  # not garment red, not garment pink: tune per character
                    keep_pixels[x, y] = 255
        seed = (spec["seed"][0] - x0, spec["seed"][1] - y0)
        if keep.getpixel(seed) == 0:
            raise RuntimeError("The head seed landed on the garment, not the face")
        ImageDraw.floodfill(keep, seed, 128)
        blob = keep.point(lambda value: 255 if value == 128 else 0)
        blob = blob.filter(ImageFilter.MaxFilter(spec.get("grow", 8) * 2 + 1))
        full = Image.new("L", self.source.size, 0)
        full.paste(blob, (x0, y0))
        return ImageChops.multiply(full, alpha)

    def support_layer(self, body, cx, cy, rx, ry, color, reach=30):
        mask = Image.new("L", self.source.size, 0)
        ImageDraw.Draw(mask).ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=255)
        return self.fill_layer(mask, body, color, reach)

    def fill_layer(self, mask, body, color, reach):
        width, height = self.source.size
        box = mask.getbbox()
        if box is None:
            return Image.new("RGBA", (OUT, OUT))
        box = (max(0, box[0] - reach), max(0, box[1] - reach),
               min(width, box[2] + reach), min(height, box[3] + reach))
        grown = body.crop(box)
        for _ in range(reach):
            grown = grown.filter(ImageFilter.MaxFilter(3))
        region = Image.new("L", self.source.size, 0)
        region.paste(grown, box[:2])
        art = Image.new("RGBA", self.source.size, color + (255,))
        art.putalpha(ImageChops.multiply(mask, region))
        return self.fit(art)

    def tinted(self, source, mask):
        art = source.copy()
        art.putalpha(mask)
        return art

    # -- geometry ---------------------------------------------------------
    def raw_point(self, point):
        return (self.offset[0] + (point[0] - self.box[0]) * self.scale_x,
                self.offset[1] + (point[1] - self.box[1]) * self.scale_y)

    def point(self, point):
        x, y = self.raw_point(point)
        return (x * SS, y * SS)

    def fit(self, image):
        resized = image.crop(self.box).resize((self.size[0] * SS, self.size[1] * SS),
                                              Image.Resampling.LANCZOS)
        result = Image.new("RGBA", (OUT, OUT))
        result.alpha_composite(resized, (self.offset[0] * SS, self.offset[1] * SS))
        return result

    # -- mouth ------------------------------------------------------------
    def mouth_head(self, style):
        """Erase the measured mouth strokes, redraw at their centre. Sizes are in
        canvas pixels and converted per source, so one set of numbers reads the
        same on a differently scaled render."""
        head = self.tinted(self.source, self.masks["head"])
        if MOUTHS[style] is None:
            return self.fit(head)
        image = head.copy()
        draw = ImageDraw.Draw(image)
        x0, y0, x1, y1 = self.spec["mouth"]["erase"]
        draw.rounded_rectangle((x0, y0, x1, y1), radius=6,
                               fill=self.spec["mouth"]["skin"] + (255,))
        cx, cy = self.spec["mouth"]["center"]
        ux, uy = self.unit_x, self.unit_y
        ink = (70, 40, 34, 255)
        if style == "grin":
            half, height = MOUTHS["grin"][0], MOUTHS["grin"][1]
            draw.arc((cx - half * ux, cy - height * uy, cx + half * ux,
                      cy + height * uy * 1.5), start=25, end=155, fill=ink,
                     width=max(2, round(0.55 * ux)))
        else:
            rx, ry, tongue = MOUTHS[style]
            rx, ry = round(rx * ux), round(ry * uy)
            draw.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=ink)
            draw.ellipse((cx - rx + round(0.5 * ux), cy - ry + round(0.2 * uy),
                          cx + rx - round(0.5 * ux), cy - ry + round(ry * 0.8)),
                         fill=(252, 248, 236, 255))
            if tongue:
                draw.ellipse((cx - round(rx * 0.55), cy + round(ry * 0.25),
                              cx + round(rx * 0.55), cy + ry), fill=(178, 86, 92, 255))
        return self.fit(image)

    # -- composition ------------------------------------------------------
    def frame(self, pose, extras):
        image = Image.new("RGBA", (OUT, OUT))
        image.alpha_composite(self.root)
        for support in self.supports:
            image.alpha_composite(support)

        for name in ("glove_left", "glove_right", "flex_arm"):
            if name not in self.parts:
                continue
            part = self.parts[name]
            dx = round(pose.get(name + "_dx", 0) * SS)
            dy = round(pose.get(name + "_dy", 0) * SS)
            if dx or dy:
                moved = Image.new("RGBA", (OUT, OUT))
                moved.alpha_composite(part, (dx, dy))
                part = moved
            angle = pose.get(name + "_rot", 0)
            if angle:
                part = part.rotate(angle, resample=Image.Resampling.BICUBIC,
                                   center=self.pivots[name])
            image.alpha_composite(part)

        head = self.heads[pose.get("mouth", "closed")]
        angle = pose.get("head_rot", 0)
        if angle:
            head = head.rotate(angle, resample=Image.Resampling.BICUBIC,
                               center=self.pivots["head"])
        image.alpha_composite(head)

        torso = pose.get("torso", 0)
        if torso:
            side = pose.get("pivot_side", 0)
            pivot = self.point((self.feet_center + side * self.feet_half, self.box[3] - 6))
            image = image.rotate(torso, resample=Image.Resampling.BICUBIC, center=pivot)

        if extras.get("bag") is not None:
            draw_bag(image, extras["bag"])
        if extras.get("puff") is not None:
            draw_puff(image, extras["puff"])

        image = image.resize((CANVAS, CANVAS), Image.Resampling.LANCZOS)
        return image.quantize(colors=96, method=Image.Quantize.FASTOCTREE).convert("RGBA")


def draw_bag(image, at):
    """A snack bag in supersampled canvas pixels (crimped top, puff print)."""
    draw = ImageDraw.Draw(image)
    x, y = at[0] * SS, at[1] * SS
    w, h = 15 * SS, 18 * SS
    draw.rounded_rectangle([x - w // 2, y - h // 2, x + w // 2, y + h // 2],
                           radius=2 * SS, fill=(233, 128, 38, 255),
                           outline=(146, 68, 18, 255), width=SS)
    draw.rectangle([x - w // 2 + SS, y - h // 2, x + w // 2 - SS, y - h // 2 + 2 * SS],
                   fill=(252, 196, 96, 255))
    draw.rectangle([x - w // 2 + 2 * SS, y - 2 * SS, x + w // 2 - 2 * SS, y + 2 * SS],
                   fill=(250, 240, 214, 255))
    for offset, size in ((-3, 3), (1, 4), (3, 3)):
        draw.ellipse([x + (offset - size // 2) * SS, y + (4 - size // 2) * SS,
                      x + (offset + size // 2) * SS, y + (4 + size // 2) * SS],
                     fill=(247, 196, 62, 255))


def draw_puff(image, at):
    draw = ImageDraw.Draw(image)
    x, y = at[0] * SS, at[1] * SS
    draw.ellipse([x - 2 * SS, y - 2 * SS, x + 2 * SS, y + 2 * SS], fill=(246, 186, 48, 255))
    draw.ellipse([x - SS, y - 2 * SS, x + SS, y], fill=(255, 231, 140, 255))


def timeline(keys, last):
    """Smoothstep interpolation over {frame: {channel: value}} keyframes."""
    out = []
    for frame in range(last + 1):
        before = max((k for k in keys if k[0] <= frame), key=lambda k: k[0])
        after = min((k for k in keys if k[0] >= frame), key=lambda k: k[0])
        t = 0 if after[0] == before[0] else (frame - before[0]) / (after[0] - before[0])
        eased = t * t * (3 - 2 * t)
        channels = set(before[1]) | set(after[1])
        out.append({channel: before[1].get(channel, 0)
                    + (after[1].get(channel, 0) - before[1].get(channel, 0)) * eased
                    for channel in channels})
    return out


# Rotation in place carries the expression; travel stays within a few pixels
# because a bigger slide tears the silhouette open.
ACTIONS = {
    # One-shot, no role: the format puts it in the idle pool by itself.
    "snack": {
        "frames": 18,
        "fps": 8,
        "keys": [
            (0, {}),
            (3, {"torso": -1, "head_rot": 4, "glove_right_dx": -1, "glove_right_dy": -1}),
            (6, {"torso": -2, "head_rot": 5, "glove_left_dy": -1, "glove_right_dx": -2,
                 "glove_right_dy": -2, "drop": -1}),
            (9, {"torso": -1, "head_rot": 4, "glove_right_dx": -2, "glove_right_dy": -1}),
            (12, {"head_rot": 2, "glove_right_dx": -1}),
            (14, {"torso": 1, "head_rot": -3, "glove_left_dy": -1, "glove_left_rot": -6,
                  "drop": -1}),
            (16, {}),
            (17, {}),
        ],
        "mouths": {0: "closed", 1: "closed", 2: "closed", 3: "closed", 4: "open",
                   5: "open", 6: "open", 7: "chew", 8: "open", 9: "open", 10: "open",
                   11: "chew", 12: "open", 13: "chew", 14: "grin", 15: "grin",
                   16: "closed", 17: "closed"},
        "extras": {5: {"puff": (22, 55)}, 6: {"puff": (29, 57)}, 7: {"puff": (37, 60)},
                   9: {"puff": (23, 56)}, 10: {"puff": (30, 58)}, 11: {"puff": (38, 60)}},
        "bag_frames": range(1, 14),
    },
    # Replaces a role's existing action instead of adding a second idle one.
    "celebrate": {
        "frames": 24,
        "fps": 8,
        "keys": [
            (0, {}),
            (3, {"torso": 5, "pivot_side": -1, "head_rot": -3, "glove_left_dx": 1,
                 "glove_left_dy": -1, "glove_left_rot": -16, "glove_right_dx": -2,
                 "glove_right_dy": -3, "glove_right_rot": 20, "drop": -1}),
            (6, {"pivot_side": 1, "head_rot": 1, "glove_left_rot": -4,
                 "glove_right_dy": -1, "glove_right_rot": 6}),
            (9, {"torso": -5, "pivot_side": 1, "head_rot": 3, "glove_left_dx": -1,
                 "glove_left_dy": -1, "glove_left_rot": 14, "glove_right_dx": 1,
                 "glove_right_rot": -8, "drop": -1}),
            (12, {"pivot_side": -1, "glove_left_rot": -3, "glove_right_dy": -1,
                  "glove_right_rot": 4}),
            (15, {"torso": 5, "pivot_side": -1, "head_rot": -3, "glove_left_dx": 1,
                  "glove_left_dy": -1, "glove_left_rot": -16, "glove_right_dx": -3,
                  "glove_right_dy": -4, "glove_right_rot": 22, "drop": -1}),
            (18, {"glove_left_dy": -1, "glove_left_rot": -8, "glove_right_dx": -3,
                  "glove_right_dy": -4, "glove_right_rot": 24}),
            (20, {"head_rot": 2, "glove_left_rot": -6, "glove_right_dx": -2,
                  "glove_right_dy": -3, "glove_right_rot": 20, "drop": -1}),
            (23, {}),
        ],
        "mouths": {0: "closed", 1: "closed", 2: "closed",
                   **{frame: "grin" for frame in range(3, 22)},
                   22: "closed", 23: "closed"},
        "extras": {},
        "bag_frames": (),
    },
}


def render(figure, spec):
    poses = timeline(spec["keys"], spec["frames"] - 1)
    frames, anchors = [], []
    for index, pose in enumerate(poses):
        drop = round(pose.pop("drop", 0))
        pose["mouth"] = spec["mouths"].get(index, "closed")
        extras = dict(spec["extras"].get(index, {}))
        if index in spec["bag_frames"]:
            extras["bag"] = (figure.spec["bag_at"][0] + round(pose.get("glove_left_dx", 0)),
                             figure.spec["bag_at"][1] + round(pose.get("glove_left_dy", 0)))
        frames.append(figure.frame(pose, extras))
        anchors.append({"head": [0, drop], "face": [0, drop], "body": [0, drop]})
    return frames, anchors


def montage(frames, path):
    """Every frame on magenta: a hole inside the body screams, grey hides it."""
    scale, columns, label = 6, 6, 20
    size = CANVAS * scale
    rows = (len(frames) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * size, rows * (size + label)), "#303030")
    draw = ImageDraw.Draw(sheet)
    for index, frame in enumerate(frames):
        background = Image.new("RGBA", frame.size, (255, 0, 255, 255))
        background.alpha_composite(frame)
        x, y = (index % columns) * size, (index // columns) * (size + label)
        draw.text((x + 3, y + 3), str(index), fill="white")
        sheet.paste(background.convert("RGB").resize((size, size), Image.NEAREST),
                    (x, y + label))
    sheet.save(path)


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    figure = Figure(SPEC)
    for name, spec in ACTIONS.items():
        frames, anchors = render(figure, spec)
        for index, frame in enumerate(frames):
            bbox = frame.getchannel("A").point(lambda value: 255 if value > 32 else 0).getbbox()
            if bbox is None or bbox[0] < 0 or bbox[1] < 0 or bbox[2] > CANVAS or bbox[3] > CANVAS:
                raise RuntimeError(f"{name} frame {index} leaves the canvas: {bbox}")
        sheet = Image.new("RGBA", (CANVAS * len(frames), CANVAS))
        for index, frame in enumerate(frames):
            sheet.alpha_composite(frame, (index * CANVAS, 0))
        sheet.save(DEST / f"{name}.png")
        (DEST / f"{name}-anchors.json").write_text(json.dumps(anchors, indent=2),
                                                   encoding="utf-8")
        montage(frames, DEST / f"{name}-check.png")
        print(f"{name}: {len(frames)} frames @{spec['fps']}fps -> {DEST / (name + '.png')}")


if __name__ == "__main__":
    main()
