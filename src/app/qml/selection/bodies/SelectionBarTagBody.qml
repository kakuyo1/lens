pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * H · the hanging tag (UI-SPEC.md 4.10): a horizontal card tied to the selection by a 16 px string.
 * The grommet sits under the selection's edge; with the reader's side set above, the string and
 * the grommet move to the card's bottom edge and the tag hangs upward.
 */
Item {
    id: root

    property var actions: []
    property bool shown: false
    /// The string is below the card (tag above the selection) rather than above it.
    property bool above: false
    property var glassSheet: null

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal picked(string action)

    readonly property real tilt: 1.4
    /// The wobble added to the resting tilt. Positive turns the tag clockwise on screen.
    property real swing: 0
    property real restTilt: tilt * (tagHover.hovered ? 0.3 : 1)

    property real enter: 0
    property string flashing: ""

    implicitWidth: card.width
    implicitHeight: card.height + 16

    function playPress(action) {
        root.flashing = action;
        swingAnim.stop();
        pressAnim.restart();
    }

    function activate(action) {
        root.playPress(action);
        root.picked(action);
    }

    onShownChanged: {
        if (shown) {
            flashing = "";
            pressAnim.stop();
            swing = -10.4;
            swingAnim.restart();
        } else {
            // The press swing keeps going through the window's exit fade.
            swingAnim.stop();
        }
    }

    Component.onCompleted: {
        if (shown) {
            swing = -10.4;
            swingAnim.restart();
        }
    }

    SequentialAnimation {
        id: swingAnim

        NumberAnimation {
            target: root
            property: "swing"
            to: 1.8
            duration: root.tokens.motion.pop
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: root
            property: "swing"
            to: -2.4
            duration: root.tokens.motion.pop
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: root
            property: "swing"
            to: 0.6
            duration: root.tokens.motion.pop * 0.8
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: root
            property: "swing"
            to: 0
            duration: root.tokens.motion.pop * 1.2
            easing.type: Easing.InOutQuad
        }
    }

    SequentialAnimation {
        id: pressAnim

        NumberAnimation {
            target: root
            property: "swing"
            to: 6
            duration: root.tokens.motion.press * 0.6
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: root
            property: "swing"
            to: -3
            duration: root.tokens.motion.press * 0.6
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: root
            property: "swing"
            to: 0
            duration: root.tokens.motion.press * 0.8
            easing.type: Easing.InOutQuad
        }
    }

    Behavior on restTilt {
        NumberAnimation {
            duration: root.tokens.motion.press
            easing.type: root.tokens.motion.easing
        }
    }

    Item {
        id: tag
        y: root.above ? 0 : 16
        width: card.width
        height: card.height

        transform: Rotation {
            origin.x: 15
            origin.y: root.above ? tag.height - 11 : 11
            angle: root.restTilt + root.swing
        }

        // The string reaches from the card into the selection; the dot at its end is what touches.
        Rectangle {
            x: 14.25
            y: root.above ? card.height - 10 : -16
            width: 1.5
            height: 26
            color: root.tokens.muted
        }

        Rectangle {
            x: 13.25
            y: root.above ? card.height + 14 : -18
            width: 4.5
            height: 4.5
            radius: 2.25
            color: root.tokens.muted
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
            width: row.width + 39
            height: row.height + 12
            radius: 12
            color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"
            border.width: 1
            // Under glass the rim is the only edge (DESIGN.md 3.1c).
            border.color: root.tokens.glassMode === "" ? root.tokens.line : "transparent"

            GlassSurface {
                anchors.fill: parent
                sheet: root.glassSheet
                mode: root.tokens.glassMode
                radius: card.radius
            }

            Row {
                id: row
                x: 31
                y: (parent.height - height) / 2
                spacing: 2

                Repeater {
                    model: root.actions

                    delegate: Rectangle {
                        id: item
                        required property var modelData
                        required property int index

                        property real hoverShift: hover.hovered ? 1 : 0
                        property real flashShift: root.flashing === item.modelData.action ? 8 : 0

                        width: itemRow.width + 18
                        height: itemRow.height + 14
                        radius: 8
                        color: hover.hovered ? root.tokens.panel2 : "transparent"
                        opacity: root.flashing === item.modelData.action ? 0.55 : 1
                        scale: press.pressed ? 0.97 : 1
                        transform: Translate {
                            x: item.hoverShift + item.flashShift
                        }

                        Behavior on hoverShift {
                            NumberAnimation {
                                duration: root.tokens.motion.press
                                easing.type: root.tokens.motion.easing
                            }
                        }
                        Behavior on flashShift {
                            NumberAnimation {
                                duration: root.tokens.motion.press
                                easing.type: root.tokens.motion.easing
                            }
                        }
                        Behavior on scale {
                            NumberAnimation {
                                duration: root.tokens.motion.press
                                easing.type: root.tokens.motion.easing
                            }
                        }

                        Row {
                            id: itemRow
                            x: 9
                            y: (parent.height - height) / 2
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
                                font.pixelSize: 12
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

            // The grommet: an outer ring the string passes through, with the card showing inside.
            Rectangle {
                width: 14
                height: 14
                x: 8
                y: root.above ? card.height - 18 : 4
                radius: 7
                color: root.tokens.panel
                border.width: 2
                border.color: root.tokens.line

                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: root.tokens.panel2
                }
            }
        }

        HoverHandler {
            id: tagHover
            cursorShape: Qt.PointingHandCursor
        }
    }
}
