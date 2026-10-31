# /// script
# @brief Build the landing asset set from data/pet: copy sheets, slice signs,
#        and composite the expression + effect + one accessory layer per frame
#        the same way the app does (body 0 / expression 2 / effect 6).
#        Run from the repo root: python -I marketing/tools/prep-assets.py
# ///
import json
import shutil
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PET_SRC = REPO / "data" / "pet"
OUT = REPO / "marketing" / "assets"

PETS = ["cat", "cloud", "dog", "ghost", "jelly", "mochi", "owl"]
# Only the sheets the page actually plays, per pet. Everything else is one
# `render_pet.py` run away if a future build needs it.
USED = {
    "cat": ["idle", "pounce", "walk"],          # pounce lands into the catyarn scene
    "dog": ["idle", "walk", "look_around"],     # hero entrance ritual
    "owl": ["idle", "ask", "celebrate", "encourage", "walk"],
    "ghost": ["edge_peek", "walk"],
    "jelly": ["idle", "thinking", "walk"],      # thinking drives the sense swap
    "mochi": ["idle", "walk", "pickup", "dizzy"],  # fling: pickup in the air, dizzy on landing
    "cloud": ["idle", "walk"],
}
SIGN_WORD = "serendipity"  # 11 chars -> smallest bin that fits
SIGN_WORD_LEN = len(SIGN_WORD)

# One accessory per pet, head or body slot only (no glasses, user request).
ACCESSORY = {
    "cat": "bell_collar",
    "cloud": "star_beanie",
    "dog": "bandana",
    "ghost": "witch_hat",
    "jelly": "starfish_clip",
    "mochi": "kanzashi",
    "owl": "mortarboard",
}

CELL = 88

meta = {"canvas": CELL, "scale": 3, "pets": {}, "signs": {}, "worn": ACCESSORY}


def paste(base, layer, xy):
    """Alpha-over `layer` onto `base` at integer offset xy."""
    base.alpha_composite(layer, (int(xy[0]), int(xy[1])))


