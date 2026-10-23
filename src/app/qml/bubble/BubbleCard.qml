pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

/**
 * The explanation card the bubble draws: its shape, its glass and every row of the answer
 * (UI-SPEC.md section 4.3).
 *
 * Bubble.qml owns the window -- where it lands, how it fades, when it goes -- and hands the card
 * that window to read the answer from. The two are apart because the card is the half that keeps
 * growing: the sense rows, the chain and the picture strip all arrived here, and Bubble.qml was one
 * change away from the 32768-character ceiling QML.md section 1 records.
 *
 * It takes the window rather than twenty separate properties, the way BubbleChainPanel.qml does:
 * the rows read the answer, the colours and the size scale off it, and the drag and hover handlers
 * here are what move it. The preview frame, the chain edges and the header actions are drawn over
 * the card rather than in it, and they are here beside it because this is its coordinate space.
 */
Item {
    id: root

    /// The bubble window this card is drawn in: its answer, its colours and its size scale.
    required property var bubble

    implicitWidth: 270
    implicitHeight: card.height

    /// True while any field is still being written out: what holds the countdown (Bubble.qml).
    readonly property bool revealing: definitionText.revealing || chineseText.revealing
                                      || translationText.revealing || wordRow.revealing
                                      || etymologyText.revealing || exampleText.revealing
                                      || senseRows.revealing || chainPanel.revealing

    /// How far the verdict row reaches past the card's own height when it opens. Bubble.qml's height
    /// hold adds and subtracts it as the pointer comes and goes.
    readonly property int verdictRowHeight: verdictRow.openHeight

    /// The definition chain, so a step can be pushed into it from outside: Main.qml takes an answered
    /// step and a failed one through here.
    property alias chainSteps: chainPanel

    /// @brief Put the picture strip away.
    /// The window asks for this when the card goes: the strip belongs to the card that asked for it,
    /// and the next card is not the one that did.
    function collapseStrip() {
        senseRows.collapse();
    }

    // The shadow is cast from a shape-only copy of the card, never from the card itself:
    // MultiEffect draws its source through an offscreen texture, and at this monitor's
    // 125% scale that texture is resampled. See SelectionBar.qml for the measurement.
    Rectangle {
        id: shadowShape
        anchors.fill: card
        radius: card.radius
        color: root.bubble.tokens.glassMode === "" ? root.bubble.tokens.bubbleBg : "transparent"
        visible: false
    }

    MultiEffect {
        // Placed and sized by the effect, from its source -- see ShadowCard.qml.
        x: card.x
        y: card.y
        source: shadowShape
        shadowEnabled: true
        shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, root.bubble.tokens.dark ? 0.75 : 0.26)
        shadowBlur: 0.9
        shadowVerticalOffset: 8
        blurMax: 44
    }

    // The mark that says this card is a demo (UI-SPEC.md 4.4).
    PreviewFrame {
        visible: root.bubble.preview
        card: card
        cardRadius: root.bubble.tokens.radiusCard
        strokeColor: root.bubble.tokens.muted
    }

    ChainEdges {
        card: card
        shown: !root.bubble.atRoot
        edgeColor: root.bubble.tokens.line
    }

    Rectangle {
        id: card
        width: parent.implicitWidth
        height: column.height + 26
        radius: root.bubble.tokens.radiusCard
        color: root.bubble.tokens.glassMode === "" ? root.bubble.tokens.bubbleBg : "transparent"
        border.width: 1
        border.color: root.bubble.tokens.glassMode === "" ? root.bubble.tokens.bubbleBorder : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: root.bubble.glass
            mode: root.bubble.tokens.glassMode
            radius: card.radius
        }

        // The handle a drag moves the window by; the timer below does the moving, and the
        // two reasons it is not the handler's own signal are in ShadowCard.qml. The flag
        // brackets the drag on both edges: setting it only on the way in left the countdown
        // stopped for good, and the bubble never went away again.
        DragHandler {
            id: mover
            target: null
            // A preview belongs to the row under the pointer; a real card is the reader's to move.
            enabled: !root.bubble.preview

            property point grabCursor: Qt.point(0, 0)
            property point grabWindow: Qt.point(0, 0)

            function place() {
                const at = Controller.cursorPos();
                root.bubble.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                root.bubble.y = Math.round(grabWindow.y + at.y - grabCursor.y);
            }

            onActiveChanged: {
                root.bubble.dragging = active;
                if (active) {
                    grabCursor = Controller.cursorPos();
                    grabWindow = Qt.point(root.bubble.x, root.bubble.y);
                } else {
                    place();
                }
            }
        }

        Timer {
            interval: 16
            repeat: true
            running: mover.active
            onTriggered: mover.place()
        }

        // Leaving takes effect only if it lasts: a pointer sitting on the edge of the
        // card can read as an exit for a frame or two while the verdict row animates open,
        // and each false reading restarts the countdown and collapses the row again.
        // Entering stays immediate, so the row still opens the moment the pointer arrives.
        // ponytail: a guard, not a diagnosis; raise the interval if it outlasts this.
        HoverHandler {
            // A preview does not report hover: the pointer is on a settings row, not holding an answer.
            enabled: !root.bubble.preview
            onHoveredChanged: {
                // Both readings, every flip: a real exit and something else moving `visible`
                // look the same in the log the controller keeps, and these two are what tells
                // them apart (TODO.md, the bubble-hover jitter item).
                root.bubble.lastHoverCursor = Controller.cursorPos();
                const at = card.mapToGlobal(0, 0);
                root.bubble.lastHoverCard = Qt.rect(at.x, at.y, card.width, card.height);
                if (hovered) {
                    hoverSettle.stop();
                    root.bubble.hovering = true;
                } else {
                    hoverSettle.restart();
                }
            }
        }

        Timer {
            id: hoverSettle
            interval: 140
            onTriggered: root.bubble.hovering = false
        }

        Column {
            id: column
            x: 16
            y: 14
            width: parent.width - 32
            spacing: 0

            // The chain takes the card's place while a step is open.
            BubbleChainPanel {
                id: chainPanel
                visible: !root.bubble.atRoot
                width: parent.width
                bubble: root.bubble
            }

            // A word bubble carries the learning verdict; a sentence bubble carries its type
            // label. An entity bubble carries neither: a named thing is not a learning item,
            // so it is drawn as title plus encyclopaedic definition with no badge.
            Rectangle {
                // A preview wears this row in place of a learning verdict (UI-SPEC.md 4.4).
                visible: root.bubble.atRoot && (root.bubble.preview || root.bubble.type === "word" || root.bubble.type === "sentence")
                width: tag.width + 16
                height: 17
                radius: root.bubble.tokens.radiusPill
                color: root.bubble.preview ? root.bubble.tokens.panel2
                     : root.bubble.type === "word" ? (root.bubble.status === "known" ? root.bubble.tokens.panel2 : root.bubble.tokens.okBg)
                     : "transparent"
                border.width: root.bubble.preview || root.bubble.type === "word" ? 0 : 1
                border.color: root.bubble.tokens.line

                Text {
                    id: tag
                    anchors.centerIn: parent
                    text: root.bubble.preview ? qsTr("Preview")
                         : root.bubble.type === "sentence" ? qsTr("Sentence")
                         : root.bubble.type === "word" ? (root.bubble.status === "known" ? qsTr("Known") : qsTr("New"))
                         : ""
                    color: root.bubble.preview ? root.bubble.tokens.muted
                         : root.bubble.type === "sentence" ? root.bubble.tokens.muted
                         : root.bubble.status === "known" ? root.bubble.tokens.muted
                         : root.bubble.tokens.okText
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }

            BubbleTitleRow {
                id: wordRow
                visible: root.bubble.atRoot && root.bubble.type !== "sentence" && root.bubble.type !== "translation"
                title: root.bubble.title
                ipa: root.bubble.type === "word" ? root.bubble.ipa : ""
                family: root.bubble.englishFont
                scale: root.bubble.fontScale
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                tokens: root.bubble.tokens
            }

            ChainSignpost {
                width: parent.width
                label: root.bubble.atRoot && root.bubble.type === "word" ? root.bubble.signpost : ""
                tokens: root.bubble.tokens
                scale: root.bubble.fontScale
            }

            SenseRows {
                id: senseRows
                senses: root.bubble.atRoot && root.bubble.type === "word" ? root.bubble.senses : []
                markColor: root.bubble.tokens.faint
                hoverColor: root.bubble.tokens.okBg
                onWordTapped: lemma => chainPanel.step(lemma)
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                englishColor: root.bubble.tokens.muted
                translationColor: root.bubble.tokens.text
                separatorColor: root.bubble.tokens.line2
                family: root.bubble.englishFont
                scale: root.bubble.fontScale
            }

            // The definition line, and for a translation bubble the whole card: a translation
            // has no title and no badge above it, so drawing it in the definition's secondary
            // colour would leave the answer looking like a footnote to nothing (UI-SPEC.md 4.3).
            ChainText {
                id: definitionText
                visible: root.bubble.atRoot && root.bubble.senses.length === 0 && root.bubble.english !== ""
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                width: parent.width
                topPadding: 6
                text: root.bubble.english
                color: root.bubble.type === "translation" ? root.bubble.tokens.text : root.bubble.tokens.muted
                family: root.bubble.englishFont
                pixelSize: root.bubble.sizeAt(13)
                lineHeight: 1.55
                spans: root.bubble.englishSpans
                markColor: root.bubble.tokens.faint
                hoverColor: root.bubble.tokens.okBg
                onWordTapped: lemma => chainPanel.step(lemma)
            }

            RevealText {
                id: chineseText
                visible: root.bubble.atRoot && root.bubble.senses.length === 0 && root.bubble.chinese !== ""
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                width: parent.width
                topPadding: 5
                text: root.bubble.chinese
                color: root.bubble.tokens.text
                pixelSize: root.bubble.sizeAt(12)
                lineHeight: 1.5
            }

            RevealText {
                id: translationText
                visible: root.bubble.atRoot && root.bubble.senses.length === 0 && root.bubble.translation !== ""
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                width: parent.width
                topPadding: 5
                text: root.bubble.translation
                color: root.bubble.tokens.text
                pixelSize: root.bubble.sizeAt(12)
                lineHeight: 1.5
            }

            // The word's origin, under the explanation it belongs to. A hairline and a small
            // label are what keep it from reading as one more line of the definition: the
            // reader asked for two different things and has to see two different things
            // (UI-SPEC.md 4.3). The label is the card's label colour and weight; the sentence under
            // it sits one step down from the definition's colour, because an origin is context
            // for the meaning rather than another meaning.
            // Nothing at all is drawn when there is none: an empty labelled slot would read as
            // a model that failed rather than as a word with no origin to give.
            // The gap above the rule is the 6 px a sense break uses, not the 12 this block
            // was drawn with first: measured on the rendered card, that put 24 px of blank
            // between the definition and the hairline -- the widest gap on the card, in the
            // one place that is only introducing an aside.
            Column {
                id: etymologyBlock
                objectName: "etymologyBlock"
                visible: root.bubble.atRoot && root.bubble.etymology !== ""
                width: parent.width
                topPadding: 6
                spacing: 5

                Rectangle {
                    objectName: "etymologySeparator"
                    width: parent.width
                    height: 1
                    color: root.bubble.tokens.line2
                }

                Text {
                    objectName: "etymologyLabel"
                    text: qsTr("Etymology")
                    color: root.bubble.tokens.muted
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }

                RevealText {
                    id: etymologyText
                    objectName: "etymologyText"
                    animated: root.bubble.revealAnimation
                    characterMs: root.bubble.revealCharacterMs
                    width: parent.width
                    text: root.bubble.etymology
                    color: root.bubble.tokens.muted
                    family: root.bubble.englishFont
                    pixelSize: root.bubble.sizeAt(12)
                    lineHeight: 1.5
                }
            }

            // The example sentence sits under the origin in the same set-off form (UI-SPEC.md 4.3). Drawn
            // only when there is one: an empty block would read as a model that failed to give one.
            Column {
                id: exampleBlock
                objectName: "exampleBlock"
                visible: root.bubble.atRoot && root.bubble.example !== ""
                width: parent.width
                topPadding: 6
                spacing: 5

                Rectangle {
                    objectName: "exampleSeparator"
                    width: parent.width
                    height: 1
                    color: root.bubble.tokens.line2
                }

                Text {
                    objectName: "exampleLabel"
                    text: qsTr("Example")
                    color: root.bubble.tokens.muted
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }

                RevealText {
                    id: exampleText
                    objectName: "exampleText"
                    animated: root.bubble.revealAnimation
                    characterMs: root.bubble.revealCharacterMs
                    width: parent.width
                    text: root.bubble.example
                    color: root.bubble.tokens.muted
                    family: root.bubble.englishFont
                    pixelSize: root.bubble.sizeAt(12)
                    lineHeight: 1.5
                }
            }

            ChainCollocations {
                width: parent.width
                words: root.bubble.atRoot && root.bubble.type === "word" ? root.bubble.collocations : []
                tokens: root.bubble.tokens
                scale: root.bubble.fontScale
            }

            // The verdict buttons, drawn shut until the pointer is on the card (UI-SPEC.md 4.3).
            BubbleVerdictRow {
                id: verdictRow
                visible: root.bubble.atRoot && root.bubble.verdictButtons
                tokens: root.bubble.tokens
                lemma: root.bubble.title
                hovering: root.bubble.hovering
                status: root.bubble.status
            }

            Text {
                // A bubble with no countdown has no seconds to count, and the line's absence is
                // what says it will wait.
                visible: root.bubble.countsDown
                width: parent.width
                topPadding: 9
                // The remaining seconds, not the setting that seeded them: the count stands
                // still while the pointer holds the bubble and steps down again when it goes.
                text: qsTr("Disappears in %1s").arg(Math.ceil(root.bubble.remainingMs / 1000))
                color: root.bubble.tokens.faint
                font.pixelSize: 11
            }

        }

        BubbleHeaderActions {
            anchors.fill: parent
            bubbleWindow: root.bubble
        }
    }
}
