pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Shapes

/**
 * The selection-bar style dropdown's preview (UI-SPEC.md 4.4 and 4.10): a miniature of the form under
 * the pointer, drawn on a sample line with the selected phrase, and raised beside the open list
 * by DropdownField.
 *
 * The bar is a window of its own because the list it belongs to is one. The miniature is drawn
 * rather than instantiated from the real bar: that one is a Window whose glass samples the
 * desktop under it, and the desktop under this card is not what the reader is choosing. The
 * card itself takes a sheet of its own like the other previews (docs/adr/0041).
 */
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false

    /// The dropdown option under the pointer: {value, label, previewStyle}.
    property var option: null
    readonly property string style: root.option ? String(root.option.previewStyle) : "bar"
    readonly property string optionLabel: root.option ? String(root.option.label) : ""

    /// @brief Point this window at @p value, the option whose form it is to draw.
    function showOption(value) {
        root.option = value;
    }

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 228
    readonly property int cardHeight: 208

    width: root.cardWidth + 2 * root.shadowMargin
    height: root.cardHeight + 2 * root.shadowMargin

    /// The desktop under this window, for the glass themes only.
    readonly property var glass: Controller.glassBackdrop.surface("selectionBarPreview")
    /// The stored theme's table: the bar follows the reader's theme, not a previewed one.
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: root.glass ? root.glass.lightText : false
    }

    /// The sample line's desk. In the glass themes the card is glass, so the stage takes the
    /// translucent popup layer to stay readable on top of it.
    readonly property color stageColor: root.tokens.glassMode === "" ? root.tokens.bg : root.tokens.popup
    /// The miniature's card fill, an approximation of the glass in the two glass themes.
    readonly property color barFill: root.tokens.glassMode === "liquid"
                                     ? Qt.rgba(248 / 255, 250 / 255, 254 / 255, 0.86)
                                     : root.tokens.glassMode === "frosted"
                                       ? Qt.rgba(1, 1, 1, 0.68)
                                       : root.tokens.panel
    readonly property color barFill2: root.tokens.glassMode === ""
                                      ? root.tokens.panel2
                                      : Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.09)
    readonly property color selectionFill: Qt.rgba(root.tokens.ink.r, root.tokens.ink.g,
                                                   root.tokens.ink.b, 0.14)
    readonly property color iconColor: root.tokens.muted
    readonly property color lineColor: root.tokens.line

    /// The icon of each action, in the order every form shows them.
    readonly property var actionIcons: [
        "qrc:/icons/ui-translate.svg", "qrc:/icons/ui-explain.svg", "qrc:/icons/ui-copy.svg"
    ]

    /// The sentence each form is drawn against: one line, with the sample selection in it.
    /// Separate properties rather than one nested ternary: `lupdate` reads a literal `qsTranslate`
    /// reliably when it stands on its own, and the five strings stay visible to the extractor.
    readonly property string noteBar: qsTranslate("SelectionBarPreview", "The original flat bar")
    readonly property string noteTab: qsTranslate("SelectionBarPreview", "Stands at the selection's side")
    readonly property string noteHand: qsTranslate("SelectionBarPreview", "Spread above the selection")
    readonly property string noteTag: qsTranslate("SelectionBarPreview", "Hangs from the selection on a string")
    readonly property string noteFan: qsTranslate("SelectionBarPreview", "Opens above the selection")
    readonly property string note: root.style === "bar" ? root.noteBar
                                   : root.style === "tab" ? root.noteTab
                                   : root.style === "hand" ? root.noteHand
                                   : root.style === "tag" ? root.noteTag
                                   : root.noteFan

    GlassHost {
        window: root
        sheet: root.glass
    }

    ShadowCard {
        id: surface
        anchors.fill: parent
        radius: root.tokens.radiusCard
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: root.glass
            mode: root.tokens.glassMode
            radius: root.tokens.radiusCard
        }

        Column {
            x: 14
            y: 13
            width: parent.width - 28
            spacing: 10

            Item {
                width: parent.width
                height: 16

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    color: root.tokens.ok
                }

                Text {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("SettingsPopup", "Selection bar")
                    color: root.tokens.text
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("SettingsPopup", "Preview")
                    color: root.tokens.faint
                    font.pixelSize: 11
                }
            }

            Rectangle {
                id: stage
                width: parent.width
                height: 110
                radius: 10
                color: root.stageColor
                border.width: 1
                border.color: root.tokens.line2
                clip: true

                // The sentence, with the word that stands for the reader's selection. Its place
                // moves with the form: the hand and the fan need the room above the line, the
                // tag the room below it, and the tab stands beside it.
                Row {
                    id: sample
                    x: 10
                    y: root.style === "tab" ? 48
                     : root.style === "hand" ? 80
                     : root.style === "tag" ? 34
                     : 88
                    spacing: 3

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "She gave a"
                        color: root.tokens.muted
                        font.pixelSize: 11
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: selectionLabel.width + 4
                        height: 14
                        radius: 3
                        color: root.selectionFill

                        Text {
                            id: selectionLabel
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: "candid"
                            color: root.tokens.text
                            font.pixelSize: 11
                        }
                    }

                    Text {
                        visible: root.style !== "tab"
                        anchors.verticalCenter: parent.verticalCenter
                        text: "answer"
                        color: root.tokens.muted
                        font.pixelSize: 11
                    }
                }

                // Classic · the original flat bar, above the line.
                Item {
                    id: miniClassic
                    visible: root.style === "bar"
                    x: 32
                    y: 56
                    width: 144
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: root.barFill
                        border.width: 1
                        border.color: root.lineColor
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: 13

                        Repeater {
                            model: root.actionIcons

                            delegate: Row {
                                id: classicAction
                                required property var modelData
                                height: 14
                                spacing: 4

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 10
                                    source: classicAction.modelData
                                    color: root.iconColor
                                }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 15
                                    height: 2.5
                                    radius: 1.25
                                    color: root.tokens.faint
                                }
                            }
                        }
                    }
                }

                // A · the side tab, to the right of the selection.
                Item {
                    id: miniTab
                    visible: root.style === "tab"
                    x: 116
                    y: 14
                    width: 56
                    height: 78

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: root.barFill
                        border.width: 1
                        border.color: root.lineColor
                    }

                    Rectangle {
                        x: 3
                        y: 8
                        width: 1
                        height: parent.height - 16
                        color: root.lineColor
                    }

                    Column {
                        x: 8
                        y: 7
                        spacing: 3

                        Repeater {
                            model: root.actionIcons

                            delegate: Row {
                                id: tabActionRow
                                required property var modelData
                                height: 16
                                spacing: 4

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 10
                                    source: tabActionRow.modelData
                                    color: root.iconColor
                                }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 17
                                    height: 2.5
                                    radius: 1.25
                                    color: root.tokens.faint
                                }
                            }
                        }
                    }

                    Shape {
                        width: 7
                        height: 7
                        x: miniTab.width - 8
                        y: miniTab.height - 8
                        // The miniature's diagonals are the same edges the real form draws, at a
                        // third of the size, so the staircase is if anything plainer here: the
                        // geometry renderer carries no coverage, the curve renderer does
                        // (docs/QML.md 6).
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            fillColor: root.barFill2
                            strokeColor: "transparent"
                            startX: 0
                            startY: 0
                            PathLine { x: 7; y: 7 }
                            PathLine { x: 0; y: 7 }
                        }
                    }
                }

                // E · the hand, three cards above the line.
                Item {
                    id: miniHand
                    visible: root.style === "hand"
                    x: 30
                    y: 14
                    width: 146
                    height: 46

                    readonly property var cardX: [0, 47, 94]
                    readonly property var cardAngle: [-6, 0, 6]
                    /// Mirrors the real hand's middle-card lift, so the preview keeps its depth.
                    readonly property var cardY: [4, 2, 4]

                    Repeater {
                        model: 3

                        delegate: Rectangle {
                            id: miniCard
                            required property int index

                            x: miniHand.cardX[miniCard.index]
                            y: miniHand.cardY[miniCard.index]
                            width: 52
                            height: 40
                            radius: 7
                            color: root.barFill2
                            border.width: 1
                            border.color: root.lineColor

                            transform: Rotation {
                                origin.x: 26
                                origin.y: 30
                                angle: miniHand.cardAngle[miniCard.index]
                            }

                            Icon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 9
                                width: 11
                                height: 11
                                source: root.actionIcons[miniCard.index]
                                color: root.iconColor
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 25
                                width: 18
                                height: 2.5
                                radius: 1.25
                                color: root.tokens.faint
                            }
                        }
                    }
                }

                // H · the tag, hanging under the line by its string.
                Item {
                    id: miniTag
                    visible: root.style === "tag"
                    x: 26
                    y: 50
                    width: 150
                    height: 44

                    transform: Rotation {
                        origin.x: 15
                        origin.y: 8
                        angle: 1.4
                    }

                    Rectangle {
                        x: 14
                        y: 0
                        width: 1
                        height: 16
                        color: root.tokens.muted
                    }

                    Rectangle {
                        x: 13.5
                        y: -2
                        width: 4
                        height: 4
                        radius: 2
                        color: root.tokens.muted
                    }

                    Rectangle {
                        y: 16
                        width: parent.width
                        height: 28
                        radius: 8
                        color: root.barFill
                        border.width: 1
                        border.color: root.lineColor
                    }

                    Rectangle {
                        x: 6
                        y: 24
                        width: 11
                        height: 11
                        radius: 5.5
                        color: root.barFill
                        border.width: 2
                        border.color: root.lineColor
                    }

                    Row {
                        x: 28
                        y: 22
                        spacing: 20

                        Repeater {
                            model: root.actionIcons

                            delegate: Icon {
                                required property var modelData
                                width: 11
                                height: 11
                                source: modelData
                                color: root.iconColor
                            }
                        }
                    }
                }

                // F · the fan, opened above the line.
                Item {
                    id: miniFan
                    visible: root.style === "fan"
                    x: 43
                    y: 2
                    width: 146
                    height: 106
                    scale: 0.78
                    transformOrigin: Item.TopLeft

                    Shape {
                        anchors.fill: parent
                        // The stage draws this one at 0.78, so its edges never land on the pixel
                        // grid; see the tab's fold above.
                        preferredRendererType: Shape.CurveRenderer

                        // The three blades share one fill (UI-SPEC.md 4.10); the seams below are
                        // the only boundary between them.
                        ShapePath {
                            fillColor: root.barFill
                            strokeColor: "transparent"
                            startX: 73
                            startY: 88
                            PathLine { x: 5; y: 47 }
                            PathLine { x: 43; y: 10 }
                            PathLine { x: 73; y: 88 }
                        }
                        ShapePath {
                            fillColor: root.barFill
                            strokeColor: "transparent"
                            startX: 73
                            startY: 88
                            PathLine { x: 43; y: 10 }
                            PathLine { x: 103; y: 10 }
                            PathLine { x: 73; y: 88 }
                        }
                        ShapePath {
                            fillColor: root.barFill
                            strokeColor: "transparent"
                            startX: 73
                            startY: 88
                            PathLine { x: 103; y: 10 }
                            PathLine { x: 141; y: 47 }
                            PathLine { x: 73; y: 88 }
                        }
                    }

                    Shape {
                        anchors.fill: parent
                        // The outline over the fills above; same reason.
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: root.lineColor
                            strokeWidth: 1
                            startX: 73
                            startY: 88
                            PathLine { x: 5; y: 47 }
                            PathLine { x: 43; y: 10 }
                            PathLine { x: 103; y: 10 }
                            PathLine { x: 141; y: 47 }
                            PathLine { x: 73; y: 88 }
                        }
                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: root.lineColor
                            strokeWidth: 1
                            startX: 73
                            startY: 88
                            PathLine { x: 43; y: 10 }
                        }
                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: root.lineColor
                            strokeWidth: 1
                            startX: 73
                            startY: 88
                            PathLine { x: 103; y: 10 }
                        }
                    }

                    Icon {
                        x: 30
                        y: 41
                        width: 11
                        height: 11
                        source: root.actionIcons[0]
                        color: root.iconColor
                    }
                    Icon {
                        x: 67.5
                        y: 38
                        width: 11
                        height: 11
                        source: root.actionIcons[1]
                        color: root.iconColor
                    }
                    Icon {
                        x: 105
                        y: 41
                        width: 11
                        height: 11
                        source: root.actionIcons[2]
                        color: root.iconColor
                    }

                    Rectangle {
                        x: 70
                        y: 91
                        width: 6
                        height: 11
                        radius: 3
                        color: root.lineColor
                    }
                    Rectangle {
                        x: 69
                        y: 85
                        width: 8
                        height: 8
                        radius: 4
                        color: root.barFill
                        border.width: 1
                        border.color: root.lineColor
                    }
                }
            }

            Text {
                text: root.optionLabel
                color: root.tokens.text
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: root.note
                color: root.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
        }
    }

    PreviewFrame {
        card: surface.card
        cardRadius: root.tokens.radiusCard
        strokeColor: root.tokens.muted
        opacity: surface.opacity
        transform: Translate { y: (1 - surface.opacity) * 8 }
    }
}
