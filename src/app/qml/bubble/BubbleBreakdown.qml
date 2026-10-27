pragma ComponentBehavior: Bound

import QtQuick

/**
 * The breakdown card's body (UI-SPEC.md 4.3): the sentence as the answer split it, each role's run
 * tinted at 16%, and a legend naming the roles the sentence has. A correction card draws the same sentence
 * with the words the model changed underlined, and no legend. The legend row keeps one height through
 * pending, failed and ready, so the card does not jump when the answer lands.
 */
Column {
    id: root

    /// The bubble window this belongs to: its segments, its breakdown state and its tokens.
    required property var bubble

    readonly property bool ready: root.bubble.breakdown === "ready"
    /// A correction marks its changed words with an underline where a breakdown tints its roles.
    readonly property bool correcting: root.bubble.correction !== ""
    readonly property var roleOrder: ["S", "V", "C", "A"]
    readonly property var roleColors: ({
        S: root.bubble.tokens.roleS,
        V: root.bubble.tokens.roleV,
        C: root.bubble.tokens.roleC,
        A: root.bubble.tokens.roleA
    })
    readonly property var roleNames: ({
        S: qsTr("Subject"),
        V: qsTr("Verb"),
        C: qsTr("Complement"),
        A: qsTr("Adverbial")
    })

    /// The sentence as plain text: the segments in order, whitespace included.
    readonly property string plain: root.bubble.segments.map(segment => segment.text).join("")
    /// The role runs as the lines they break into: one entry per line of each run, with the first and
    /// last character index it covers and the line's top. A space has no glyph position of its own, so a
    /// run's box spans the spaces between its words and stops at its last word.
    readonly property var runBoxes: {
        sentence.layoutRevision;
        const boxes = [];
        let start = 0;
        for (const segment of root.bubble.segments) {
            const end = start + segment.text.length;
            if (segment.role !== "") {
                let line = null;
                for (let i = start; i < end; ++i) {
                    const piece = sentence.positions[i];
                    if (!piece)
                        continue;
                    if (line === null || piece.y !== line.y) {
                        line = {y: piece.y, first: i, last: i, role: segment.role};
                        boxes.push(line);
                    } else {
                        line.last = i;
                    }
                }
            }
            start = end;
        }
        return boxes;
    }

    /// @return The role colour at 16% over the bubble's own fill, as the hex a box is painted with.
    function tintOf(color) {
        const base = root.bubble.tokens.bubbleBg;
        const mix = (c, b) => Math.round((c * 0.16 + b * 0.84) * 255).toString(16).padStart(2, "0");
        return "#" + mix(color.r, base.r) + mix(color.g, base.g) + mix(color.b, base.b);
    }

    spacing: 10

    Item {
        width: root.width
        height: sentence.implicitHeight

        // Under the sentence, and faded in over it when the answer lands (UI-SPEC.md 4.17). A Text cannot
        // paint a span's background in Qt Quick, so each run is a box laid on the glyph positions the
        // sentence was measured with, the way ChainText places its rules.
        Item {
            objectName: "tintLayer"
            anchors.fill: parent
            opacity: root.ready ? 1 : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: root.bubble.tokens.motion.pop
                    easing.type: root.bubble.tokens.motion.easing
                }
            }

            Repeater {
                model: root.correcting ? [] : root.runBoxes

                delegate: Rectangle {
                    id: box
                    required property var modelData

                    objectName: "tintBox"
                    x: sentence.positions[box.modelData.first].x - 2
                    y: sentence.positions[box.modelData.first].y + sentence.topPadding + sentence.lineBaseline - fontProbe.baselineOffset - 1
                    width: sentence.measureWidth(sentence.text.substring(box.modelData.first, box.modelData.last + 1)) + 4
                    height: fontProbe.implicitHeight + 2
                    radius: root.bubble.tokens.radiusField
                    color: root.tintOf(root.roleColors[box.modelData.role])
                }
            }

            // A correction's changed words: a 1 px rule under each word's baseline, whole pixels so it stays crisp.
            Repeater {
                model: root.correcting ? root.runBoxes : []

                delegate: Rectangle {
                    id: mark
                    required property var modelData

                    objectName: "underlineBox"
                    x: Math.round(sentence.positions[mark.modelData.first].x)
                    y: Math.round(sentence.positions[mark.modelData.first].y + sentence.topPadding + sentence.lineBaseline + 2)
                    width: Math.round(sentence.measureWidth(sentence.text.substring(mark.modelData.first, mark.modelData.last + 1)))
                    height: 1
                    color: root.bubble.tokens.warn
                }
            }
        }

        RevealText {
            id: sentence
            objectName: "breakdownSentence"
            width: parent.width
            height: implicitHeight
            animated: false
            text: root.plain
            topPadding: 6
            family: root.bubble.englishFont
            pixelSize: root.bubble.sizeAt(13)
            lineHeight: 1.9
            color: root.bubble.tokens.text
        }

        // The glyph box the tint boxes are measured from: the sentence's own font at its natural height.
        Text {
            id: fontProbe
            visible: false
            text: "A"
            font.family: root.bubble.englishFont
            font.pixelSize: root.bubble.sizeAt(13)
        }
    }

    Item {
        width: root.width
        height: 16

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Repeater {
                model: root.ready ? root.roleOrder.filter(role => root.bubble.roles.indexOf(role) >= 0) : []

                delegate: Row {
                    id: legendItem
                    required property string modelData

                    spacing: 5

                    Rectangle {
                        y: (parent.height - height) / 2
                        width: 9
                        height: 9
                        radius: 4.5
                        color: root.roleColors[legendItem.modelData]
                    }

                    Text {
                        text: root.roleNames[legendItem.modelData]
                        color: root.bubble.tokens.muted
                        font.pixelSize: root.bubble.sizeAt(11)
                    }
                }
            }
        }

        Text {
            visible: !root.ready
            anchors.verticalCenter: parent.verticalCenter
            text: root.bubble.breakdown === "failed"
                  ? (root.correcting ? qsTr("This could not be corrected.") : qsTr("This sentence could not be broken down."))
                  : (root.correcting ? qsTr("Correcting…") : qsTr("Breaking down…"))
            color: root.bubble.tokens.muted
            font.pixelSize: root.bubble.sizeAt(11)
        }
    }
}
