import QtQuick

/**
 * The dark values of every colour token, from DESIGN.md section 3.1b.
 *
 * Not the light theme with the lights off, and not the light table inverted. What it decides, in the
 * order it is visible:
 *
 * The neutrals carry one cool cast (hue ~216, chroma rising with lightness) instead of zero
 * saturation: dead grey reads as a default, and three surfaces differing only in luminance are the
 * hardest to tell apart at the bottom of the range; one tint makes the same steps legible.
 *
 * The three surfaces are a ladder, not a rounding -- bg L* 3, panel L* 10, panel2 L* 16. A card
 * floats above the page and an inset sits visibly inside the card; on dark the shadow does almost
 * nothing, so this step is the hierarchy.
 *
 * `line` is the edge: lighter than the fill it borders, which is what an edge is on dark, and the
 * only thing drawing a card's outline. It sits well above panel and panel2 rather than one step
 * above them, and `line2` stays the quieter hairline it is in light.
 *
 * `ok` is tuned for dark rather than lifted from light: same hue family as `okText` and `okBg`
 * (155, held), lightness raised until it reads on the new panel, saturation left where a green
 * still reads as green instead of glowing.
 *
 * `text`, `muted` and `faint` are three readable steps (14 / 8 / 5.6 against panel) rather than
 * two plus a decorative third: `faint` carries 11 px metadata, and metadata is text. `text` stops
 * short of pure white, and `ink` / `on` invert -- on dark the primary action is the light fill.
 */
QtObject {
    readonly property color bg: "#0b0e13"
    readonly property color panel: "#171c25"
    readonly property color panel2: "#212936"
    readonly property color line: "#36404e"
    readonly property color line2: "#242b37"
    readonly property color text: "#e9edf3"
    readonly property color muted: "#a8b3c2"
    readonly property color faint: "#8a95a6"
    readonly property color ok: "#4dc492"
    readonly property color okBg: "#1a4031"
    readonly property color okText: "#7ad7b0"
    readonly property color ink: "#eef3fa"
    readonly property color on: "#0c1016"
    readonly property color danger: "#ec9282"
    readonly property color warn: "#d6ac6b"
    readonly property color popup: "#171c25"

    /**
     * The bubble's frosted fill and its border.
     *
     * The bubble floats over the desktop rather than over a card, so it must hold its own on both:
     * the fill is a tone above panel, which keeps it visible over a dark application; the border is
     * a lit glass edge rather than a separator -- a 9% white line vanishes into any dark window.
     */
    readonly property color bubbleBg: Qt.rgba(30 / 255, 36 / 255, 47 / 255, 0.85)
    readonly property color bubbleBorder: Qt.rgba(1, 1, 1, 0.14)

    /// The sentence-breakdown roles (DESIGN.md 3.1e), lifted for the dark desktop.
    readonly property color roleS: "#7aa9f5"
    readonly property color roleV: "#f07d93"
    readonly property color roleC: "#5fcad2"
    readonly property color roleA: "#e3b25c"
}
