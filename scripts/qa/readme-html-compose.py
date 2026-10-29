#!/usr/bin/env python
"""Crop, join and label the README's HTML marketing stills.

Run after readme-html-shots.sh. This script is deliberately outside the product build: it reads
PNG frames from the scratch directory, writes the final stills and the mother-tongue peek GIF under
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


def compose_peek_gif() -> None:
    """The English card, the globe held, the mother-tongue line, and back to English.

    The frames already carry the shared reading page; the card is the only thing that moves.
    """
    frames = []
    for name in ("fb-page-en", "fb-page-hold", "fb-page-peek", "fb-page-en"):
        frame = Image.open(FRAMES / f"{name}.png").convert("RGB").resize((760, 760), Image.Resampling.LANCZOS)
        frames.append(frame.quantize(colors=128, method=Image.MEDIANCUT))
    frames[0].save(
        OUT / "english-first.gif",
        save_all=True,
        append_images=frames[1:],
        duration=[1600, 700, 2200, 1200],
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


def cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Fill a cell from the centre: the review scene and the year card have their own aspects, and
    squeezing them into the collage's cells would fatten the pet."""
    target_w, target_h = size
    scale = max(target_w / image.width, target_h / image.height)
    resized = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
    left = (resized.width - target_w) // 2
    top = (resized.height - target_h) // 2
    return resized.crop((left, top, left + target_w, top + target_h))


def compose_learning_loop() -> None:
    """Two framed cards: the mochi review sign on the left, the year graph on the right."""
    canvas = gradient((1600, 960), (250, 247, 240), (235, 229, 219))
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

    draw.rounded_rectangle((120, 210, 720, 710), radius=22, fill=(244, 236, 217, 255))
    draw.rounded_rectangle((880, 210, 1480, 710), radius=22, fill=(244, 236, 217, 255))

    review = cover(Image.open(OUT / "review.png").convert("RGB"), (580, 267))
    year = cover(Image.open(OUT / "year-in-words.png").convert("RGB"), (490, 479))
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

    draw.text((120, 750), "The pet's own sign asks the word; answer Forgot or Remembered.", font=caption_font, fill=(102, 102, 111, 255))
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
        canvas = gradient((width, height), (250, 247, 240), (235, 229, 219))
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


def compose_lantern_gif() -> None:
    """The ghost's jack-o'-lantern scene, animated from the shipped sheets at the scene's own 4 fps.

    Every layer is a 12-frame row of 88 px cells -- the scene's back and front, the pet's body and
    the eyes -- so frame i composites cell i of each, at the scene's own dx/dy/scale for that frame.
    """
    pet, scene_id = "ghost", "ghost_lantern"
    data = ROOT / "data"
    catalog = json.loads((data / "scenes" / "scenes.json").read_text(encoding="utf-8"))
    scene = {entry["id"]: entry for entry in catalog["scenes"]}[scene_id]
    action = json.loads((data / "pet" / pet / "animations.json").read_text(encoding="utf-8"))["actions"][scene_id]
    eyes_sheet = Image.open(data / "pet" / pet / "sprites" / "expressions" / "eyes.png").convert("RGBA")
    back_sheet = Image.open(data / "scenes" / scene_id / "back.png").convert("RGBA")
    front_sheet = Image.open(data / "scenes" / scene_id / "front.png").convert("RGBA")
    body_sheet = Image.open(data / "pet" / pet / action["sheet"]).convert("RGBA")

    def cell_of(sheet: Image.Image, frame: int) -> Image.Image:
        return sheet.crop((frame * 88, 0, (frame + 1) * 88, 88))

    pad, label_h, scale = 44, 46, 4
    cell = 88 * scale
    width, height = cell + pad * 2, cell + pad * 2 + label_h
    label = "Jack-o'-lantern"
    label_font = font("seguisb.ttf", 18)
    frames = []
    for i in range(scene["frames"]):
        frame = scene["framesData"][i]
        expressions = action.get("expressions") or [0]
        eyes = expressions[i % len(expressions)]
        figure = Image.alpha_composite(cell_of(body_sheet, i), cell_of(eyes_sheet, eyes))
        figure_scale = frame.get("scale", 1)
        if figure_scale != 1:
            figure = figure.resize((round(88 * figure_scale), round(88 * figure_scale)), Image.Resampling.NEAREST)
        composed = Image.new("RGBA", (88, 88), (0, 0, 0, 0))
        composed.alpha_composite(cell_of(back_sheet, i))
        composed.alpha_composite(figure, (round(frame.get("dx", 0)), round(frame.get("dy", 0))))
        composed.alpha_composite(cell_of(front_sheet, i))
        composed = composed.resize((cell, cell), Image.Resampling.NEAREST)

        canvas = gradient((width, height), (250, 247, 240), (235, 229, 219))
        draw = ImageDraw.Draw(canvas)
        canvas.alpha_composite(composed, (pad, pad))
        text_width = draw.textbbox((0, 0), label, font=label_font)[2]
        draw.text(((width - text_width) / 2, pad + cell + 14), label, font=label_font, fill=(26, 26, 31, 255))
        frames.append(canvas.convert("RGB").quantize(colors=128, method=Image.MEDIANCUT, dither=Image.Dither.NONE))
    frames[0].save(OUT / "pet-lantern.gif", save_all=True, append_images=frames[1:], duration=250, loop=0, optimize=True)