for pet in PETS:
    anims = json.loads((PET_SRC / pet / "animations.json").read_text(encoding="utf-8"))
    actions = anims["actions"]
    anchors = json.loads((PET_SRC / pet / "anchors.json").read_text(encoding="utf-8"))["actions"]
    acc_cfg = json.loads((PET_SRC / pet / "accessories.json").read_text(encoding="utf-8"))["accessories"]
    eyes = Image.open(PET_SRC / pet / "sprites" / "expressions" / "eyes.png").convert("RGBA")

    acc = next((a for a in acc_cfg if a["id"] == ACCESSORY[pet]), None)
    acc_img = None
    if acc:
        acc_img = Image.open(PET_SRC / pet / acc["asset"]).convert("RGBA")

    pet_out = OUT / "pets" / pet
    pet_out.mkdir(parents=True, exist_ok=True)
    pet_meta = {}
    for name in USED[pet]:
        if name not in actions:
            continue
        act = actions[name]
        frames = act.get("frames", 0)
        frames = len(frames) if isinstance(frames, list) else frames
        expr = act.get("expressions") or [0] * frames
        body = Image.open(PET_SRC / pet / act["sheet"]).convert("RGBA")
        effect = None
        if act.get("effect"):
            p = PET_SRC / pet / act["effect"]
            if p.exists():
                eff = Image.open(p).convert("RGBA")
                if eff.width == frames * CELL:  # only when cells align 1:1
                    effect = eff

        sheet = Image.new("RGBA", (frames * CELL, CELL), (0, 0, 0, 0))
        for i in range(frames):
            cell = body.crop((i * CELL, 0, (i + 1) * CELL, CELL))
            if i < len(expr):
                e = expr[i] % (eyes.width // CELL)
                paste(cell, eyes.crop((e * CELL, 0, (e + 1) * CELL, CELL)), (0, 0))
            if effect is not None:
                paste(cell, effect.crop((i * CELL, 0, (i + 1) * CELL, CELL)), (0, 0))
            sheet.paste(cell, (i * CELL, 0))
        sheet.save(pet_out / f"{name}.png")
        pet_meta[name] = {"frames": frames, "fps": act.get("fps"),
                          "expr": bool(expr), "effect": effect is not None}
    # cat_yarn: the scene's prop sheets align 1:1 with the body frames
    if pet == "cat" and "cat_yarn" in actions:
        cy = actions["cat_yarn"]
        n = cy.get("frames", 0)
        n = len(n) if isinstance(n, list) else n
        body = Image.open(PET_SRC / pet / cy["sheet"]).convert("RGBA")
        back = Image.open(REPO / "data" / "scenes" / "cat_yarn" / "back.png").convert("RGBA")
        front = Image.open(REPO / "data" / "scenes" / "cat_yarn" / "front.png").convert("RGBA")
        cy_expr = cy.get("expressions") or [0] * n
        comp = Image.new("RGBA", (n * CELL, CELL), (0, 0, 0, 0))
        for i in range(n):
            cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
            if back.width >= (i + 1) * CELL:
                paste(cell, back.crop((i * CELL, 0, (i + 1) * CELL, CELL)), (0, 0))
            paste(cell, body.crop((i * CELL, 0, (i + 1) * CELL, CELL)), (0, 0))
            if i < len(cy_expr):
                e = cy_expr[i] % (eyes.width // CELL)
                paste(cell, eyes.crop((e * CELL, 0, (e + 1) * CELL, CELL)), (0, 0))
            if front.width >= (i + 1) * CELL:
                paste(cell, front.crop((i * CELL, 0, (i + 1) * CELL, CELL)), (0, 0))
            comp.paste(cell, (i * CELL, 0))
        comp.save(pet_out / "catyarn.png")
        pet_meta["catyarn"] = {"frames": n, "fps": cy.get("fps")}

    if acc_img is not None:
        # accessory stays a static overlay in the page: pet animates, acc does not
        acc_img.save(pet_out / "acc.png")
        pet_meta["acc"] = {"id": acc["id"], "slot": acc["slot"]}
    meta["pets"][pet] = pet_meta

    # --- sign: smallest bin that fits the demo word, slice the fully raised frame
    sign_cfg = json.loads((PET_SRC / pet / "sign.json").read_text(encoding="utf-8"))
    bin_ = next(b for b in sign_cfg["bins"] if b["width"] >= SIGN_WORD_LEN)
    img = Image.open(PET_SRC / pet / bin_["sheet"]).convert("RGBA")
    cw, ch = bin_["cellWidth"], bin_["cellHeight"]
    n = img.width // cw
    raised = img.crop(((n - 1) * cw, 0, n * cw, ch))
    signs_out = OUT / "signs"
    signs_out.mkdir(parents=True, exist_ok=True)
    raised.save(signs_out / f"{pet}.png")
    meta["signs"][pet] = {
        "w": cw, "h": ch,
        "wordTop": sign_cfg["word"]["top"],
        "wordColor": sign_cfg["word"]["color"],
        "design": sign_cfg["design"],
    }

# crane demo photos: the sense-pictures prototype's own set, cropped 4:3
pic_src = REPO / "ui-prototypes" / "assets" / "word-images"
pic_out = OUT / "pictures"
pic_out.mkdir(parents=True, exist_ok=True)
for name in ["crane-bird-1", "crane-bird-2", "crane-bird-3",
             "crane-machine-a", "crane-machine-b", "crane-machine-c"]:
    im = Image.open(pic_src / f"{name}.jpg").convert("RGB")
    w, hh = im.size
    if w / hh > 4 / 3:
        nw = int(hh * 4 / 3); x = (w - nw) // 2; im = im.crop((x, 0, x + nw, hh))
    else:
        nh = int(w * 3 / 4); y = (hh - nh) // 2; im = im.crop((0, y, w, y + nh))
    im.resize((360, 270), Image.LANCZOS).save(pic_out / f"{name}.jpg", quality=86)

fonts_out = OUT / "fonts"
fonts_out.mkdir(parents=True, exist_ok=True)
shutil.copyfile(REPO / "data" / "fonts" / "PixelifySans-Regular.ttf", fonts_out / "PixelifySans-Regular.ttf")
shutil.copyfile(REPO / "data" / "fonts" / "OFL.txt", fonts_out / "OFL.txt")
brand_out = OUT / "brand"
brand_out.mkdir(parents=True, exist_ok=True)
for f in ["lens-mark.svg", "lens-mark.png"]:
    shutil.copyfile(REPO / "docs" / "readme" / "brand" / f, brand_out / f)

(OUT / "meta.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
worn = {p: meta["pets"][p] for p in PETS}
print(json.dumps({p: {k: v for k, v in d.items()} for p, d in worn.items()}, ensure_ascii=False)[:900])
print("done ->", OUT)
