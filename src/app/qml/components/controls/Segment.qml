pragma ComponentBehavior: Bound

import QtQuick

/**
 * The outlined two-or-three-way switch UI-SPEC.md 4.4 uses for language, theme and the words
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

    /// One tab stop for the whole control, the way a radio group takes one: the arrow keys move
    /// the choice, so the cells themselves are named and checked for the screen reader but are
    /// not tab stops of their own.
    activeFocusOnTab: true
    Accessible.role: Accessible.Grouping
    Keys.onPressed: (event) => {
        if (root.labels.length < 2)
            return;
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            event.accepted = true;
            const step = event.key === Qt.Key_Right ? 1 : -1;
            const next = Math.max(0, Math.min(root.labels.length - 1, root.currentIndex + step));
            if (next !== root.currentIndex) {
                root.currentIndex = next;
                root.picked(next);
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.tokens.radiusField
        color: root.tokens.panel2
        border.width: 1
        // The focus state UI-SPEC.md 4.4 gives this control: the outline turns ink, no glow.
        border.color: root.activeFocus ? root.tokens.ink : root.tokens.line
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

                Accessible.role: Accessible.RadioButton
                Accessible.name: cell.modelData
                Accessible.checked: cell.index === root.currentIndex
                Accessible.onToggleAction: {
                    root.currentIndex = cell.index;
                    root.picked(cell.index);
                }

                width: (root.width - 2) / root.labels.length
                height: root.height - 2
                radius: root.tokens.radiusField - 1
                color: index === root.currentIndex ? root.tokens.ink : "transparent"
                Behavior on color {
                    ColorAnimation {
                        duration: root.tokens.motion.press
                        easing.type: root.tokens.motion.easing
                    }
                }
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
                    Behavior on color {
                        ColorAnimation {
                            duration: root.tokens.motion.press
                            easing.type: root.tokens.motion.easing
                        }
                    }
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
