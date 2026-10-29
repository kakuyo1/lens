import QtQuick

/**
 * The liquid glass values, from DESIGN.md section 3.1d.
 *
 * The card itself is transparent: the refraction is the only thing drawn over the desktop, so
 * `panel` carries no fill. `text` is a step darker than Frosted's, because nothing whitens the
 * desktop behind it. Like Frosted, this table leaves out the roles the glass does not change.
 */
QtObject {
    readonly property color panel: Qt.rgba(0, 0, 0, 0)
    readonly property color popup: Qt.rgba(248 / 255, 250 / 255, 254 / 255, 0.78)
    readonly property color panel2: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.075)
    readonly property color line: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.16)
    readonly property color line2: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.11)
    readonly property color text: "#101319"
    readonly property color muted: "#333846"
    readonly property color faint: "#4a4f5e"
    readonly property color ink: "#12151b"
    readonly property color on: "#ffffff"
}
