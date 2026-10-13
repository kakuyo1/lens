import QtQuick

/**
 * One pill choice in the pet settings, for an action or an accessory. The ink fill marks the one in use,
 * the way the segment control marks its selected item.
 */
Rectangle {
    id: chip

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string label: ""
    property bool selected: false

    signal picked()

    implicitWidth: caption.implicitWidth + 20
    implicitHeight: 27
    radius: chip.tokens.radiusPill
    color: selected ? chip.tokens.ink : chip.tokens.panel2
    border.width: selected ? 0 : 1
    border.color: chip.tokens.line

    Text {
        id: caption
        anchors.centerIn: parent
        text: chip.label
        color: chip.selected ? chip.tokens.on : chip.tokens.text
        font.pixelSize: 12
        font.weight: chip.selected ? Font.Bold : Font.Normal
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: chip.picked() }
}
