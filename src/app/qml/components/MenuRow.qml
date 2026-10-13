import QtQuick

/**
 * One row of the tray menu (UI.md section 4.2): a glyph and a label, an optional value on the
 * right in the monospace face, and the hover fill. The dangerous one -- Quit -- is the same row
 * in the danger colour.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string source: ""
    property string label: ""
    property string note: ""
    property bool danger: false

    /// Whether the pointer is on this row. The language row watches it to unfold its list.
    readonly property alias hovered: hover.hovered

    signal picked()

    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: root.tokens.radiusField
        color: hover.hovered ? root.tokens.panel2 : "transparent"
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler { onTapped: root.picked() }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        spacing: 9

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            source: root.source
            color: root.danger ? root.tokens.danger : root.tokens.muted
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.danger ? root.tokens.danger : root.tokens.text
            font.pixelSize: 13
        }
    }

    MixedText {
        visible: root.note !== ""
        id: noteText
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        value: root.note
        color: root.tokens.faint
        pixelSize: 11
    }
}
