# -*- coding: utf-8 -*-
"""Preview compositor for a Lens pet package: draws frames exactly like the app
(body layer at anchors.body, expression layer at anchors.face, effect at the
origin), so what you see is what the pet will look like.

Usage:
    python -I preview.py --idle <pkg> <out.png>     # idle frame 1, scaled x3
    python -I preview.py --sheet <pkg> <out.png>    # all actions + faces + accessories
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SCALE = 3


def load(pkg):
    pkg = Path(pkg)
    return pkg, {stem: json.loads((pkg / f"{stem}.json").read_text(encoding="utf-8"))
                 for stem in ("pet", "animations", "anchors", "accessories")}


def canvas_of(pkg, docs):
    return Image.new("RGBA", (docs["pet"]["canvas"],) * 2, (0, 0, 0, 0))


def cell(sheet, index, canvas):
    return sheet.crop((index * canvas, 0, (index + 1) * canvas, canvas))


def frame_image(pkg, docs, action, i):
    canvas = docs["pet"]["canvas"]
    spec = docs["animations"]["actions"][action]
    a = docs["anchors"]["actions"][action][i]
    img = canvas_of(pkg, docs)
    body = Image.open(pkg / spec["sheet"]).convert("RGBA")
    img.alpha_composite(cell(body, i, canvas), tuple(a["body"]))
    expr_indices = spec.get("expressions") or [0]
    idx = expr_indices[i] if i < len(expr_indices) else 0
    expr = Image.open(pkg / docs["pet"]["expression"]).convert("RGBA")
    img.alpha_composite(cell(expr, idx, canvas), tuple(a["face"]))
    if "effect" in spec:
        fx = Image.open(pkg / spec["effect"]).convert("RGBA")
        img.alpha_composite(cell(fx, i, canvas), (0, 0))
    return img, a


def worn(pkg, docs, img, a, acc_ids):
    for acc in docs["accessories"]["accessories"]:
        if acc["id"] not in acc_ids:
            continue
        anchor = a[{"head": "head", "face": "face", "body": "body"}[acc["slot"]]]
        art = Image.open(pkg / acc["asset"]).convert("RGBA")
        img.alpha_composite(art, tuple(anchor))
    return img


def scaled(img, factor):
    return img.resize((img.width * factor, img.height * factor), Image.NEAREST)


def idle_preview(pkg_path, out_path):
    pkg, docs = load(pkg_path)
    anim = docs["animations"]
    idle = anim["roles"]["idle"]
    img, _ = frame_image(pkg, docs, idle, 0)
    worn(pkg, docs, img, docs["anchors"]["actions"][idle][0], ["top_bone"])
    scaled(img, SCALE).save(out_path)
    print("wrote", out_path)


def contact_sheet(pkg_path, out_path):
    pkg, docs = load(pkg_path)
    canvas = docs["pet"]["canvas"]
    anim = docs["animations"]
    idle = anim["roles"]["idle"]
    rows = []
    for action in anim["actions"]:
        if action == "blink":
            continue
        frames = []
        for i in range(anim["actions"][action]["frames"]):
            img, _ = frame_image(pkg, docs, action, i)
            frames.append(img)
        rows.append((action, frames))
    # faces row: every expression cell on the idle body
    expr_sheet = Image.open(pkg / docs["pet"]["expression"]).convert("RGBA")
    base, a0 = frame_image(pkg, docs, idle, 0)
    face_rows = []
    for i in range(expr_sheet.width // canvas):
        img = base.copy()
        img.alpha_composite(cell(expr_sheet, i, canvas), tuple(a0["face"]))
        face_rows.append(img)
    rows.insert(1, ("faces", face_rows))
    # accessories row
    acc_row = []
    for acc in docs["accessories"]["accessories"]:
        img, a = frame_image(pkg, docs, idle, 0)
        img.alpha_composite(Image.open(pkg / acc["asset"]).convert("RGBA"),
                            tuple(a[{"head": "head", "face": "face", "body": "body"}[acc["slot"]]]))
        acc_row.append(img)
    rows.append(("accessories", acc_row))

    pad = 2
    width = max(sum(f.width for f in frames) + pad * len(frames) for _, frames in rows)
    height = sum(frames[0].height + pad for _, frames in rows)
    sheet = Image.new("RGBA", (width, height), (128, 128, 128, 255))
    y = 0
    for _name, frames in rows:
        x = 0
        for f in frames:
            sheet.alpha_composite(f, (x, y))
            x += f.width + pad
        y += frames[0].height + pad
    sheet = scaled(sheet, 2)
    sheet.save(out_path)
    print("wrote", out_path, sheet.size)


if __name__ == "__main__":
    mode, pkg_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    (idle_preview if mode == "--idle" else contact_sheet)(pkg_path, out_path)
