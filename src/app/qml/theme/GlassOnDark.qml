import QtQuick

/**
 * The glass roles over a dark desktop, for both glass themes.
 *
 * UI.md 3.1c and 3.1d leave the dark case open: their dark text fails AA on any dark backdrop, and
 * the choice between a light and a dark text is made on the picture under the card (GlassBackdrop).
 * This table is the light side of that choice. Its text ramp is the dark theme's, so the two dark
 * surfaces read alike, and its tints are white rather than ink, since a dark tint vanishes on a dark
 * picture.
 */
QtObject {
    readonly property color popup: Qt.rgba(23 / 255, 28 / 255, 37 / 255, 0.78)
    readonly property color panel2: Qt.rgba(1, 1, 1, 0.08)
    readonly property color line: Qt.rgba(1, 1, 1, 0.18)
    readonly property color line2: Qt.rgba(1, 1, 1, 0.10)
    readonly property color text: "#e9edf3"
    readonly property color muted: "#a8b3c2"
    readonly property color faint: "#8a95a6"
    readonly property color ink: "#eef3fa"
    readonly property color on: "#0c1016"
}
