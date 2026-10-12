import QtQuick

/**
 * One pill choice in the pet settings, for an action or an accessory. The ink fill marks the one in use,
 * the way the segment control marks its selected item.
 */
Rectangle {
    id: chip

    property string label: ""
    property bool selected: false

    signal picked()

    implicitWidth: caption.implicitWidth + 20
    implicitHeight: 27
    radius: SettingsTokens.radiusPill
    color: selected ? SettingsTokens.ink : SettingsTokens.panel2
    border.width: selected ? 0 : 1
    border.color: SettingsTokens.line

    Text {
        id: caption
        anchors.centerIn: parent
        text: chip.label
        color: chip.selected ? SettingsTokens.on : SettingsTokens.text
        font.pixelSize: 12
        font.weight: chip.selected ? Font.DemiBold : Font.Normal
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: chip.picked() }
}
