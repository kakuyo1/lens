import QtQuick

/**
 * The frosted glass values, from DESIGN.md section 3.1c.
 *
 * The card's fill is a fixed 40% white laid over the blurred desktop, so `panel` is translucent and
 * the tokens sitting on it are dark tints. TokenTable reads every role this table leaves out from the
 * light one, and swaps the text, ink and tint roles for GlassOnDark when the desktop under the card
 * is dark.
 */
QtObject {
    readonly property color panel: Qt.rgba(1, 1, 1, 0.4)
    readonly property color popup: Qt.rgba(1, 1, 1, 0.78)
    readonly property color panel2: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.065)
    readonly property color line: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.15)
    readonly property color line2: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.10)
    readonly property color text: "#14161c"
    readonly property color muted: "#3c4250"
    readonly property color faint: "#535865"
    readonly property color ink: "#191b21"
    readonly property color on: "#ffffff"
}
