pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The pet type dropdown's preview (UI-SPEC.md 4.4): the hovered pet as a postcard, raised beside the open list by
 * DropdownField.
 *
 * A postcard rather than a row of text because the question a hover asks is "what does this one look like": the
 * picture is the pet's own idle frame, drawn at whole-number magnification so the pixel art keeps its edges, and the
 * marks around it -- a perforated stamp, a postmark, a printed caption -- say what kind of card this is without a word
 * of interface copy. The name on the caption is the row's own label.
 *
 * Only a shipped pet has a card: Pet.portrait() answers with nothing for an installed package, and the window then
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

    /// The still Pet.portrait() answered with, or null when this pet has none.
    readonly property var still: root.option && !root.option.external
        ? Pet.portrait(String(root.option.value)) : null
    /// Whether there is a card to raise. An installed package has none, and neither has a folder that will not load.
    readonly property bool hasContent: root.still !== null && root.still.canvas !== undefined
    /// The name the caption prints: the row's own, so the card and the row say the same thing.
    readonly property string petName: root.option ? String(root.option.label) : ""

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
        liquidTextColor: Tokens.liquidTextColor
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

        // The picture: a well of its own, a shade darker at the top and dissolving into the card's paper at the foot,
        // so the pet stands in a soft ground rather than on a hard-edged panel. The card's paper shows around it, as
        // the margin of a printed postcard does.
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
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.tokens.panel2 }
                GradientStop { position: 1.0; color: root.tokens.panel }
            }

            /// The line the pet stands on, in the picture's own coordinates.
            readonly property real ground: photo.height - 18

            // The ground line, the same device the settings stage uses: the figure is placed on it, not above it.
            Rectangle {
                x: 30
                y: photo.ground
                width: photo.width - 60
                height: 1
                color: root.tokens.line
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
                    x: root.faceAt.x
                    y: root.faceAt.y
                    width: root.canvas
                    height: root.canvas
                    source: root.faceSheet
                    sourceClipRect: Qt.rect(root.faceFrame * root.canvas, 0, root.canvas, root.canvas)
                    smooth: false
                    visible: source !== ""
                }
            }

            // The stamp, stuck on the picture at a hand's angle: paper with its teeth punched out of the edge, an
            // inner frame, and the pet's initial where a stamp carries its value.
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
                    text: root.petName.charAt(0)
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