def compose_pet_postcards() -> None:
    """The seven pets as postcards, one per frame, the way PetPostcardPreview.qml raises them.

    Each card is the QML's: a painted landscape in the pet's own palette (sky, sun or moon, two
    hills, the ground), the idle pet at whole-number zoom standing on the ground line at its ink
    foot, one accessory per slot, a perforated stamp with the pet's initial, a postmark struck
    across its corner, and a caption with the name, a rule and the LENS mark.
    """
    pets = [
        ("cat", "Stub cat"), ("owl", "Owl"), ("mochi", "Mochi"), ("ghost", "Ghost"),
        ("jelly", "Jellyfish"), ("cloud", "Cloud"), ("dog", "Dog"),
    ]
    scenery = {
        "cat":   {"sky": ((247, 220, 180), (242, 185, 140)), "sun": (255, 241, 201), "hillFar": (215, 154, 120), "hillNear": (192, 127, 95), "ground": (143, 168, 106)},
        "dog":   {"sky": ((195, 231, 248), (143, 201, 232)), "sun": (255, 247, 214), "hillFar": (143, 191, 116), "hillNear": (111, 158, 87), "ground": (95, 140, 74)},
        "ghost": {"sky": ((42, 47, 74), (22, 26, 44)), "sun": (242, 240, 216), "hillFar": (51, 58, 92), "hillNear": (35, 40, 64), "ground": (27, 31, 51), "night": True},
        "mochi": {"sky": ((251, 226, 232), (246, 196, 207)), "sun": (255, 243, 208), "hillFar": (224, 167, 179), "hillNear": (201, 138, 152), "ground": (159, 176, 106)},
        "owl":   {"sky": ((74, 58, 99), (122, 90, 130)), "sun": (245, 233, 200), "hillFar": (59, 48, 80), "hillNear": (46, 39, 64), "ground": (36, 31, 51), "night": True},
        "jelly": {"sky": ((195, 240, 234), (111, 201, 196)), "sun": (232, 255, 248), "hillFar": (95, 179, 171), "hillNear": (62, 140, 134), "ground": (47, 111, 106)},
        "cloud": {"sky": ((232, 241, 251), (196, 216, 236)), "sun": (255, 255, 255), "hillFar": (185, 206, 226), "hillNear": (162, 188, 212), "ground": (200, 216, 232)},
    }
    eyewear = {"wire_glasses", "cat_shades", "spectacles", "round_shades", "heart_frames", "monocle",
               "star_frames", "glasses", "candy_shades", "readers", "sleep_mask", "bubble_glasses",
               "dive_mask", "raindrop_glasses", "cloud_frames"}
    zoom = 2  # The figure's whole-number magnification, PetPostcardPreview.qml's zoom.
    paper, card_edge = (250, 251, 254), (222, 226, 233)
    ink_text, ink_muted, ink_faint = (16, 19, 25), (51, 56, 70), (74, 79, 94)

    def postcard(pet: str, name: str) -> Image.Image:
        directory = ROOT / "data" / "pet" / pet
        idle = json.loads((directory / "animations.json").read_text(encoding="utf-8"))["actions"]["idle"]
        expressions = idle.get("expressions") or [0]
        body = Image.open(directory / idle["sheet"]).convert("RGBA").crop((0, 0, 88, 88))
        eyes = Image.open(directory / "sprites" / "expressions" / "eyes.png").convert("RGBA")
        figure = Image.alpha_composite(body, eyes.crop((expressions[0] * 88, 0, (expressions[0] + 1) * 88, 88)))
        items = json.loads((directory / "accessories.json").read_text(encoding="utf-8"))["accessories"]
        taken = set()
        for item in items:  # One piece per slot, in catalogue order; the face slot skips eyewear.
            slot = item["slot"]
            if slot in taken or (slot == "face" and item["id"] in eyewear):
                continue
            figure = Image.alpha_composite(figure, Image.open(directory / item["asset"]).convert("RGBA"))
            taken.add(slot)
        # The ink foot: the idle body's opaque bounding box, centre x and bottom y (pet_assets.cpp).
        alpha = body.getchannel("A")
        box = alpha.point(lambda a: 255 if a >= 40 else 0).getbbox()
        foot_x, foot_y = (box[0] + box[2]) / 2, box[3]

        card = Image.new("RGBA", (600, 480), (0, 0, 0, 0))
        draw = ImageDraw.Draw(card)
        draw.rounded_rectangle((0, 0, 599, 479), radius=24, fill=paper, outline=card_edge, width=2)
        photo = (20, 20, 580, 412)  # The 280 x 196 picture at 2x.
        ground = photo[1] + 356  # photo.height - 18, at 2x.
        top, bottom = scenery[pet]["sky"]
        sky = Image.new("RGBA", (560, 392), (0, 0, 0, 0))
        sky_draw = ImageDraw.Draw(sky)
        for y in range(392):
            t = y / 391
            sky_draw.line((0, y, 560, y), fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
        card.paste(sky, (photo[0], photo[1]))
        draw = ImageDraw.Draw(card)
        sun = scenery[pet]["sun"]
        draw.ellipse((photo[0] + 52, photo[1] + 40, photo[0] + 96, photo[1] + 84),
                     fill=(sun[0], sun[1], sun[2], 229))  # The disc at 90%.
        if scenery[pet].get("night"):
            for star in ((60, 26), (78, 50), (52, 64), (104, 22)):
                draw.rectangle((photo[0] + star[0] * 2, photo[1] + star[1] * 2,
                                photo[0] + star[0] * 2 + 3, photo[1] + star[1] * 2 + 3), fill=(255, 255, 255, 178))
        far_w = round(280 * 0.64 * 2)
        draw.ellipse((photo[0] - 44, ground - round(far_w * 0.56),
                      photo[0] - 44 + far_w, ground - round(far_w * 0.56) + far_w), fill=scenery[pet]["hillFar"])
        near_w = round(280 * 0.66 * 2)
        draw.ellipse((photo[0] + 560 - near_w + 52, ground - round(near_w * 0.44),
                      photo[0] + 560 + 52, ground - round(near_w * 0.44) + near_w), fill=scenery[pet]["hillNear"])
        draw.rectangle((photo[0], ground, photo[0] + 560, photo[3]), fill=scenery[pet]["ground"])
        draw.line((photo[0], ground, photo[0] + 560, ground),
                  fill=tuple(round(c / 1.18) for c in scenery[pet]["ground"]))
        # The picture's rounded mask and hairline, over everything painted so far.
        mask = Image.new("L", (560, 392), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, 559, 391), radius=16, fill=255)
        region = card.crop(photo)
        base = Image.new("RGBA", region.size, paper + (255,))
        card.paste(Image.composite(region, base, mask), photo[:2])
        draw.rounded_rectangle(photo, radius=16, outline=(18, 24, 36, 28), width=1)

        sprite = figure.resize((88 * zoom, 88 * zoom), Image.Resampling.NEAREST)
        card.alpha_composite(sprite, (photo[0] + round(140 - foot_x) * zoom, ground - round(foot_y) * zoom))

        # The stamp: paper with its teeth punched out, an inner frame, the pet's initial.
        stamp = Image.new("RGBA", (88, 108), (0, 0, 0, 0))
        stamp_draw = ImageDraw.Draw(stamp)
        stamp_draw.rectangle((0, 0, 87, 107), fill=paper + (255,))
        teeth = Image.new("L", (88, 108), 255)
        teeth_draw = ImageDraw.Draw(teeth)
        for x in range(10, 88, 14):
            teeth_draw.ellipse((x - 4, -4, x + 4, 4), fill=0)
            teeth_draw.ellipse((x - 4, 104, x + 4, 112), fill=0)
        for y in range(10, 108, 14):
            teeth_draw.ellipse((-4, y - 4, 4, y + 4), fill=0)
            teeth_draw.ellipse((84, y - 4, 92, y + 4), fill=0)
        stamp.putalpha(teeth)
        stamp_draw = ImageDraw.Draw(stamp)
        for x in range(10, 88, 14):
            stamp_draw.ellipse((x - 4, -4, x + 4, 4), outline=(74, 79, 94, 115))
            stamp_draw.ellipse((x - 4, 104, x + 4, 112), outline=(74, 79, 94, 115))
        stamp_draw.rectangle((11, 11, 76, 96), outline=(18, 24, 36, 90))
        init_font = font("seguisb.ttf", 32)
        w = stamp_draw.textbbox((0, 0), name[0], font=init_font)[2]
        stamp_draw.text(((88 - w) / 2, 16), name[0], font=init_font, fill=ink_muted)
        stamp = stamp.rotate(4, resample=Image.BICUBIC)
        card.alpha_composite(stamp, (photo[0] + round((280 - 44 - 10) * zoom), photo[1] + 20))

        # The postmark: one ring and a date block, struck across the stamp's lower corner.
        mark = Image.new("RGBA", (88, 88), (0, 0, 0, 0))
        mark_draw = ImageDraw.Draw(mark)
        mark_draw.ellipse((2, 2, 84, 84), outline=ink_faint + (255,), width=2)
        mark_draw.line((24, 54, 64, 54), fill=ink_faint + (255,), width=2)
        mark_draw.line((24, 66, 52, 66), fill=ink_faint + (255,), width=2)
        mark = mark.rotate(-12, resample=Image.BICUBIC)
        mark.putalpha(mark.getchannel("A").point(lambda a: round(a * 0.4)))
        card.alpha_composite(mark, (photo[0] + round((280 - 44 - 12) * zoom), photo[1] + 20 + 60))

        # The caption: the name, a rule, the LENS mark.
        name_font = font("seguisb.ttf", 26)
        card_draw = ImageDraw.Draw(card)
        caption_y = photo[3] + 20
        card_draw.text((28, caption_y), name, font=name_font, fill=ink_text)
        name_w = card_draw.textbbox((0, 0), name, font=name_font)[2]
        mark_font = font("seguisb.ttf", 18)
        widths = [card_draw.textbbox((0, 0), ch, font=mark_font)[2] for ch in "LENS"]
        mark_w = sum(w + 3 for w in widths) - 3
        x = 600 - 28 - mark_w
        for ch, w in zip("LENS", widths):
            card_draw.text((x, caption_y + 4), ch, font=mark_font, fill=ink_faint)
            x += w + 3
        rule_y = caption_y + 16
        card_draw.line((28 + name_w + 20, rule_y, 600 - 28 - mark_w - 20, rule_y), fill=(18, 24, 36, 40), width=1)
        return card

    pad = 60
    width, height = 600 + pad * 2, 480 + pad * 2
    frames = []
    for pet, name in pets:
        canvas = gradient((width, height), (250, 247, 240), (235, 229, 219))
        add_card_shadow(canvas, (pad + 10, pad + 14, pad + 590, pad + 486))
        canvas.alpha_composite(postcard(pet, name), (pad, pad))
        frames.append(canvas.convert("RGB").quantize(colors=128, method=Image.MEDIANCUT, dither=Image.Dither.NONE))
    frames[0].save(OUT / "pet-postcards.gif", save_all=True, append_images=frames[1:], duration=1100, loop=0, optimize=True)


