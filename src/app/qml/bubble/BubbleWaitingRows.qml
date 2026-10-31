pragma ComponentBehavior: Bound

import QtQuick

/**
 * The waiting card's body (UI-SPEC.md 4.3): the lines the answer will be written on, breathed out in
 * the card's hairline token, and the one line that says what the card is waiting for.
 *
 * The card goes up at the reader's own click, before the model has written anything, so the click has
 * a surface on screen from its first frame (docs/adr/0146). What is drawn here is not the answer's
 * whole shape -- the blocks the reader's display switches keep off are not reserved -- but the body
 * every answer has: where its definition and its translation will stand.
 * The bars are the same idiom as the picture strip's skeleton (PictureStrip.qml): the interface's one
 * looping animation, `Tokens.motion.pulse`, and it rests at full opacity when motion is reduced.
 */
Column {
    id: root

    /// The bubble window this belongs to: its waiting flag, its colours and its size scale.
    required property var bubble

    readonly property bool waiting: root.bubble.waiting
    /// The line box a bar stands in: the definition's own leading, so the bars sit where its lines
    /// would and the answer takes their places without moving anything else on the card.
    readonly property real lineHeight: Math.round(root.bubble.sizeAt(13) * 1.55)
    /// The share of the card's width each bar takes: a paragraph's ragged edge, the last line short.
    readonly property var widths: [1.0, 1.0, 0.62]

    objectName: "waitingRows"
    visible: root.waiting
    topPadding: 6
    spacing: 0

    // The whole body breathes as one, and stops the moment the answer arrives -- the bars are gone
    // with the block, so nothing keeps looping over a card that has been filled.
    SequentialAnimation on opacity {
        loops: Animation.Infinite
        running: root.waiting && !root.bubble.tokens.motion.reduced
        NumberAnimation {
            to: 0.55
            duration: root.bubble.tokens.motion.pulse
            easing.type: root.bubble.tokens.motion.easing
        }
        NumberAnimation {
            to: 1.0
            duration: root.bubble.tokens.motion.pulse
            easing.type: root.bubble.tokens.motion.easing
        }
    }

    Repeater {
        model: root.widths

        delegate: Item {
            id: bar
            required property var modelData

            width: root.width
            height: root.lineHeight

            Rectangle {
                objectName: "waitingBar"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(bar.width * bar.modelData)
                height: root.bubble.sizeAt(10)
                radius: root.bubble.tokens.radiusPill
                color: root.bubble.tokens.line2
            }
        }
    }

    Text {
        objectName: "waitingLine"
        width: root.width
        topPadding: 4
        text: qsTr("Explaining…")
        color: root.bubble.tokens.muted
        font.pixelSize: root.bubble.sizeAt(11)
        lineHeight: 1.5
    }
}
