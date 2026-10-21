import QtQuick

/**
 * The review sign's board (UI.md 4.13): the paper with the word on it, its shadow, and the pole that hangs from it. The
 * desktop sign window draws it, and so does the settings preview while the hold-sign action plays, so the two read as
 * one object. The board's own size is boardWidth by boardHeight from the top-left; the shadow runs to its right and
 * below, and the pole hangs under it on the hand's side, its centre 16 design px in from the edge nearest the pet.
 *
 * The board is drawn at its design size and scaled from the top-left by the pet's magnification, so it stays in
 * proportion to the figure it hangs from.
 */
Item {
    id: paper

    /// The word on the board. One line: it never wraps and never elides.
    property string word: ""
    /// The pet's magnification, Pet.scale. The board grows and shrinks with it.
    required property real petScale
    /// The hand that holds the pole, "left" or "right". The pole stands on the edge of the board that faces the pet.
    required property string side

    readonly property SurfaceTokens tokens: SurfaceTokens {}
    /// The magnification the board is drawn at, the default ×3.01, where it is not scaled at all.
    readonly property real designScale: 3.01
    readonly property real boardScale: petScale / designScale
    readonly property int designShadowOffset: 5
    readonly property int designPoleHeight: 26
    /// The board's width at the design size, border included. The parent passes it, the same figure the placement uses.
    required property real designWidth
    /// The line the word sits on, 20 px tall at the design size (line-height 1 at 20 px).
    readonly property int wordLine: 20
    readonly property real designHeight: wordLine + 26 + 6
    readonly property real boardWidth: designWidth * boardScale
    readonly property real boardHeight: designHeight * boardScale
    readonly property real shadowOffset: designShadowOffset * boardScale
    readonly property real poleHeight: designPoleHeight * boardScale
    /// The word is 20 px at every length, as .sign in pet-review.html sets it; a longer word makes a wider board.
    readonly property int wordPixelSize: 20
    readonly property string wordFamily: Qt.fontFamilies().indexOf("Pixelify Sans") >= 0 ? "Pixelify Sans" : tokens.monoFamily

    width: boardWidth + shadowOffset
    height: boardHeight + poleHeight + shadowOffset

    // The design-size drawing, scaled from its top-left: the paper, word, border, shadow and pole shrink together.
    Item {
        width: board.width + paper.designShadowOffset
        height: board.height + paper.designPoleHeight + paper.designShadowOffset
        scale: paper.boardScale
        transformOrigin: Item.TopLeft

        Rectangle {
            x: paper.designShadowOffset
            y: paper.designShadowOffset
            width: board.width
            height: board.height
            color: "#2114141a"
        }

        Rectangle {
            id: board

            // Padding 18 across and 14 above, 12 below, plus the 3 px border on each side (.sign).
            width: paper.designWidth
            height: paper.designHeight
            color: "#f6efd9"
            border.width: 3
            border.color: "#26221b"
            radius: 2

            Text {
                id: wordText

                // The line is 20 px tall, as line-height 1 gives at 20 px, and sits inside the border and padding.
                anchors.horizontalCenter: parent.horizontalCenter
                y: 17
                height: paper.wordLine
                verticalAlignment: Text.AlignVCenter
                text: paper.word
                color: "#26221b"
                font.family: paper.wordFamily
                font.pixelSize: paper.wordPixelSize
                wrapMode: Text.NoWrap
            }
        }

        Rectangle {
            x: paper.side === "right" ? 14 : board.width - 18
            y: board.height
            width: 4
            height: paper.designPoleHeight
            color: "#26221b"
        }
    }
}
