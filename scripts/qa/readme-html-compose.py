#!/usr/bin/env python
"""Crop, join and label the README's HTML marketing stills.

Run after readme-html-shots.sh. This script is deliberately outside the product build: it reads
PNG frames from the scratch directory, writes the final stills and the language-switch GIF under
docs/readme/, and needs Pillow. It never opens Lens or touches the desktop.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont
except ImportError:  # pragma: no cover - the message is the useful part
    sys.exit("readme-html-compose: Pillow is required (pip install Pillow)")

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "readme"
FRAMES = Path(os.environ.get("LENS_README_FRAMES") or (Path(tempfile.gettempdir()) / "lens-readme-gif"))


def autocrop(path: Path, background: tuple[int, int, int] = (233, 235, 239), margin: int = 64) -> Image.Image:
    """Trim the flat README page colour around one surface, keeping its shadow and a margin."""
    image = Image.open(path).convert("RGB")
    difference = ImageChops.difference(image, Image.new("RGB", image.size, background))
    box = difference.getbbox()
    if box is None:
        return image
    left, top, right, bottom = box
    return image.crop(
        (
            max(0, left - margin),
            max(0, top - margin),
            min(image.width, right + margin),
            min(image.height, bottom + margin),
        )
    )


def compose_language_gif() -> None:
    # The frames already carry the shared reading page; the bubble is the only thing that moves.
    frames = []
    for name in ("fb-page-en", "fb-page-menu", "fb-page-zh", "fb-page-en"):
        frame = Image.open(FRAMES / f"{name}.png").convert("RGB").resize((760, 760), Image.Resampling.LANCZOS)
        frames.append(frame.quantize(colors=128, method=Image.MEDIANCUT))
    frames[0].save(
        OUT / "english-first.gif",
        save_all=True,
        append_images=frames[1:],
        duration=[1400, 1000, 1800, 1400],
        loop=0,
        optimize=True,
        disposal=2,
    )


def compose_selection_styles() -> None:
    """Five action-bar shapes over one page, one frame each."""
    frames = []
    for style in ("classic", "side", "cards", "tag", "fan"):
        frame = Image.open(FRAMES / f"sel-style-{style}.png").convert("RGB").resize((900, 620), Image.Resampling.LANCZOS)
        frames.append(frame.quantize(colors=128, method=Image.MEDIANCUT))
    frames[0].save(
        OUT / "selection-styles.gif",
        save_all=True,
        append_images=frames[1:],
        duration=1300,
        loop=0,
        optimize=True,
        disposal=2,
    )


def compose_themes() -> None:
    themes = [
        ("Light", "bubble-page-light.png"),
        ("Dark", "bubble-page-dark.png"),
        ("Frosted glass", "bubble-page-frosted.png"),
        ("Liquid glass", "bubble-page-liquid.png"),
    ]
    cell_width, cell_height, label_height = 900, 700, 46
    gap, margin = 26, 40
    width = margin * 2 + cell_width * 2 + gap
    height = margin * 2 + (cell_height + label_height) * 2 + gap
    sheet = Image.new("RGB", (width, height), (238, 241, 244))
    draw = ImageDraw.Draw(sheet)
    font_path = Path(os.environ.get("WINDIR") or os.environ.get("SystemRoot") or "") / "Fonts" / "seguisb.ttf"
    font = ImageFont.truetype(str(font_path), 20) if font_path.exists() else ImageFont.load_default()
    for index, (label, filename) in enumerate(themes):
        column, row = index % 2, index // 2
        x = margin + column * (cell_width + gap)
        y = margin + row * (cell_height + label_height + gap)
        image = Image.open(OUT / filename).convert("RGB").resize(
            (cell_width, cell_height), Image.Resampling.LANCZOS
        )
        sheet.paste(image, (x, y))
        draw.text((x + 2, y + cell_height + 12), label, font=font, fill=(26, 26, 31))
    sheet.save(OUT / "themes.png", optimize=True)


FONT_DIR = Path(os.environ.get("WINDIR") or os.environ.get("SystemRoot") or "") / "Fonts"


def font(name: str, size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    path = FONT_DIR / name
    return ImageFont.truetype(str(path), size) if path.exists() else ImageFont.load_default()


def gradient(size: tuple[int, int], top: tuple[int, int, int], bottom: tuple[int, int, int]) -> Image.Image:
    width, height = size
    strip = Image.new("RGB", (1, height))
    for y in range(height):
        t = y / max(1, height - 1)
        strip.putpixel((0, y), tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return strip.resize((width, height), Image.Resampling.BILINEAR).convert("RGBA")


def add_card_shadow(canvas: Image.Image, box: tuple[int, int, int, int]) -> None:
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    x0, y0, x1, y1 = box
    draw.rounded_rectangle((x0, y0 + 18, x1, y1 + 18), radius=28, fill=(18, 22, 30, 36))
    canvas.alpha_composite(layer.filter(ImageFilter.GaussianBlur(26)))


def compose_learning_loop() -> None:
    """Two framed cards: the mochi review sign on the left, the year graph on the right."""
    canvas = gradient((1600, 960), (247, 248, 250), (233, 237, 242))
    draw = ImageDraw.Draw(canvas)
    label_font = font("seguisb.ttf", 30)
    caption_font = font("segoeui.ttf", 18)
    small_font = font("segoeui.ttf", 15)
    small_bold = font("seguisb.ttf", 15)

    # Equal cards, equal insets, and the same vertical rhythm on both sides: label, media,
    # caption, subcaption, then the same bottom padding.
    for box in ((60, 80, 780, 880), (820, 80, 1540, 880)):
        add_card_shadow(canvas, box)
        draw.rounded_rectangle(box, radius=28, fill=(255, 255, 255, 255), outline=(226, 230, 236, 255), width=2)

    draw.rounded_rectangle((120, 210, 720, 710), radius=22, fill=(246, 242, 234, 255))
    draw.rounded_rectangle((880, 210, 1480, 710), radius=22, fill=(246, 242, 234, 255))

    review = Image.open(OUT / "review.png").convert("RGB").resize((580, 267), Image.Resampling.LANCZOS)
    year = Image.open(OUT / "year-in-words.png").convert("RGB").resize((490, 479), Image.Resampling.LANCZOS)
    canvas.alpha_composite(review.convert("RGBA"), (130, 225))
    canvas.alpha_composite(year.convert("RGBA"), (935, 220))

    draw.text((120, 120), "A word comes back", font=label_font, fill=(26, 26, 31, 255))
    draw.text((880, 120), "A year of reading", font=label_font, fill=(26, 26, 31, 255))
    draw.rectangle((120, 164, 270, 168), fill=(47, 158, 110, 255))
    draw.rectangle((880, 164, 1030, 168), fill=(47, 158, 110, 255))

    # The left media panel has a wide review scene, so the space under it carries the schedule.
    draw.text((150, 522), "Spaced review", font=small_bold, fill=(102, 102, 111, 255))
    draw.line((155, 566, 685, 566), fill=(206, 211, 219, 255), width=2)
    for index, label in enumerate(("10m", "1d", "3d", "7d", "21d", "60d")):
        x = 155 + index * 106
        draw.ellipse((x - 5, 561, x + 5, 571), fill=(47, 158, 110, 255))
        text_width = draw.textbbox((0, 0), label, font=small_font)[2]
        draw.text((x - text_width / 2, 586), label, font=small_font, fill=(132, 135, 143, 255))
    draw.text((150, 648), "No separate review screen.", font=small_font, fill=(132, 135, 143, 255))

    draw.text((120, 750), "The pet holds up a due word; answer Forgot or Remembered.", font=caption_font, fill=(102, 102, 111, 255))
    draw.text((120, 785), "It comes back on a schedule from 10 minutes to 60 days.", font=small_font, fill=(132, 135, 143, 255))
    draw.text((880, 750), "Every lookup leaves one green square.", font=caption_font, fill=(102, 102, 111, 255))
    draw.text((880, 785), "A year of reading, without a streak.", font=small_font, fill=(132, 135, 143, 255))

    canvas.convert("RGB").save(OUT / "learning-loop.png", optimize=True)


def compose_pets_gif() -> None:
    """Seven pets, each playing one of its own actions, on a shared strip."""
    pets = [
        ("cat", "Stub cat", "knead", "Knead"),
        ("owl", "Owl", "study", "Reading"),
        ("mochi", "Mochi", "bounce", "Bounce"),
        ("ghost", "Ghost", "float", "Float"),
        ("jelly", "Jellyfish", "bob", "Bob"),
        ("cloud", "Cloud", "drift", "Drift"),
        ("dog", "Dog", "wag", "Wag"),
    ]
    width, height = 1600, 440
    cell_width, gap, pet_top = 176, 32, 100
    margin = (width - (len(pets) * cell_width + (len(pets) - 1) * gap)) // 2
    label_font = font("seguisb.ttf", 20)
    action_font = font("segoeui.ttf", 13)

    prepared = []
    for pet, label, action, action_label in pets:
        directory = ROOT / "data" / "pet" / pet
        animations = json.loads((directory / "animations.json").read_text(encoding="utf-8"))
        definition = animations["actions"][action]
        anchors = json.loads((directory / "anchors.json").read_text(encoding="utf-8"))["actions"].get(action)
        if anchors is None:
            anchors = json.loads((directory / "anchors.json").read_text(encoding="utf-8"))["actions"]["idle"]
        body_sheet = Image.open(directory / definition["sheet"]).convert("RGBA")
        eyes_sheet = Image.open(directory / "sprites" / "expressions" / "eyes.png").convert("RGBA")
        effect_sheet = None
        if definition.get("effect"):
            effect_sheet = Image.open(directory / definition["effect"]).convert("RGBA")
        prepared.append((label, action_label, definition, anchors, body_sheet, eyes_sheet, effect_sheet))

    frames = []
    for step in range(15):
        canvas = gradient((width, height), (247, 248, 250), (233, 237, 242))
        shadows = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        shadow_draw = ImageDraw.Draw(shadows)
        for index, _ in enumerate(prepared):
            x = margin + index * (cell_width + gap)
            shadow_draw.ellipse((x + 30, 284, x + 146, 302), fill=(20, 22, 28, 26))
        canvas.alpha_composite(shadows.filter(ImageFilter.GaussianBlur(4)))
        draw = ImageDraw.Draw(canvas)

        for index, (label, action_label, definition, anchors, body_sheet, eyes_sheet, effect_sheet) in enumerate(prepared):
            frame = int(step * definition["fps"] / 10) % definition["frames"]
            expressions = definition.get("expressions") or [0]
            eyes = expressions[frame % len(expressions)]
            body = body_sheet.crop((frame * 88, 0, (frame + 1) * 88, 88))
            eye = eyes_sheet.crop((eyes * 88, 0, (eyes + 1) * 88, 88))
            sprite = Image.alpha_composite(body, eye)
            if effect_sheet is not None:
                effect_frame = frame % (effect_sheet.width // 88)
                effect = effect_sheet.crop((effect_frame * 88, 0, (effect_frame + 1) * 88, 88))
                sprite = Image.alpha_composite(sprite, effect)
            sprite = sprite.resize((176, 176), Image.Resampling.NEAREST)
            dy = anchors[frame % len(anchors)]["body"][1] * 2
            x = margin + index * (cell_width + gap)
            canvas.alpha_composite(sprite, (x, pet_top + dy))
            name_width = draw.textbbox((0, 0), label, font=label_font)[2]
            draw.text((x + (cell_width - name_width) / 2, 320), label, font=label_font, fill=(26, 26, 31, 255))
            action_width = draw.textbbox((0, 0), action_label, font=action_font)[2]
            draw.text((x + (cell_width - action_width) / 2, 344), action_label, font=action_font, fill=(132, 135, 143, 255))

        frames.append(canvas.convert("RGB").quantize(colors=96, method=Image.MEDIANCUT, dither=Image.Dither.NONE))

    frames[0].save(
        OUT / "pets.gif",
        save_all=True,
        append_images=frames[1:],
        duration=100,
        loop=0,
        optimize=True,
        disposal=2,
    )


def main() -> None:
    if not FRAMES.is_dir():
        sys.exit(f"readme-html-compose: frames not found at {FRAMES}; run readme-html-shots.sh first")

    autocrop(OUT / "selection-bar.png", background=(255, 255, 255)).save(OUT / "selection-bar.png", optimize=True)
    autocrop(OUT / "year-in-words.png", background=(246, 242, 234)).save(OUT / "year-in-words.png", optimize=True)
    review_hover = OUT / "review-hover.png"
    if review_hover.exists():
        autocrop(review_hover, background=(246, 242, 234), margin=90).save(OUT / "review.png", optimize=True)
        review_hover.unlink()

    compose_language_gif()
    compose_selection_styles()
    compose_themes()
    compose_learning_loop()
    compose_pets_gif()

    for name in (
        "english-first.gif",
        "selection-styles.gif",
        "pets.gif",
        "themes.png",
        "learning-loop.png",
        "review.png",
        "selection-bar.png",
        "year-in-words.png",
    ):
        path = OUT / name
        print(f"  docs/readme/{name}: {path.stat().st_size / 1024:.0f} KB")


if __name__ == "__main__":
    main()
