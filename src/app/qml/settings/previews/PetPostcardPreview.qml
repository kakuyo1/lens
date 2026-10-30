pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The pet type dropdown's preview (UI-SPEC.md 4.4): the hovered pet as a postcard, raised beside the open list by
 * DropdownField.
 *
 * A postcard rather than a row of text because the question a hover asks is "what does this one look like": the
 * picture is the pet dressed in one piece per slot and standing in a painted landscape of its own, drawn at
 * whole-number magnification so the pixel art keeps its edges, and the marks around it -- a perforated stamp, a
 * postmark, a printed caption -- say what kind of card this is without a word of interface copy. The name on the
 * caption is the row's own label.
 *
 * Only a shipped pet has a card: Pet.picker.portrait() answers with nothing for an installed package, and the window then
 * stays down rather than standing empty beside the list. Like the other dropdown previews it is a window of its own
 * (the list it belongs to is one), and the two glass themes draw the real desktop through a sheet of their own
 * (docs/adr/0041).
 */
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false

    /// The dropdown option under the pointer: {value, label, group, note, external}.
    property var option: null

    /// The still Pet.picker.portrait() answered with, or null when this pet has none.
    readonly property var still: root.option && !root.option.external
        ? Pet.picker.portrait(String(root.option.value)) : null
    /// Whether there is a card to raise. An installed package has none, and neither has a folder that will not load.
    readonly property bool hasContent: root.still !== null && root.still.canvas !== undefined
    /// The name the caption prints: the row's own, so the card and the row say the same thing.
    readonly property string petName: root.option ? String(root.option.label) : ""
    /// The character the stamp carries: one per pet in each language, since a name's first character is not one
    /// (the Chinese names of the dog, the ghost and the cloud all open with 小).
    readonly property string petMark: {
        switch (root.option ? String(root.option.value) : "") {
        case "dog": return qsTranslate("PetPostcardPreview", "Dog", "stamp mark");
        case "cat": return qsTranslate("PetPostcardPreview", "Stub cat", "stamp mark");
        case "owl": return qsTranslate("PetPostcardPreview", "Owl", "stamp mark");
        case "mochi": return qsTranslate("PetPostcardPreview", "Mochi", "stamp mark");
        case "ghost": return qsTranslate("PetPostcardPreview", "Ghost", "stamp mark");
        case "jelly": return qsTranslate("PetPostcardPreview", "Jellyfish", "stamp mark");
        case "cloud": return qsTranslate("PetPostcardPreview", "Cloud", "stamp mark");
        default: return root.petName.charAt(0);
        }
    }

    /// @brief Point this window at @p value, the option whose pet it is to picture.
    function showOption(value) {
        root.option = value;
    }

    /// The margin the card's shadow needs. DropdownField subtracts it to put the card clear of the list.
    readonly property int shadowMargin: 26
    readonly property int cardWidth: 300
    /// Fixed, whichever row is hovered: the card does not jump as the pointer crosses the list, and placement can
    /// read it before the first layout (the note preview's rule, for the same reason).
    readonly property int cardHeight: 240

    width: root.cardWidth + 2 * root.shadowMargin
    height: root.cardHeight + 2 * root.shadowMargin

    /// The desktop under this window, for the glass themes only.
    readonly property var glass: Controller.glassBackdrop.surface("petPreview")
    /// The stored theme's table: this card follows the reader's theme rather than previewing one.
    readonly property TokenTable tokens: TokenTable {
        theme: Tokens.theme
        glassEnabled: true
        glassLightText: root.glass ? root.glass.lightText : false
        motion: Tokens.motion
    }

    // The still's parts, read once each: every read of a variant map makes a new JS object.
    readonly property int canvas: root.still && root.still.canvas !== undefined ? root.still.canvas : 0
    readonly property point foot: root.still && root.still.foot ? root.still.foot : Qt.point(0, 0)
    readonly property point bodyAt: root.still && root.still.anchors && root.still.anchors.body
        ? root.still.anchors.body : Qt.point(0, 0)
    readonly property point faceAt: root.still && root.still.anchors && root.still.anchors.face
        ? root.still.anchors.face : Qt.point(0, 0)
    readonly property string bodySheet: root.still && root.still.sheets ? String(root.still.sheets.body) : ""
    readonly property string faceSheet: root.still && root.still.sheets ? String(root.still.sheets.expression) : ""
    readonly property int faceFrame: root.still && root.still.expression !== undefined ? root.still.expression : 0

    /// The pieces the card dresses the pet in: one per slot, the face only when it is not eyewear, so the face
    /// stays visible. Empty when the portrait carries no outfit.
    readonly property var outfit: root.still && root.still.outfit !== undefined ? root.still.outfit : []
    /// The landscape behind the pet: a painted sky, a sun or moon, two hills and the ground. One palette per pet,
    /// so each card stands somewhere of its own without a single new art file.
    readonly property var scenery: {
        switch (root.option ? String(root.option.value) : "") {
        case "cat":
            return { sky: ["#f7dcb4", "#f2b98c"], sun: "#fff1c9", hillFar: "#d79a78", hillNear: "#c07f5f", ground: "#8fa86a", night: false };
        case "dog":
            return { sky: ["#c3e7f8", "#8fc9e8"], sun: "#fff7d6", hillFar: "#8fbf74", hillNear: "#6f9e57", ground: "#5f8c4a", night: false };
        case "ghost":
            return { sky: ["#2a2f4a", "#161a2c"], sun: "#f2f0d8", hillFar: "#333a5c", hillNear: "#232840", ground: "#1b1f33", night: true };
        case "mochi":
            return { sky: ["#fbe2e8", "#f6c4cf"], sun: "#fff3d0", hillFar: "#e0a7b3", hillNear: "#c98a98", ground: "#9fb06a", night: false };
        case "owl":
            return { sky: ["#4a3a63", "#7a5a82"], sun: "#f5e9c8", hillFar: "#3b3050", hillNear: "#2e2740", ground: "#241f33", night: true };
        case "jelly":
            return { sky: ["#c3f0ea", "#6fc9c4"], sun: "#e8fff8", hillFar: "#5fb3ab", hillNear: "#3e8c86", ground: "#2f6f6a", night: false };
        case "cloud":
            return { sky: ["#e8f1fb", "#c4d8ec"], sun: "#ffffff", hillFar: "#b9cee2", hillNear: "#a2bcd4", ground: "#c8d8e8", night: false };
        default:
            return { sky: ["#dfe7ef", "#c3d1df"], sun: "#f5f0d8", hillFar: "#a8bccd", hillNear: "#8ea4b8", ground: "#9fb0a8", night: false };
        }
    }

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

        // The picture: the pet in a painted landscape of its own -- sky, a sun or moon, two hills and the ground --
        // with the card's paper showing around it, as the margin of a printed postcard does. The landscape takes the
        // pet's own palette rather than the theme's, because a postcard's photograph does not change with the app.
        Rectangle {
            id: photo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            height: 196
            radius: root.tokens.radiusGroup
            border.width: 1
            border.color: root.tokens.line2
            clip: true

            /// The line the pet stands on, in the picture's own coordinates.
            readonly property real ground: photo.height - 18

            // The sky, from the top of the card down to the horizon.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: root.scenery.sky[0] }
                    GradientStop { position: 1.0; color: root.scenery.sky[1] }
                }
            }

            // The sun, or a night card's moon: one disc, in the corner the stamp leaves free.
            Rectangle {
                x: 26
                y: 20
                width: 22
                height: 22
                radius: 11
                color: root.scenery.sun
                opacity: 0.9
            }

            // Stars, only where the card is a night one: four small dots, placed by hand.
            Repeater {
                model: root.scenery.night ? [[60, 26], [78, 50], [52, 64], [104, 22]] : []

                Rectangle {
                    required property var modelData
                    x: modelData[0]
                    y: modelData[1]
                    width: 2
                    height: 2
                    color: "#ffffff"
                    opacity: 0.7
                }
            }

            // Two hills at the horizon: big discs whose tops are the only part above the ground line.
            Rectangle {
                x: -22
                y: photo.ground - width * 0.56
                width: photo.width * 0.64
                height: width
                radius: width / 2
                color: root.scenery.hillFar
            }
            Rectangle {
                x: photo.width - width + 26
                y: photo.ground - width * 0.44
                width: photo.width * 0.66
                height: width
                radius: width / 2
                color: root.scenery.hillNear
            }

            // The ground the pet stands on, and a hairline where it meets the sky.
            Rectangle {
                y: photo.ground
                width: parent.width
                height: parent.height - photo.ground
                color: root.scenery.ground
            }
            Rectangle {
                y: photo.ground
                width: parent.width
                height: 1
                color: Qt.darker(root.scenery.ground, 1.18)
            }

            // The pet, drawn as the desktop draws it: one cell of the idle sheet per layer, at the anchors the data
            // gives. A whole-number magnification, because a fractional one leaves the pixel art's edges uneven.
            Item {
                id: figure

                readonly property int zoom: 2

                x: Math.round(photo.width / 2 - root.foot.x * zoom)
                y: Math.round(photo.ground - root.foot.y * zoom)
                width: root.canvas
                height: root.canvas
                scale: zoom
                transformOrigin: Item.TopLeft

                Image {
                    z: 0
                    x: root.bodyAt.x
                    y: root.bodyAt.y
                    width: root.canvas
                    height: root.canvas
                    source: root.bodySheet
                    sourceClipRect: Qt.rect(0, 0, root.canvas, root.canvas)
                    smooth: false
                    visible: source !== ""
                }

                Image {
                    z: 2
                    x: root.faceAt.x
                    y: root.faceAt.y
                    width: root.canvas
                    height: root.canvas
                    source: root.faceSheet
                    sourceClipRect: Qt.rect(root.faceFrame * root.canvas, 0, root.canvas, root.canvas)
                    smooth: false
                    visible: source !== ""
                }

                // The clothes: one accessory per slot, each at the anchor the data gives for its slot and in the
                // stacking order accessories.json sets -- body 3, face 4, head 5, so they sit over the pet's own
                // layers (body 0, expression 2) and under nothing else on this card.
                Repeater {
                    model: root.outfit

                    Image {
                        required property var modelData

                        z: modelData.zIndex
                        x: modelData.x
                        y: modelData.y
                        width: root.canvas
                        height: root.canvas
                        source: modelData.asset
                        sourceClipRect: Qt.rect(0, 0, root.canvas, root.canvas)
                        smooth: false
                    }
                }
            }

            // The stamp, stuck on the picture at a hand's angle: paper with its teeth punched out of the edge, an
            // inner frame, and the pet's mark where a stamp carries its value.
            Item {
                id: stamp

                x: photo.width - width - 10
                y: 10
                width: 44
                height: 54
                rotation: 4

                Canvas {
                    anchors.fill: parent

                    /// The colours the stamp is drawn in, as properties so a theme change repaints it with the rest.
                    property color paper: root.tokens.panel
                    property color frame: root.tokens.line
                    property color teeth: Qt.rgba(root.tokens.faint.r, root.tokens.faint.g, root.tokens.faint.b, 0.45)

                    onPaint: {
                        const ctx = getContext("2d");
                        const radius = 2.2;
                        ctx.clearRect(0, 0, width, height);
                        ctx.fillStyle = paper;
                        ctx.fillRect(0, 0, width, height);
                        // The teeth: half circles cut out of the paper, so the picture shows through the notches, and
                        // a hairline round each one, because a white stamp on a pale picture is otherwise no edge at
                        // all.
                        const notches = [];
                        for (let x = 5; x < width; x += 7)
                            notches.push([x, 0], [x, height]);
                        for (let y = 5; y < height; y += 7)
                            notches.push([0, y], [width, y]);
                        ctx.globalCompositeOperation = "destination-out";
                        for (const [x, y] of notches) {
                            ctx.beginPath();
                            ctx.arc(x, y, radius, 0, 2 * Math.PI);
                            ctx.fill();
                        }
                        ctx.globalCompositeOperation = "source-over";
                        ctx.strokeStyle = teeth;
                        ctx.lineWidth = 1;
                        for (const [x, y] of notches) {
                            ctx.beginPath();
                            ctx.arc(x, y, radius, 0, 2 * Math.PI);
                            ctx.stroke();
                        }
                        ctx.strokeStyle = frame;
                        ctx.strokeRect(5.5, 5.5, width - 11, height - 11);
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaperChanged: requestPaint()
                    onFrameChanged: requestPaint()
                    onTeethChanged: requestPaint()
                }

                // The value sits high in the stamp, as a printed one does, and leaves the lower half to the cancel.
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    text: root.petMark
                    color: root.tokens.muted
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
            }

            // The postmark, struck across the stamp's lower corner: one ring and a date block, the way a cancel reads
            // at this size. It is the one mark that says "posted" rather than "printed".
            Canvas {
                id: postmark

                x: stamp.x - 2
                y: stamp.y + stamp.height - 24
                width: 44
                height: 44
                rotation: -12
                opacity: 0.4

                /// The ink the mark is struck in, as a property so a theme change repaints it with the rest.
                property color ink: root.tokens.faint

                onPaint: {
                    const ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    ctx.strokeStyle = ink;
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.arc(22, 22, 21, 0, 2 * Math.PI);
                    ctx.stroke();
                    // The date block, low in the ring: two rules where a postmark prints the day it was struck, kept
                    // out of the upper half so the mark never reads as a face.
                    ctx.beginPath();
                    ctx.moveTo(12, 27);
                    ctx.lineTo(32, 27);
                    ctx.moveTo(12, 33);
                    ctx.lineTo(26, 33);
                    ctx.stroke();
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onInkChanged: requestPaint()
            }
        }

        // The printed caption: the pet's name, a rule running out to the card's edge, and the publisher's mark a
        // postcard carries in its corner. No label, no punctuation: the row above already said "Pet type".
        Item {
            id: caption

            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.top: photo.bottom
            anchors.topMargin: 10
            height: 16

            Text {
                id: petNameText

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                // Sized by its own text rather than stretched to the row, which would leave the rule beside it no
                // room at all: the caption is a name, a rule and a mark, in that order.
                width: Math.min(implicitWidth, caption.width - mark.width - 40)
                text: root.petName
                color: root.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                id: mark

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "LENS"
                color: root.tokens.faint
                font.pixelSize: 9
                font.weight: Font.Bold
                font.letterSpacing: 1.4
            }

            Rectangle {
                anchors.left: petNameText.right
                anchors.leftMargin: 10
                anchors.right: mark.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: root.tokens.line
            }
        }
    }

    // The demo mark every preview card carries, outside the card rather than on its edge.
    PreviewFrame {
        card: surface.card
        cardRadius: root.tokens.radiusCard
        strokeColor: root.tokens.muted
        opacity: surface.opacity
        transform: Translate { y: (1 - surface.opacity) * 8 }
    }
}
