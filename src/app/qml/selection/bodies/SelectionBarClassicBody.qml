pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * The original horizontal bar (UI.md 4.10), kept as the default style: three items on one
 * `panel` card, the card's left edge at the release point. It has no entrance or press gesture
 * of its own; the window's fade carries it, and each item shrinks under the press.
 */
Item {
    id: root

    property var actions: []
    /// Accepted for the same interface as the other bodies; the window's fade is the entrance.
    property bool shown: false
    property var glassSheet: null

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal picked(string action)

    implicitWidth: card.width
    implicitHeight: card.height

    function activate(action) {
        root.picked(action);
    }

    Rectangle {
        id: shadowShape
        anchors.fill: card
        radius: card.radius
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"
        visible: false
    }

    MultiEffect {
        x: card.x
        y: card.y
        source: shadowShape
        shadowEnabled: true
        shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, root.tokens.dark ? 0.75 : 0.24)
        shadowBlur: 0.9
        shadowVerticalOffset: 10
        blurMax: 40
    }

    Rectangle {
        id: card
        width: row.width + 10
        height: row.height + 10
        radius: root.tokens.radiusMenu
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"
        border.width: 1
        // Under glass the rim is the only edge (UI.md 3.1c).
        border.color: root.tokens.glassMode === "" ? root.tokens.line : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: root.glassSheet
            mode: root.tokens.glassMode
            radius: card.radius
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 2

            Repeater {
                model: root.actions

                delegate: Rectangle {
                    id: item
                    required property var modelData

                    width: itemRow.width + 22
                    height: itemRow.height + 14
                    radius: root.tokens.radiusField
                    color: hover.hovered ? root.tokens.panel2 : "transparent"
                    // The press, not the hover: a HoverHandler has no `pressed`, so this read
                    // gave undefined and the press never shrank the item.
                    scale: press.pressed ? 0.97 : 1.0

                    Behavior on scale {
                        NumberAnimation {
                            duration: root.tokens.motion.press
                            easing.type: root.tokens.motion.easing
                        }
                    }

                    Row {
                        id: itemRow
                        anchors.centerIn: parent
                        spacing: 7

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 15
                            height: 15
                            source: item.modelData.source
                            color: root.tokens.muted
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: item.modelData.label
                            textFormat: Text.PlainText
                            color: root.tokens.text
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }
                    }

                    HoverHandler {
                        id: hover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        id: press
                        onTapped: root.activate(item.modelData.action)
                    }
                }
            }
        }
    }
}
