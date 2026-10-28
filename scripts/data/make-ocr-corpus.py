#!/usr/bin/env python3
"""Generate the synthetic OCR corpus under test/ocr/corpus/.

The corpus is deliberately small and readable: it exists to open the tooling and to compare
engines, not to be a benchmark suite. Every image is drawn here from the text in SAMPLES, so the
transcription in manifest.json cannot drift from the pixels. The images are committed so tests do
not need Pillow; regenerate with

    python scripts/data/make-ocr-corpus.py

Content is original and synthetic. It must never be replaced with a screenshot of a real desktop,
mailbox or chat: the online smoke uses the same generated images, and a corpus is only safe to
share when nobody's screen is in it.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "test", "ocr", "corpus")

FONT_DIR = os.path.join(os.environ.get("WINDIR", os.environ.get("SystemRoot", "Windows")), "Fonts")
FONTS = {
    "sans": os.path.join(FONT_DIR, "segoeui.ttf"),
    "serif": os.path.join(FONT_DIR, "times.ttf"),
    "mono": os.path.join(FONT_DIR, "consola.ttf"),
    "cjk": os.path.join(FONT_DIR, "msyh.ttc"),
    "cjk-serif": os.path.join(FONT_DIR, "simsun.ttc"),
}

EN_PARAGRAPH = (
    "The harbour clock had run four minutes fast for as long as anyone could remember. "
    "Sailors set their watches by it anyway, because a shared mistake is easier to correct "
    "than a private one. On foggy mornings the bell carried farther than the lighthouse, and "
    "the ferry waited for its second stroke before casting off."
)

EN_PARAGRAPH_TWO = (
    "A small workshop kept every failed prototype on a shelf above the lathe. Visitors often "
    "mistook the shelf for a museum, but the owner called it a library: each crooked bracket "
    "recorded a decision that had seemed reasonable at the time."
)

ZH_PARAGRAPH = (
    "清晨的码头还笼罩在薄雾里，第一班渡船已经解开了缆绳。"
    "售票亭的窗户上贴着一张手写的时刻表，边角被海风吹得卷了起来。"
    "没有人抱怨那四分钟的误差，因为所有人都用它来校准自己的手表。"
)

ZH_PARAGRAPH_TWO = (
    "工作台上方的木架上摆着许多失败的零件。来访的人常把它们当作收藏，"
    "老师傅却说那是一本账：每一个变形的支架都记着一次当时看来合理的决定。"
)

MIXED_PARAGRAPH = (
    "The label on the drawer read “待修 / pending”, and the ink had faded to a soft brown. "
    "Inside were three brass gears and a note: 先量再做，否则还会卡住。"
)

CODE = (
    "#include <chrono>\n"
    "\n"
    "struct Clock {\n"
    "    explicit Clock(int driftMs) : drift{driftMs} {}\n"
    "    int now() const { return base + drift; }\n"
    "    int base = 0;\n"
    "    int drift;\n"
    "};"
)

MIXED_CODE = (
    "// 时钟偏快四分钟，但所有人都用它校准\n"
    "constexpr int kDriftMs = 4 * 60 * 1000;\n"
    "int corrected(int raw) { return raw - kDriftMs; }\n"
    "// 注释与代码混排\n"
)

UI_ROWS = [
    "Appearance",
    "Interface language",
    "Text size",
    "Reduce motion",
    "Apply changes",
]

UI_ROWS_ZH = [
    "外观",
    "界面语言",
    "文字大小",
    "减少动效",
    "应用更改",
]

UI_ROWS_MIXED = [
    "Appearance 外观",
    "Interface language 界面语言",
    "Text size 文字大小",
    "Apply 应用",
]


@dataclass(frozen=True)
class Sample:
    id: str
    language: str
    source: str
    theme: str
    layout: str
    font: str
    scale: int
    text: str


SAMPLES = [
    Sample("browser-en-100-light", "en", "browser", "light", "paragraph", "sans", 100, EN_PARAGRAPH),
    Sample("browser-en-125-light", "en", "browser", "light", "paragraph", "sans", 125, EN_PARAGRAPH),
    Sample("browser-en-150-dark", "en", "browser", "dark", "paragraph", "sans", 150, EN_PARAGRAPH),
    Sample("browser-en-200-dark", "en", "browser", "dark", "paragraph", "sans", 200, EN_PARAGRAPH_TWO),
    Sample("pdf-en-100-light", "en", "pdf", "light", "paragraph", "serif", 100, EN_PARAGRAPH_TWO),
    Sample("pdf-en-125-dark", "en", "pdf", "dark", "paragraph", "serif", 125, EN_PARAGRAPH),
    Sample("code-en-100-dark", "en", "code", "dark", "paragraph", "mono", 100, CODE),
    Sample("code-en-150-light", "en", "code", "light", "paragraph", "mono", 150, CODE),
    Sample("ui-en-100-light", "en", "ui", "light", "sparse", "sans", 100, "\n".join(UI_ROWS)),
    Sample("ui-en-200-dark", "en", "ui", "dark", "sparse", "sans", 200, "\n".join(UI_ROWS)),
    Sample("headline-en-150-light", "en", "single-line", "light", "single", "sans", 150, "The harbour clock runs four minutes fast."),
    Sample("browser-zh-100-light", "zh-Hans", "browser", "light", "paragraph", "cjk", 100, ZH_PARAGRAPH),
    Sample("browser-zh-125-dark", "zh-Hans", "browser", "dark", "paragraph", "cjk", 125, ZH_PARAGRAPH),
    Sample("browser-zh-150-light", "zh-Hans", "browser", "light", "paragraph", "cjk", 150, ZH_PARAGRAPH_TWO),
    Sample("pdf-zh-125-dark", "zh-Hans", "pdf", "dark", "paragraph", "cjk-serif", 125, ZH_PARAGRAPH_TWO),
    Sample("ui-zh-100-light", "zh-Hans", "ui", "light", "sparse", "cjk", 100, "\n".join(UI_ROWS_ZH)),
    Sample("headline-zh-200-dark", "zh-Hans", "single-line", "dark", "single", "cjk", 200, "先量再做，否则还会卡住。"),
    Sample("browser-mixed-100-light", "zh-Hans+en", "browser", "light", "paragraph", "cjk", 100, MIXED_PARAGRAPH),
    Sample("code-mixed-125-dark", "zh-Hans+en", "code", "dark", "paragraph", "mono", 125, MIXED_CODE),
    Sample("ui-mixed-150-light", "zh-Hans+en", "ui", "light", "sparse", "cjk", 150, "\n".join(UI_ROWS_MIXED)),
    Sample("headline-mixed-200-light", "zh-Hans+en", "single-line", "light", "single", "cjk", 200, "待修 / pending: 先量再做。"),
]

THEMES = {
    "light": ("#ffffff", "#1f2328"),
    "dark": ("#17191d", "#e6e6e6"),
}


def font_for(sample: Sample) -> ImageFont.FreeTypeFont:
    if sample.font == "cjk":
        return ImageFont.truetype(FONTS["cjk"], round(18 * sample.scale / 100))
    if sample.font == "cjk-serif":
        return ImageFont.truetype(FONTS["cjk-serif"], round(18 * sample.scale / 100))
    if sample.font == "mono":
        return ImageFont.truetype(FONTS["mono"], round(16 * sample.scale / 100))
    if sample.font == "serif":
        return ImageFont.truetype(FONTS["serif"], round(19 * sample.scale / 100))
    return ImageFont.truetype(FONTS["sans"], round(18 * sample.scale / 100))


def is_cjk(character: str) -> bool:
    code = ord(character)
    return 0x2E80 <= code <= 0x9FFF or 0xF900 <= code <= 0xFAFF or 0xFF00 <= code <= 0xFFEF


def break_opportunity(text: str, at: int) -> bool:
    """Whether a line may end at @p at: after a space, or beside a CJK character."""
    if at <= 0 or at >= len(text):
        return True
    previous, following = text[at - 1], text[at]
    return previous.isspace() or is_cjk(previous) or is_cjk(following)


def wrap_paragraph(paragraph: str, font: ImageFont.FreeTypeFont, width: int) -> list[str]:
    """Wrap by break opportunity rather than by spaces: Chinese has no spaces to break at."""
    lines: list[str] = []
    start = 0
    while start < len(paragraph):
        end = len(paragraph)
        while end > start and font.getlength(paragraph[start:end].rstrip()) > width:
            end -= 1
        if end == start:
            end = start + 1
        cut = end
        while cut > start and not break_opportunity(paragraph, cut):
            cut -= 1
        if cut == start:
            cut = end
        line = paragraph[start:cut].rstrip()
        if line:
            lines.append(line)
        start = cut
        while start < len(paragraph) and paragraph[start].isspace():
            start += 1
    return lines


def visual_lines(sample: Sample, font: ImageFont.FreeTypeFont, width: int) -> list[str]:
    """Wrap @p text the way the picture wraps it, so the drawing is a reader of the transcription."""
    if sample.layout == "single":
        return sample.text.splitlines()
    if sample.layout == "sparse":
        return sample.text.splitlines()

    lines: list[str] = []
    for paragraph in sample.text.splitlines():
        if sample.font == "mono":
            lines.extend(paragraph.splitlines() if paragraph else [""])
            continue
        lines.extend(wrap_paragraph(paragraph, font, width))
    return lines


def draw(sample: Sample) -> Image.Image:
    font = font_for(sample)
    background, ink = THEMES[sample.theme]
    margin = 48
    width = 860
    lines = visual_lines(sample, font, width - 2 * margin)
    ascent, descent = font.getmetrics()
    line_height = round((ascent + descent) * (1.55 if sample.layout == "paragraph" else 2.4))
    height = max(220, 2 * margin + line_height * len(lines))
    image = Image.new("RGB", (width, height), background)
    draw_ = ImageDraw.Draw(image)
    y = margin
    for line in lines:
        draw_.text((margin, y), line, font=font, fill=ink)
        y += line_height
    return image


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    manifest = {"format": 1, "notes": "Synthetic images drawn by scripts/data/make-ocr-corpus.py; text is the transcription.", "samples": []}
    for sample in SAMPLES:
        image = draw(sample)
        path = os.path.join(OUT, sample.id + ".png")
        image.save(path, "PNG", optimize=True)
        manifest["samples"].append(
            {
                "id": sample.id,
                "file": sample.id + ".png",
                "text": sample.text,
                "language": sample.language,
                "source": sample.source,
                "theme": sample.theme,
                "layout": sample.layout,
                "scale": sample.scale,
                "width": image.width,
                "height": image.height,
            }
        )
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(f"wrote {len(SAMPLES)} images and manifest.json to {OUT}")


if __name__ == "__main__":
    main()
