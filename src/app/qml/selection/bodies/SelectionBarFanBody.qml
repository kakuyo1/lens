pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects
import QtQuick.Shapes

/**
 * F · the fan (UI.md 4.10): three wedge-shaped blades around a pivot at the bottom, opened above
 * the selection. Pressing one folds the fan toward it and back. The blades are one glass fan
 * under a pentagon mask; the seams and the outline are drawn over it.
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

    readonly property var bladeAngles: [-40, 0, 40]
    readonly property var bladeCentres: [Qt.point(62, 80), Qt.point(130, 76), Qt.point(198, 80)]
    readonly property var bladeWidths: [66, 54, 66]
    readonly property var fanPoints: [
        Qt.point(130, 150), Qt.point(10, 87), Qt.point(78, 26),
        Qt.point(182, 26), Qt.point(250, 87)
    ]

    property real enter: 0
    property int hovered: -1
    property int flashIndex: -1
    property real fold: 0
    property real foldAngle: 0

    implicitWidth: 260
    implicitHeight: 176

    function contains(x, y) {
        let inside = false;
        for (let i = 0, j = root.fanPoints.length - 1; i < root.fanPoints.length; j = i++) {
            const xi = root.fanPoints[i].x;
            const yi = root.fanPoints[i].y;
            const xj = root.fanPoints[j].x;
            const yj = root.fanPoints[j].y;
            if ((yi > y) !== (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                inside = !inside;
        }
        return inside;
    }

    /// @return The blade under a point in this item's coordinates, or -1 outside the fan.
    function wedgeAt(position) {
        if (!root.contains(position.x, position.y))
            return -1;
        const angle = Math.atan2(150 - position.y, position.x - 130) * 180 / Math.PI;
        if (angle >= 112.75)
            return 0;
        if (angle <= 67.25)
            return 2;
        return 1;
    }

    // Colours read the token table directly rather than through a function: a ShapePath's
    // fill is repainted from these bindings every time the theme changes, so a fan first shown
    // in one theme cannot keep the colours of another.
    readonly property bool glass: root.tokens.glassMode !== ""
    readonly property color restSide: root.glass ? root.tokens.panel2
                                           : (root.tokens.dark ? root.tokens.panel2 : root.tokens.bg)
    readonly property color restCenter: root.glass ? "transparent" : root.tokens.panel
    readonly property color hoverTint: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.12)
    readonly property color hoverSide: root.glass ? root.hoverTint : root.tokens.panel
    readonly property color hoverCenter: root.glass ? root.hoverTint : root.tokens.panel2
    readonly property color bladeLeft: root.hovered === 0 ? root.hoverSide : root.restSide
    readonly property color bladeCenter: root.hovered === 1 ? root.hoverCenter : root.restCenter
    readonly property color bladeRight: root.hovered === 2 ? root.hoverSide : root.restSide
    /// The white rim the other glass cards get from GlassSurface. The fan cannot use that
    /// rounded-rect rim, so it is drawn along the pentagon's own edge from the same setting.
    readonly property real rim: root.tokens.glassMode === "liquid" ? Controller.settings.liquidRim
                                                                   : Controller.settings.frostedRim
    readonly property color rimColor: Qt.rgba(1, 1, 1, root.rim / 100)

    function playPress(action) {
        enterAnim.stop();
        enter = 1;
        for (let i = 0; i < root.actions.length; ++i) {
            if (root.actions[i].action === action) {
                root.flashIndex = i;
                root.foldAngle = root.bladeAngles[i];
            }
        }
        pressAnim.restart();
    }

    function activate(action) {
        root.playPress(action);
        root.picked(action);
    }

    onShownChanged: {
        if (shown) {
            pressAnim.stop();
            enter = 0;
            fold = 0;
            hovered = -1;
            flashIndex = -1;
            enterAnim.restart();
        } else {
            // The fold gesture keeps going through the window's exit fade.
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
        duration: root.tokens.motion.pop * 4
        easing.type: Easing.OutCubic
    }

    SequentialAnimation {
        id: pressAnim

        NumberAnimation {
            target: root
            property: "fold"
            to: 1
            duration: root.tokens.motion.press * 0.6
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: root
            property: "fold"
            to: 0
            duration: root.tokens.motion.press * 0.9
            easing.type: Easing.OutQuad
        }
    }

    // The entrance is a left-to-right reveal, like a hand fan opening: the clip's right edge
    // travels across the fan, uncovering the left blade first and the right one last. The fan
    // itself is not scaled or faded; the window's own fade handles the arrival, and the clip
    // does the unfolding.
    Item {
        id: reveal
        width: root.implicitWidth * root.enter
        height: root.implicitHeight
        clip: true

        Item {
            id: fan
            width: root.implicitWidth
            height: root.implicitHeight

            transform: [
                Rotation {
                    origin.x: 130
                    origin.y: 150
                    angle: root.foldAngle * root.fold
                },
                Scale {
                    origin.x: 130
                    origin.y: 150
                    xScale: 1 - 0.84 * root.fold
                    yScale: 1
                }
            ]

        Shape {
            id: fanShadowShape
            anchors.fill: parent
            visible: false

            ShapePath {
                fillColor: "white"
                strokeColor: "transparent"
                startX: 130
                startY: 150
                PathLine { x: 10; y: 87 }
                PathLine { x: 78; y: 26 }
                PathLine { x: 182; y: 26 }
                PathLine { x: 250; y: 87 }
                PathLine { x: 130; y: 150 }
            }
        }

        MultiEffect {
            anchors.fill: parent
            source: fanShadowShape
            // The effect draws its source as well as the shadow, and under the glass themes
            // the fan's centre is transparent: a white source would show through as a white
            // fan. The solid forms cover the source with their fills, so only they cast one.
            visible: root.tokens.glassMode === ""
            shadowEnabled: true
            shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, root.tokens.dark ? 0.7 : 0.2)
            shadowBlur: 0.7
            shadowVerticalOffset: 6
            blurMax: 32
        }

        // The glass is read through a ShaderEffectSource rather than handed to an effect
        // directly: the GlassSurface is rectangular, and the fan has to keep only the pixels
        // inside its pentagon. fan.frag does that cut; a MultiEffect mask left the rectangle
        // showing in the liquid theme.
        GlassSurface {
            id: fanGlass
            anchors.fill: parent
            sheet: root.glassSheet
            mode: root.tokens.glassMode
            radius: 0
            rim: 0
        }

        ShaderEffectSource {
            id: fanGlassSource
            anchors.fill: fanGlass
            sourceItem: fanGlass
            hideSource: true
            // The texture is read by the ShaderEffect below; source and effect must not also
            // paint themselves, or the full rectangular capture shows through the fan.
            visible: false
            live: true
        }

        ShaderEffect {
            anchors.fill: parent
            visible: root.tokens.glassMode !== ""
            fragmentShader: "qrc:/shaders/fan.frag.qsb"
            property variant glass: fanGlassSource
            property vector2d size: Qt.vector2d(width, height)
        }

        Shape {
            anchors.fill: parent

            ShapePath {
                fillColor: root.bladeLeft
                strokeColor: "transparent"
                startX: 130
                startY: 150
                PathLine { x: 10; y: 87 }
                PathLine { x: 78; y: 26 }
                PathLine { x: 130; y: 150 }
            }
            ShapePath {
                fillColor: root.bladeCenter
                strokeColor: "transparent"
                startX: 130
                startY: 150
                PathLine { x: 78; y: 26 }
                PathLine { x: 182; y: 26 }
                PathLine { x: 130; y: 150 }
            }
            ShapePath {
                fillColor: root.bladeRight
                strokeColor: "transparent"
                startX: 130
                startY: 150
                PathLine { x: 182; y: 26 }
                PathLine { x: 250; y: 87 }
                PathLine { x: 130; y: 150 }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.tokens.glassMode !== ""

            ShapePath {
                fillColor: "transparent"
                strokeColor: root.rimColor
                strokeWidth: 2
                startX: 130
                startY: 150
                PathLine { x: 10; y: 87 }
                PathLine { x: 78; y: 26 }
                PathLine { x: 182; y: 26 }
                PathLine { x: 250; y: 87 }
                PathLine { x: 130; y: 150 }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.rimColor
                strokeWidth: 2
                startX: 130
                startY: 150
                PathLine { x: 78; y: 26 }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.rimColor
                strokeWidth: 2
                startX: 130
                startY: 150
                PathLine { x: 182; y: 26 }
            }
        }

        Shape {
            anchors.fill: parent

            ShapePath {
                fillColor: "transparent"
                strokeColor: root.tokens.line
                strokeWidth: 1
                startX: 130
                startY: 150
                PathLine { x: 10; y: 87 }
                PathLine { x: 78; y: 26 }
                PathLine { x: 182; y: 26 }
                PathLine { x: 250; y: 87 }
                PathLine { x: 130; y: 150 }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.tokens.line
                strokeWidth: 1
                startX: 130
                startY: 150
                PathLine { x: 78; y: 26 }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.tokens.line
                strokeWidth: 1
                startX: 130
                startY: 150
                PathLine { x: 182; y: 26 }
            }
        }

        Repeater {
            model: root.actions

            delegate: Column {
                id: face
                required property var modelData
                required property int index

                property real hoverShift: root.hovered === face.index ? -2 : 0

                x: root.bladeCentres[face.index].x - width / 2
                y: root.bladeCentres[face.index].y - height / 2
                width: root.bladeWidths[face.index]
                spacing: 5
                opacity: root.flashIndex === face.index ? 0.35 : 1
                transform: Translate { y: face.hoverShift }

                Behavior on hoverShift {
                    NumberAnimation {
                        duration: root.tokens.motion.press
                        easing.type: root.tokens.motion.easing
                    }
                }

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16
                    height: 16
                    source: face.modelData.source
                    color: root.tokens.muted
                }

                Text {
                    width: parent.width
                    text: face.modelData.label
                    textFormat: Text.PlainText
                    color: root.tokens.text
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                }
            }
        }

        Rectangle {
            x: 126
            y: 156
            width: 8
            height: 18
            radius: 4
            color: root.tokens.line
        }

        Rectangle {
            x: 124.5
            y: 146
            width: 11
            height: 11
            radius: 5.5
            color: root.tokens.panel
            border.width: 1
            border.color: root.tokens.line
        }

        HoverHandler {
            id: fanHover
            cursorShape: Qt.PointingHandCursor

            onHoveredChanged: {
                if (!hovered)
                    root.hovered = -1;
            }
            onPointChanged: root.hovered = root.wedgeAt(fanHover.point.position)
        }

        TapHandler {
            id: fanTap
            onTapped: {
                const index = root.wedgeAt(fanTap.point.position);
                if (index >= 0)
                    root.activate(root.actions[index].action);
            }
        }
        }
    }
}
