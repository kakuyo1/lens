pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * E · the hand of cards (UI.md 4.10): three cards fanned out above the selection, with no card
 * behind them. The pointer straightens and lifts the card it is on.
 */
Item {
    id: root

    property var actions: []
    property bool shown: false
    property var glassSheet: null

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal picked(string action)

    property real enter: 0
    property string flashing: ""

    readonly property var cardAngles: [-6, 0, 6]
    readonly property var cardY: [2, -2, 2]
    readonly property var dealX: [34, 0, -34]

    implicitWidth: 238
    implicitHeight: 76

    function playPress(action) {
        root.flashing = action;
        flashTimer.restart();
    }

    function activate(action) {
        root.playPress(action);
        root.picked(action);
    }

    function cardProgress(index) {
        if (root.tokens.motion.reduced)
            return 1;
        return Math.max(0, Math.min(1, (root.enter - index * 0.05) / 0.85));
    }

    onShownChanged: {
        if (shown) {
            flashing = "";
            flashTimer.stop();
            enter = 0;
            enterAnim.restart();
        } else {
            // The pressed card keeps falling through the window's exit fade; the timer clears it.
            enterAnim.stop();
        }
    }

    Component.onCompleted: {
        if (shown)
            enterAnim.restart();
    }

    NumberAnimation {
        id: enterAnim
        target: root
        property: "enter"
        to: 1
        duration: root.tokens.motion.bubble * 2
        easing.type: root.tokens.motion.easing
    }

    Timer {
        id: flashTimer
        interval: root.tokens.motion.press
        onTriggered: root.flashing = ""
    }

    Item {
        id: hand
        x: 0
        y: 6
        width: root.implicitWidth
        height: 64

        Repeater {
            model: root.actions

            delegate: Item {
                id: slot
                required property var modelData
                required property int index

                readonly property real progress: root.cardProgress(slot.index)
                property real hoverLift: hover.hovered ? -6 : 0
                property real hoverScale: hover.hovered ? 1.035 : 1
                property real hoverTurn: hover.hovered ? 0 : 1

                x: slot.index * 77
                width: 84
                height: 64
                z: hover.hovered ? 5 : (slot.index === 1 ? 3 : (slot.index === 0 ? 1 : 2))
                opacity: slot.progress * (root.flashing === slot.modelData.action ? 0 : 1)

                transform: [
                    Translate {
                        x: (1 - slot.progress) * root.dealX[slot.index]
                        y: (1 - slot.progress) * 10 + root.cardY[slot.index] * slot.progress + slot.hoverLift
                    },
                    Rotation {
                        origin.x: 42
                        origin.y: 48
                        angle: root.cardAngles[slot.index] * slot.progress * slot.hoverTurn
                    },
                    Scale {
                        origin.x: 42
                        origin.y: 48
                        xScale: (0.9 + 0.1 * slot.progress) * slot.hoverScale
                        yScale: (0.9 + 0.1 * slot.progress) * slot.hoverScale
                    }
                ]

                Behavior on hoverLift {
                    NumberAnimation {
                        duration: root.tokens.motion.press
                        easing.type: root.tokens.motion.easing
                    }
                }
                Behavior on hoverScale {
                    NumberAnimation {
                        duration: root.tokens.motion.press
                        easing.type: root.tokens.motion.easing
                    }
                }
                Behavior on hoverTurn {
                    NumberAnimation {
                        duration: root.tokens.motion.press
                        easing.type: root.tokens.motion.easing
                    }
                }

                Rectangle {
                    id: shadowShape
                    anchors.fill: face
                    radius: face.radius
                    color: root.tokens.glassMode === "" ? root.tokens.panel2 : "transparent"
                    visible: false
                }

                MultiEffect {
                    x: face.x
                    y: face.y
                    source: shadowShape
                    shadowEnabled: true
                    shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, root.tokens.dark ? 0.65 : 0.16)
                    shadowBlur: 0.65
                    shadowVerticalOffset: 4
                    blurMax: 24
                }

                Rectangle {
                    id: face
                    anchors.fill: parent
                    radius: 10
                    color: root.tokens.glassMode !== "" ? "transparent"
                         : hover.hovered ? root.tokens.panel
                         : root.tokens.panel2
                    border.width: 1
                    border.color: hover.hovered ? root.tokens.muted : root.tokens.line

                    GlassSurface {
                        anchors.fill: parent
                        sheet: root.glassSheet
                        mode: root.tokens.glassMode
                        radius: face.radius
                    }

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 10
                        spacing: 5

                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 15
                            height: 15
                            source: slot.modelData.source
                            color: root.tokens.muted
                        }

                        Text {
                            width: parent.width
                            text: slot.modelData.label
                            textFormat: Text.PlainText
                            color: root.tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            lineHeight: 1.2
                        }
                    }
                }

                HoverHandler {
                    id: hover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    id: press
                    onTapped: root.activate(slot.modelData.action)
                }
            }
        }
    }
}
