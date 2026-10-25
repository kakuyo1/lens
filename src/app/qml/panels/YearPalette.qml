pragma ComponentBehavior: Bound

import QtQuick

/**
 * The year palette: the hue wheel, the centre disc and the four shade swatches.
 *
 * Its own file so YearWordsPopup stays under the QML size cap; the reader and the state
 * still live in YearWordsPopup, passed in as `year`.
 */
Item {
    id: palette
    required property var year

    implicitHeight: paletteBody.y + paletteBody.height
    width: palette.year.cardWidth - 40

    // Rule and title unroll with the palette.
    Rectangle {
        y: 10
        width: parent.width
        height: 1
        color: palette.year.tokens.line2
    }

    Text {
        objectName: "yearPaletteTitle"
        y: 20
        text: qsTranslate("YearWordsPopup", "Cell colors")
        color: palette.year.tokens.text
        font.pixelSize: 11
        font.weight: Font.Bold
    }

    Item {
        id: paletteBody
        y: 40
        width: parent.width
        height: wheel.height

        Item {
            id: wheel
            objectName: "yearHueWheel"
            width: 124
            height: 124

            Item {
                id: orbit
                anchors.fill: parent

                RotationAnimation {
                    target: orbit
                    property: "rotation"
                    from: -20
                    to: 0
                    duration: palette.year.tokens.motion.pop
                    easing.type: palette.year.tokens.motion.easing
                    running: palette.year.tokens.motion.pop > 0
                }

                Item {
                    id: spin
                    anchors.fill: parent
                    rotation: palette.year.wheelRotation
                    Behavior on rotation {
                        NumberAnimation {
                            duration: palette.year.tokens.motion.pop
                            easing.type: palette.year.tokens.motion.easing
                        }
                    }

                    // The colour disc is inset 12 px; the beads straddle its rim.
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d");
                            const cx = spin.width / 2;
                            const cy = spin.height / 2;
                            const radius = spin.width / 2 - 12;
                            const colors = ["#ef4444", "#f97316", "#eab308", "#84cc16", "#22c55e", "#14b8a6",
                                            "#06b6d4", "#3b82f6", "#6366f1", "#8b5cf6", "#d946ef", "#ec4899"];
                            const step = Math.PI / 6;
                            // A hair of overlap hides the seam between wedges.
                            const overlap = 0.008;
                            // An annulus, so the centre discs read as the card's glass.
                            const inner = 27;
                            for (let index = 0; index < colors.length; ++index) {
                                const from = -Math.PI / 2 + index * step - overlap;
                                ctx.beginPath();
                                ctx.arc(cx, cy, radius, from, from + step + 2 * overlap);
                                ctx.arc(cx, cy, inner, from + step + 2 * overlap, from, true);
                                ctx.closePath();
                                ctx.fillStyle = colors[index];
                                ctx.fill();
                            }
                            // The beads' rest shadow turns with the ring.
                            for (let index = 0; index < colors.length; ++index) {
                                const bx = cx + Math.sin(index * step) * 51;
                                const by = cy - Math.cos(index * step) * 51 + 1;
                                const shadow = ctx.createRadialGradient(bx, by, 8, bx, by, 14);
                                shadow.addColorStop(0, "rgba(20,20,26,0.24)");
                                shadow.addColorStop(1, "rgba(20,20,26,0)");
                                ctx.beginPath();
                                ctx.arc(bx, by, 14, 0, 2 * Math.PI);
                                ctx.fillStyle = shadow;
                                ctx.fill();
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 12
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.24)
                    }

                    Repeater {
                        model: palette.year.hues.length
                        delegate: Rectangle {
                            id: hueCell
                            objectName: "yearHueCell"
                            required property int index
                            readonly property real angle: index * Math.PI / 6
                            readonly property int hue: palette.year.hues[index].hue
                            readonly property bool active: palette.year.hueIndex === index || hueHover.hovered
                            x: wheel.width / 2 - width / 2 + Math.sin(angle) * 51
                            y: wheel.height / 2 - height / 2 - Math.cos(angle) * 51
                            width: 21
                            height: 21
                            radius: 10.5
                            color: Qt.hsla(hue / 360, 0.70, 0.52, 1)
                            border.width: 2
                            border.color: palette.year.tokens.panel
                            scale: active ? 1.12 : 1
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: palette.year.hues[index].name
                            Accessible.onPressAction: palette.year.selectHue(hueCell.index)
                            Keys.onPressed: (event) => {
                                if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                                        && !event.isAutoRepeat) {
                                    event.accepted = true;
                                    palette.year.selectHue(hueCell.index);
                                }
                            }
                            FocusRing { radius: 10.5 }

                            Behavior on scale {
                                NumberAnimation {
                                    duration: palette.year.tokens.motion.press
                                    easing.type: palette.year.tokens.motion.easing
                                }
                            }
                            HoverHandler {
                                id: hueHover
                                cursorShape: Qt.PointingHandCursor
                            }
                            TapHandler { onTapped: palette.year.selectHue(hueCell.index) }

                            // Hover and selection: one ring outside the dot.
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 4
                                height: parent.height + 4
                                radius: width / 2
                                color: "transparent"
                                border.width: 2
                                border.color: palette.year.tokens.text
                                opacity: hueCell.active ? 1 : 0
                                Accessible.ignored: true
                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: palette.year.tokens.motion.press
                                        easing.type: palette.year.tokens.motion.easing
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 56
                height: 56
                radius: 28
                color: palette.year.tokens.panel
                border.width: 1
                border.color: palette.year.tokens.line
            }

            Rectangle {
                anchors.centerIn: parent
                width: 48
                height: 48
                radius: 24
                color: palette.year.tokens.panel2
                Text {
                    objectName: "yearHueName"
                    anchors.centerIn: parent
                    width: parent.width - 8
                    text: palette.year.hues[palette.year.hueIndex].name
                    color: palette.year.tokens.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }

        Row {
            id: shades
            objectName: "yearShadeColumn"
            x: wheel.width + 12
            width: paletteBody.width - x
            height: paletteBody.height
            spacing: 4

            Repeater {
                model: 4
                delegate: Item {
                    id: shadeCell
                    required property int index
                    width: (shades.width - 3 * shades.spacing) / 4
                    height: paletteBody.height

                    // The swatch sits on the wheel's midline; its caption hangs below.
                    Rectangle {
                        id: swatch
                        objectName: "yearShadeSwatch"
                        y: (shadeCell.height - height) / 2
                        width: parent.width
                        height: 18
                        radius: 3
                        color: palette.year.cellColor(shadeCell.index + 1)
                        border.width: palette.year.selectedLevel === shadeCell.index + 1 ? 1 : 0
                        border.color: palette.year.tokens.text
                    }
                    Text {
                        y: swatch.y + swatch.height + 4
                        width: parent.width
                        text: shadeCell.index === 0 ? qsTranslate("YearWordsPopup", "Light") : shadeCell.index === 3 ? qsTranslate("YearWordsPopup", "Dark") : String(shadeCell.index + 1)
                        color: palette.year.tokens.faint
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
