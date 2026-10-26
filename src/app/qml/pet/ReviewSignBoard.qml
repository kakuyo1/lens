pragma ComponentBehavior: Bound

import QtQuick

/**
 * The review sign's wooden board (UI-SPEC.md 4.13): a pixel plank with the word on it, two grain lines, four nails and a short post,
 * standing beside the pet. The desktop sign window draws it, and so does the settings preview while the ask action plays, so the
 * two read as one object. The board's own size is designWidth by designHeight from the top-left; the shadow runs to its right and
 * below, and the post hangs under it on the board's centre line, its foot at the pet's feet.
 *
 * The sign is drawn at its design size and scaled from the top-left by the pet's magnification, so it stays in proportion to the
 * figure it stands beside.
 *
 * A pet with sign art of its own draws that instead: one frame of the bin the word takes, with the word on it in the pet's colour.
 * The art's cell starts `bleed` before the board, so the board keeps its place for both.
 */
Item {
    id: sign

    /// The word on the board. One line: it never wraps and never elides.
    property string word: ""
    /// The pet's magnification, Pet.scale. The sign grows and shrinks with it.
    required property real petScale
    /// The board's width at the design size, border included. The parent passes it, the same figure the placement uses.
    required property real designWidth
    /// The pet's sign art (Pet.reviewSign), and the bin of it the word takes; null for the wooden board.
    required property var reviewSign
    required property var bin

    readonly property bool art: bin !== null
    readonly property SurfaceTokens tokens: SurfaceTokens {}
    /// The magnification the sign is drawn at, the default ×3.01, where it is not scaled at all.
    readonly property real designScale: 3.01
    readonly property real boardScale: petScale / designScale
    /// The plank's height at the design size, one 20 px line with 10 px of wood each side; the art's board is as tall as its foot.
    readonly property real designHeight: art ? reviewSign.foot : 40
    /// The post under the plank: ten pixels wide, two pixels of it hidden behind the board, and its foot on the ground.
    readonly property real postWidth: 10
    readonly property real postHeight: 32
    readonly property real postOverlap: 2
    readonly property real designBase: art ? reviewSign.foot : designHeight + postHeight - postOverlap
    readonly property real shadowOffset: 4
    readonly property real boardWidth: designWidth * boardScale
    readonly property real boardHeight: designHeight * boardScale
    /// The window's size: the plank and post plus the shadow that runs to their right and below, or the art's cell.
    readonly property real signWidth: (art ? bin.cellWidth : designWidth + shadowOffset) * boardScale
    readonly property real signHeight: (art ? bin.cellHeight : designBase + shadowOffset) * boardScale
    /// How far the window starts before the board's top-left: the art's margin, nothing for the wooden board.
    readonly property real bleed: art ? reviewSign.margin * boardScale : 0
    /// Whether the sign is on a window that is shown, which is when its motion runs.
    readonly property bool onScreen: visible && Window.window !== null && Window.window.visible
    /// The word is 20 px at every length (UI-SPEC.md 4.13); a longer word makes a wider plank.
    readonly property int wordPixelSize: 20
    readonly property string wordFamily: Qt.fontFamilies().indexOf("Pixelify Sans") >= 0 ? "Pixelify Sans" : tokens.monoFamily
    /// The plank's wood: one fixed palette, as the sign is pet art and does not follow the theme (UI-SPEC.md 4.13).
    readonly property color plank: "#d2a76a"
    readonly property color plankLight: "#e6c88f"
    readonly property color plankShade: "#a8783f"
    readonly property color plankGrain: "#bd8d4f"
    readonly property color postWood: "#b8864f"
    readonly property color postLight: "#d2a76a"
    readonly property color postShade: "#8a5a2f"
    readonly property color outline: "#3a2412"
    readonly property color nail: "#5b3b20"
    readonly property color wordInk: "#2b1a0e"
    readonly property int border: 3

    width: signWidth
    height: signHeight

    // The design-size drawing, scaled from its top-left: plank, post, nails, wood grain and shadow shrink together.
    Item {
        visible: !sign.art
        width: sign.designWidth + sign.shadowOffset
        height: sign.designBase + sign.shadowOffset
        scale: sign.boardScale
        transformOrigin: Item.TopLeft

        // The plank's shadow, the post's, and the patch of shadow the post's foot stands in.
        Rectangle {
            x: sign.shadowOffset
            y: sign.shadowOffset
            width: sign.designWidth
            height: sign.designHeight
            color: "#2114141a"
        }

        Rectangle {
            x: post.x + sign.shadowOffset
            y: post.y + sign.shadowOffset
            width: sign.postWidth
            height: sign.postHeight
            color: "#2114141a"
        }

        Rectangle {
            x: sign.designWidth / 2 - 10
            y: sign.designBase - 1
            width: 20
            height: 3
            radius: 1.5
            color: "#21141414"
        }

        // The post: a dark frame, the wood, a lit left edge and a shaded right one.
        Rectangle {
            id: post

            x: (sign.designWidth - sign.postWidth) / 2
            y: sign.designHeight - sign.postOverlap
            width: sign.postWidth
            height: sign.postHeight
            color: sign.outline

            Rectangle {
                x: 2
                y: 2
                width: sign.postWidth - 4
                height: sign.postHeight - 4
                color: sign.postWood
            }

            Rectangle {
                x: 2
                y: 2
                width: 1
                height: sign.postHeight - 4
                color: sign.postLight
            }

            Rectangle {
                x: sign.postWidth - 3
                y: 2
                width: 1
                height: sign.postHeight - 4
                color: sign.postShade
            }
        }

        // The plank: a dark frame, the wood, a lit top and left edge, a shaded bottom and right one.
        Rectangle {
            width: sign.designWidth
            height: sign.designHeight
            color: sign.outline

            Rectangle {
                x: sign.border
                y: sign.border
                width: sign.designWidth - 2 * sign.border
                height: sign.designHeight - 2 * sign.border
                color: sign.plank
            }

            Rectangle {
                x: sign.border
                y: sign.border
                width: sign.designWidth - 2 * sign.border
                height: 1
                color: sign.plankLight
            }

            Rectangle {
                x: sign.border
                y: sign.border
                width: 1
                height: sign.designHeight - 2 * sign.border
                color: sign.plankLight
            }

            Rectangle {
                x: sign.border
                y: sign.designHeight - sign.border - 1
                width: sign.designWidth - 2 * sign.border
                height: 1
                color: sign.plankShade
            }

            Rectangle {
                x: sign.designWidth - sign.border - 1
                y: sign.border
                width: 1
                height: sign.designHeight - 2 * sign.border
                color: sign.plankShade
            }

            // Two grain lines, kept out of the word's line, and four nails with a lit corner each.
            Repeater {
                model: [8, sign.designHeight - 9]

                Rectangle {
                    required property int modelData

                    x: sign.border + 5
                    y: modelData
                    width: sign.designWidth - 2 * sign.border - 10
                    height: 1
                    color: sign.plankGrain
                }
            }

            Repeater {
                model: [[7, 7], [sign.designWidth - 9, 7], [7, sign.designHeight - 9], [sign.designWidth - 9, sign.designHeight - 9]]

                Rectangle {
                    required property var modelData

                    x: modelData[0]
                    y: modelData[1]
                    width: 2
                    height: 2
                    color: sign.nail

                    Rectangle {
                        width: 1
                        height: 1
                        color: sign.plankLight
                    }
                }
            }

            Text {
                id: wordText

                // The line is 20 px tall, as line-height 1 gives at 20 px, and sits inside the border and padding.
                anchors.horizontalCenter: parent.horizontalCenter
                y: (sign.designHeight - sign.wordPixelSize) / 2
                height: sign.wordPixelSize
                verticalAlignment: Text.AlignVCenter
                text: sign.word
                color: sign.wordInk
                font.family: sign.wordFamily
                font.pixelSize: sign.wordPixelSize
                wrapMode: Text.NoWrap
            }
        }
    }

    // The pet's own art, from the board's top-left: the cell starts `bleed` before it.
    Loader {
        objectName: "reviewSignArt"
        active: sign.art
        x: sign.bleed
        y: sign.bleed
        scale: sign.boardScale
        transformOrigin: Item.TopLeft

        sourceComponent: Item {
            id: artwork

            /// Beats since the sign came up; the frame advances one step per `every` of them.
            property int beat: 0
            readonly property bool moving: sign.onScreen && !sign.tokens.motion.reduced
            readonly property int frame: moving ? Math.floor(beat / sign.reviewSign.every) % sign.reviewSign.frames : 0

            Image {
                x: -sign.reviewSign.margin
                y: -sign.reviewSign.margin
                width: sign.bin.cellWidth
                height: sign.bin.cellHeight
                source: sign.bin.source
                sourceClipRect: Qt.rect(artwork.frame * sign.bin.cellWidth, 0, sign.bin.cellWidth, sign.bin.cellHeight)
                smooth: false
            }

            Text {
                objectName: "reviewSignWord"
                y: sign.reviewSign.wordTop
                width: sign.designWidth
                horizontalAlignment: Text.AlignHCenter
                text: sign.word
                color: sign.reviewSign.wordColor
                font.family: sign.wordFamily
                font.pixelSize: sign.wordPixelSize
                wrapMode: Text.NoWrap
            }

            Timer {
                interval: sign.reviewSign.beatMs
                running: artwork.moving
                repeat: true
                onTriggered: artwork.beat++
            }
        }
    }
}
