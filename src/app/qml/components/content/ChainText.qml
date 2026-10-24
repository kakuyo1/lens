pragma ComponentBehavior: Bound

import QtQuick

/**
 * One field of an answer whose words the definition chain offers (UI-SPEC.md 4.3).
 *
 * A thin overlay on RevealText rather than a change to it: the field still writes itself out one character at a time,
 * and the words above the reader's band get a dotted line, a hover tint and a tap. The text itself is not redrawn --
 * the characters stay where RevealText put them, because a second drawing would kern its letters differently and land
 * a pixel or two off; the same reason marks a link by line and tint rather than a colour change.
 *
 * The positions are read off RevealText's own layout through `layoutRevision`, so a chunk that extends the answer
 * re-lays the token it touched and the line follows it.
 */
Item {
    id: root

    /// The whole field, as far as it has arrived. RevealText's own property, through this one.
    property string text: ""
    /// The words to offer, as the duty found them: one map per word with `start`, `end` (QString
    /// indices, end exclusive) and `lemma`.
    property var spans: []
    property int pixelSize: 13
    property string family: ""
    property color color: "black"
    /// The multiple the field is drawn at, from the reader's font-size setting (UI-SPEC.md 4.4).
    property real lineHeight: 1.0
    /// Whether the field is written out a character at a time.
    property bool animated: true
    /// The pace the field is written at, in real milliseconds (RevealText).
    property int characterMs: 35
    property real topPadding: 0
    property real bottomPadding: 0
    /// The dotted line's colour, and the tint a hovered word takes.
    property color markColor: "gray"
    property color hoverColor: "transparent"

    /// @brief The word the reader tapped, as the definition wrote it.
    signal wordTapped(string lemma)

    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight

    /// True while the field is still being written out, which is what holds the card's countdown.
    readonly property bool revealing: body.revealing

    RevealText {
        id: body
        x: 0
        y: 0
        // The caller gives this its column's width; the field wraps to it exactly as it did before.
        width: root.width
        text: root.text
        pixelSize: root.pixelSize
        color: root.color
        family: root.family
        lineHeight: root.lineHeight
        animated: root.animated
        characterMs: root.characterMs
        topPadding: root.topPadding
        bottomPadding: root.bottomPadding
    }

    /// One word of the field that the chain offers: its line, its hover and its tap.
    Repeater {
        model: root.spans

        delegate: Item {
            id: link

            required property var modelData
            readonly property string lemma: link.modelData.lemma
            /// The first character of the word, re-read whenever the layout changes: positions is
            /// edited in place as an extending chunk arrives, and a var property does not announce.
            readonly property var first: {
                body.layoutRevision;
                return body.positions[link.modelData.start] || null;
            }
            /// The word's own width, measured whole. The run up to it is what places it, and the
            /// letters after it are not kerned together, so measuring the word is the only reading
            /// that matches where RevealText put it.
            readonly property real runWidth: {
                body.layoutRevision;
                return spanMetrics.advanceWidth;
            }
            /// A word the reveal has not reached yet is not a word the reader can read, so its line
            /// waits with it.
            readonly property bool settled: link.first !== null && body.revealed > link.modelData.end

            x: link.first ? link.first.x : 0
            y: link.first ? link.first.y + body.topPadding : 0
            // Behind the text: the tint is opaque, and drawn over the word it would hide it.
            z: -1
            width: link.runWidth
            height: body.lineBaseline + 3
            visible: link.settled

            // The word itself is the link's name -- spanMetrics already measures exactly that
            // substring. No tab stop or ring: the bubble's window never takes focus (UI-SPEC.md 4.3).
            Accessible.role: Accessible.Button
            Accessible.name: spanMetrics.text
            Accessible.onPressAction: root.wordTapped(link.lemma)

            Rectangle {
                x: -1
                y: -1
                width: link.width + 2
                height: link.height + 2
                radius: 3
                color: root.hoverColor
                opacity: hover.hovered ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: 120 }
                }
            }

            // The line is a row of one-pixel dots rather than a dashed rectangle: a dash pattern is
            // measured in whole pixels of the rectangle, and a short word's line would end up with
            // one dot or none.
            Repeater {
                model: Math.max(1, Math.ceil(link.width / 3))
                delegate: Rectangle {
                    required property int index
                    x: index * 3
                    y: body.lineBaseline + 1
                    width: 1
                    height: 1
                    color: root.markColor
                }
            }

            // Written only when the field names one, the way RevealText does it: a Text told
            // `font.family: ""` falls back to whatever the font database hands an unmatched
            // request, and the word would be measured in the wrong face.
            Binding {
                target: spanMetrics
                property: "font.family"
                value: body.family
                when: body.family !== ""
            }

            TextMetrics {
                id: spanMetrics
                font.pixelSize: body.pixelSize
                font.weight: body.weight
                text: {
                    body.layoutRevision;
                    return link.modelData.start >= 0 && link.modelData.end <= root.text.length
                               ? root.text.substring(link.modelData.start, link.modelData.end)
                               : "";
                }
            }

            HoverHandler {
                id: hover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: root.wordTapped(link.lemma)
            }
        }
    }
}