def main() -> None:
    if not FRAMES.is_dir():
        sys.exit(f"readme-html-compose: frames not found at {FRAMES}; run readme-html-shots.sh first")

    autocrop(OUT / "selection-bar.png", background=(255, 255, 255)).save(OUT / "selection-bar.png", optimize=True)
    autocrop(OUT / "year-in-words.png", background=(255, 255, 255)).save(OUT / "year-in-words.png", optimize=True)
    review_hover = OUT / "review-hover.png"
    if review_hover.exists():
        autocrop(review_hover, background=(244, 236, 217), margin=90).save(OUT / "review.png", optimize=True)
        review_hover.unlink()

    compose_peek_gif()
    compose_selection_styles()
    compose_themes()
    compose_learning_loop()
    compose_pets_gif()
    compose_lantern_gif()
    compose_pet_postcards()

    for name in (
        "english-first.gif",
        "selection-styles.gif",
        "pets.gif",
        "pet-lantern.gif",
        "pet-postcards.gif",
        "themes.png",
        "learning-loop.png",
        "review.png",
        "selection-bar.png",
        "selection-drawer.png",
        "year-in-words.png",
    ):
        path = OUT / name
        print(f"  docs/readme/{name}: {path.stat().st_size / 1024:.0f} KB")


if __name__ == "__main__":
    main()
