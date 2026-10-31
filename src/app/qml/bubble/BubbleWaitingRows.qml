pragma ComponentBehavior: Bound

import QtQuick

/**
 * The space the answer has not written yet (UI-SPEC.md 4.3): the line that says what the card is
 * waiting for, and under it the lines the answer will be written on, breathed out in the card's
 * hairline token.
 *
 * The card goes up at the reader's own click, before the model has written anything, and keeps the
 * height its answer is expected to reach until the whole of it lands (docs/adr/0146). So what is
 * drawn here is not a fixed picture but the remainder: the block stands under the rows the answer has
 * already written, and as each field lands the space left for these lines shrinks around them. The
 * card itself never moves while that happens -- which is the whole point.
 *
 * The lines are the same idiom as the picture strip's skeleton (PictureStrip.qml): the interface's one
 * looping animation, `Tokens.motion.pulse`, and it rests at full opacity when motion is reduced.
 */
Item {
    id: root

    /// The bubble window this belongs to: its colours, its size scale and whether it is still waiting.
    required property var bubble

    /// The line box one standing line takes: the definition's own leading, so the lines sit where its
    /// lines will.
    readonly property real lineHeight: Math.round(root.bubble.sizeAt(13) * 1.55)
    /// The height the caption claims: the whole time the answer is on its way, so the lines under it
    /// neither appear nor shift when the first field lands.
    readonly property real captionHeight: root.bubble.holding ? caption.height : 0
    /// How many lines fit in what is left. The last line of a paragraph is short, so every third is.
    readonly property int lines: Math.max(0, Math.floor((root.height - root.captionHeight) / root.lineHeight))

    objectName: "waitingRows"
    // Nothing is drawn, and nothing breathes, once the answer has filled the space it was reserved in.
    visible: root.height > 0
    clip: true

    // The whole body breathes as one, and stops with the space it stands in.
    SequentialAnimation on opacity {
        loops: Animation.Infinite
        running: root.visible && !root.bubble.tokens.motion.reduced
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

    Text {
        id: caption
        objectName: "waitingLine"
        visible: root.bubble.holding
        width: root.width
        topPadding: 4
        text: qsTr("Explaining…")
        color: root.bubble.tokens.muted
        font.pixelSize: root.bubble.sizeAt(11)
        lineHeight: 1.5
    }

    Repeater {
        model: root.lines

        delegate: Rectangle {
            id: line
            required property int index

            objectName: "waitingBar"
            x: 0
            y: root.captionHeight + line.index * root.lineHeight + Math.round((root.lineHeight - height) / 2)
            width: Math.round(root.width * (line.index % 3 === 2 ? 0.62 : 1.0))
            height: root.bubble.sizeAt(10)
            radius: root.bubble.tokens.radiusPill
            color: root.bubble.tokens.line2
        }
    }
}
