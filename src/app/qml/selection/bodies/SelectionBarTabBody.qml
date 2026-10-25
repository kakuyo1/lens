pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects
import QtQuick.Shapes

/**
 * A · the side tab (UI-SPEC.md 4.10): three actions stacked on a vertical card, with the spine and
 * folded corner of a page marker. Placed beside the selection, flipping to the other side when
 * the preferred one has no room.
 */
Item {
    id: root

    property var actions: []
    property bool shown: false
    /// The tab is to the left of the selection, so its spine and fold move to the other edge.
    property bool sideLeft: false
    property var glassSheet: null

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal picked(string action)

    property real enter: 0
    property string flashing: ""

    implicitWidth: card.width
    implicitHeight: card.height

    function playPress(action) {
        root.flashing = action;
        flashTimer.restart();
    }

    function activate(action) {
        root.playPress(action);
        root.picked(action);
    }

    /// The pointer reaching one item after the one above it, without a per-item animation.
    function itemProgress(index) {
        if (root.tokens.motion.reduced)
            return 1;
        return Math.max(0, Math.min(1, (root.enter - index * 0.12) / 0.76));
    }

    onShownChanged: {
        if (shown) {
            flashing = "";
            flashTimer.stop();
            enter = 0;
            enterAnim.restart();
        } else {
            // The press flash keeps going through the window's exit fade; the timer clears it.
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
        duration: root.tokens.motion.pop * 2
        easing.type: root.tokens.motion.easing
    }

    Timer {
        id: flashTimer
        interval: root.tokens.motion.press
        onTriggered: root.flashing = ""
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
        width: column.width + 15
        height: column.height + 10
        radius: root.tokens.radiusMenu
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

        Rectangle {
            x: root.sideLeft ? card.width - 5 : 4
            y: 9
            width: 1
            height: card.height - 18
            color: root.tokens.line
        }

        Column {
            id: column
            x: 9
            y: 5
            spacing: 2

            Repeater {
                model: root.actions

                delegate: Rectangle {
                    id: item
                    required property var modelData
                    required property int index

                    readonly property real progress: root.itemProgress(item.index)
                    property real hoverShift: hover.hovered ? 2 : 0
                    property real flashShift: root.flashing === item.modelData.action ? 10 : 0

                    width: itemRow.width + 20
                    height: itemRow.height + 14
                    radius: root.tokens.radiusField
                    color: hover.hovered ? root.tokens.panel2 : "transparent"
                    opacity: item.progress * (root.flashing === item.modelData.action ? 0.55 : 1)
                    scale: press.pressed ? 0.97 : 1

                    Accessible.role: Accessible.Button
                    Accessible.name: item.modelData.label
                    Accessible.onPressAction: root.activate(item.modelData.action)

                    transform: Translate {
                        // The shift lifts the item away from the spine, so its direction flips
                        // with the side the tab ended up on.
                        x: (1 - item.progress) * 8 + (root.sideLeft ? -1 : 1) * (item.hoverShift + item.flashShift)
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
                        x: 10
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

        // The folded corner of a page marker: bottom-right, or bottom-left on the other side.
        Shape {
            width: 9
            height: 9
            x: root.sideLeft ? 1 : card.width - 10
            y: card.height - 10
            // Its hypotenuse is the one diagonal on this form, and the geometry renderer draws
            // it without coverage (docs/QML.md 6).
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: root.tokens.panel2
                strokeColor: "transparent"
                startX: root.sideLeft ? 9 : 0
                startY: 0
                PathLine { x: 9; y: 9 }
                PathLine { x: 0; y: 9 }
            }
        }
    }
}
