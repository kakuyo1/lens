"""Check the dog's sprite sheets against their frame counts, and compare a fresh render with data/pet/dog/sprites.

    python -I scripts/pet/sync_sprites.py

The generator (scripts/pet/dog/build-assets.py) writes straight into data/pet/dog/sprites, so this script renders
into a scratch folder and only compares. It changes nothing in the repository. Every PNG the app reads has a fixed
size: a body or effect sheet is frames x 88 wide and 88 tall, the expression sheet is a row of 88 x 88 cells, and
each accessory is 88 x 88. A size error is reported here, before the app refuses the pet.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
DOG = ROOT / "data" / "pet" / "dog"
SPRITES = DOG / "sprites"
BUILD = ROOT / "scripts" / "pet" / "dog" / "build-assets.py"
CANVAS = 88


def check_sizes(animations, pet, accessories):
    problems = []
    for name, action in animations["actions"].items():
        for key in ("sheet", "effect"):
            if key not in action:
                continue
            width, height = Image.open(DOG / action[key]).size
            want = action["frames"] * CANVAS
            if (width, height) != (want, CANVAS):
                problems.append(f"{action[key]}: {width}x{height}, want {want}x{CANVAS} for {name}")
    width, height = Image.open(DOG / pet["expression"]).size
    if height != CANVAS or width % CANVAS:
        problems.append(f"{pet['expression']}: {width}x{height} is not a row of {CANVAS}x{CANVAS} cells")
    for acc in accessories:
        size = Image.open(DOG / acc["asset"]).size
        if size != (CANVAS, CANVAS):
            problems.append(f"{acc['asset']}: {size[0]}x{size[1]}, want {CANVAS}x{CANVAS}")
    return problems


def main():
    animations = json.loads((DOG / "animations.json").read_text(encoding="utf-8"))
    pet = json.loads((DOG / "pet.json").read_text(encoding="utf-8"))
    accessories = json.loads((DOG / "accessories.json").read_text(encoding="utf-8"))["accessories"]
    problems = check_sizes(animations, pet, accessories)
    for problem in problems:
        print("SIZE", problem)
    if problems:
        raise SystemExit("sizes do not match the frame counts; fix the sprites or animations.json")

    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        subprocess.run([sys.executable, "-I", str(BUILD), "--out", str(scratch)], check=True)
        renders = sorted(p.relative_to(scratch) for p in scratch.rglob("*.png"))
        differ = []
        for rel in renders:
            target = SPRITES / rel
            if not target.exists():
                status = "MISSING"
            elif target.read_bytes() == (scratch / rel).read_bytes():
                status = "same"
            else:
                status = "DIFFERS"
            if status != "same":
                differ.append(rel)
            print(f"{status:8} {rel.as_posix()}")
        extra = sorted(p.relative_to(SPRITES) for p in SPRITES.rglob("*.png") if p.relative_to(SPRITES) not in renders)
        for rel in extra:
            print(f"EXTRA    {rel.as_posix()} (not produced by the generator)")

    print(f"\n{len(differ)} of {len(renders)} differ from data/pet/dog/sprites, {len(extra)} extra")
    if differ or extra:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
