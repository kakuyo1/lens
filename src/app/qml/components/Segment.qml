pragma ComponentBehavior: Bound

import QtQuick

/**
 * The outlined two-or-three-way switch UI.md 4.4 uses for language, theme and the words
 * filter: the selected item takes the ink fill, the rest stay plain.
 *
 * The background is a sibling of the Row, not a child of it: a Row lays its children out in a
 * line, and an item that fills the parent is not something it can place.
 */
Item {
    id: root

    property var labels: []
    property int currentIndex: 0
    /// The table the segments take their colours from: the one of the window they sit in.
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal picked(int index)

    Rectangle {
        anchors.fill: parent
        radius: root.tokens.radiusField
        color: root.tokens.panel2
        border.width: 1
        border.color: root.tokens.line
    }

    Row {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Repeater {
            model: root.labels

            delegate: Rectangle {
                id: cell
                required property int index
                required property var modelData

                width: (root.width - 2) / root.labels.length
                height: root.height - 2
                radius: root.tokens.radiusField - 1
                color: index === root.currentIndex ? root.tokens.ink : "transparent"
                scale: cellPress.pressed ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: root.tokens.motion.press
                        easing.type: root.tokens.motion.easing
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: cell.modelData
                    color: cell.index === root.currentIndex ? root.tokens.on : root.tokens.muted
                    font.pixelSize: 12
                    font.weight: cell.index === root.currentIndex ? Font.Bold : Font.Normal
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: cellPress
                    onTapped: root.picked(cell.index)
                }
            }
        }
    }
}
