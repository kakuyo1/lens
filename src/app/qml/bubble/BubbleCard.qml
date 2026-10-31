pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

/**
 * The explanation card the bubble draws: its shape, its glass and every row of the answer
 * (UI-SPEC.md section 4.3).
 *
 * Bubble.qml owns the window -- where it lands, how it fades, when it goes -- and hands the card
 * that window to read the answer from. The two are apart so Bubble.qml stays under the
 * 32768-character QML size cap (QML.md section 1); the card is the half that keeps growing.
 *
 * It takes the window rather than twenty separate properties, the way BubbleChainPanel.qml does:
 * the rows read the answer, the colours and the size scale off it, and the drag and hover handlers
 * here move it. The preview frame, the chain edges and the header actions are drawn over the card,
 * here because this is its coordinate space.
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

    /// Whether a verdict is on offer at all: the card is one the duty settled as offering one, and
    /// it has a word's status to show. The row's visibility, its opening and Bubble.qml's height
    /// hold all read this one value, so the three cannot disagree about the same card.
    readonly property bool verdictsAvailable: root.bubble.verdictButtons && root.bubble.status !== ""

    /// How far the verdict row reaches past the card's own height when it opens. The one-time hint
    /// is counted before the row has opened, so the window is already tall enough when it appears;
    /// Bubble.qml's height hold adds and subtracts this as the pointer comes and goes.
    readonly property int verdictRowHeight: verdictRow.openHeight
                                            + (!verdictRow.showHint && !Controller.settings.verdictHintSeen
                                               ? verdictRow.hintHeight : 0)

    /// The definition chain, so a step can be pushed into it from outside: Main.qml takes an answered
    /// step and a failed one through here.
    property alias chainSteps: chainPanel

    /// @brief Put the picture strip away.
    /// The window asks for this when the card goes: the strip belongs to the card that asked for it,
    /// and the next card is not the one that did.
    function collapseStrip() {
        senseRows.collapse();
    }

    /// @brief Point the open picture strip at @p query, or take it down when the sense has none.
    ///
    /// The strip follows the sense the Not-the-Sense? entry swapped in: the query moves, the duty
    /// re-asks under it, and the skeleton the strip already draws stands in while the new pictures
    /// come. A sense with no query has no strip to show, and it goes the way collapseStrip() takes it.
    function followSense(query) {
        if (senseRows.expandedIndex < 0)
            return;
        if (query === "")
            senseRows.collapse();
        else
            Controller.pictureDuty.request(query);
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

        // The handle a drag moves the window by; the timer below does the moving, and the two
        // reasons it is not the handler's own signal are in ShadowCard.qml. The flag is set on both
        // edges: setting it only on the way in left the countdown stopped for good.
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

        // Leaving takes effect only if it lasts: a pointer on the card's edge can read as an exit for
        // a frame or two while the verdict row animates open, and each false reading restarts the
        // countdown and collapses the row. Entering stays immediate, so the row opens the moment the
        // pointer arrives.
        // ponytail: a guard, not a diagnosis; raise the interval if it outlasts this.
        HoverHandler {
            // A preview does not report hover: the pointer is on a settings row, not holding an answer.
            enabled: !root.bubble.preview
            onHoveredChanged: {
                // Both readings, every flip: a real exit and something else moving `visible` look the
                // same in the controller's log, and these two tell them apart (TODO.md, the
                // bubble-hover jitter item).
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
                // A correction is typed as a word or a sentence, but its tag is a label: it takes the sentence's look.
                color: root.bubble.preview ? root.bubble.tokens.panel2
                     : root.bubble.type === "word" && !root.bubble.annotated ? (root.bubble.status === "known" ? root.bubble.tokens.panel2 : root.bubble.tokens.okBg)
                     : "transparent"
                border.width: root.bubble.preview || (root.bubble.type === "word" && !root.bubble.annotated) ? 0 : 1
                border.color: root.bubble.tokens.line

                Text {
                    id: tag
                    anchors.centerIn: parent
                    text: root.bubble.preview ? qsTr("Preview")
                         : root.bubble.correction === "spell" ? qsTr("Spelling")
                         : root.bubble.correction === "grammar" ? qsTr("Grammar")
                         : root.bubble.breakdown !== "" ? qsTr("Breakdown")
                         : root.bubble.type === "sentence" ? qsTr("Sentence")
                         : root.bubble.type === "word" ? (root.bubble.status === "known" ? qsTr("Known") : qsTr("New"))
                         : ""
                    color: root.bubble.preview ? root.bubble.tokens.body
                         : root.bubble.type === "sentence" || root.bubble.annotated ? root.bubble.tokens.body
                         : root.bubble.status === "known" ? root.bubble.tokens.body
                         : root.bubble.tokens.okText
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }

            // The breakdown takes the sentence's place: the sentence in its tints and the legend under it.
            BubbleBreakdown {
                visible: root.bubble.atRoot && root.bubble.breakdown !== ""
                width: parent.width
                bubble: root.bubble
            }

            BubbleTitleRow {
                id: wordRow
                visible: root.bubble.atRoot && !root.bubble.annotated && root.bubble.type !== "sentence" && root.bubble.type !== "translation"
                title: root.bubble.title
                ipa: root.bubble.type === "word" ? root.bubble.ipa : ""
                pos: root.bubble.type === "word" ? root.bubble.pos : ""
                // A word card carries a speaker; a sentence, a translation and a breakdown do not,
                // and neither does the settings preview -- a demonstration of a setting is not a
                // word the reader looked up (UI-SPEC.md 4.3).
                speakable: root.bubble.type === "word" && !root.bubble.preview
                speaking: root.bubble.speaking
                onSpeakRequested: root.bubble.speak()
                family: root.bubble.englishFont
                scale: root.bubble.fontScale
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                tokens: root.bubble.tokens
                // The globe and the cross stand at the right edge, where a bare title would run under them.
                headerReserve: 48
            }

            ChainSignpost {
                width: parent.width
                label: root.bubble.atRoot && root.bubble.type === "word" ? root.bubble.signpost : ""
                tokens: root.bubble.tokens
                scale: root.bubble.fontScale
            }

            // The lines the answer has not written yet (UI-SPEC.md 4.3). The card the reader's click
            // raised goes up before the model has a word ready, and these are what it shows in the
            // body's place until the first field lands.
            BubbleWaitingRows {
                width: parent.width
                bubble: root.bubble
            }

            SenseRows {
                id: senseRows
                senses: root.bubble.atRoot && root.bubble.type === "word" ? root.bubble.senses : []
                // Once a swapped sense has been drawn the rows draw whole (Bubble.qml): the
                // reader has read this card already, and a switched sense typed out a character
                // at a time would stand between the click and the sense it asked for.
                animated: root.bubble.revealAnimation && !root.bubble.senseSwappedOnce
                markColor: root.bubble.tokens.faint
                hoverColor: root.bubble.tokens.okBg
                onWordTapped: lemma => chainPanel.step(lemma)
                characterMs: root.bubble.revealCharacterMs
                englishColor: root.bubble.tokens.body
                translationColor: root.bubble.tokens.text
                separatorColor: root.bubble.tokens.line2
                family: root.bubble.englishFont
                scale: root.bubble.fontScale
            }

            // The definition line, and for a translation bubble the whole card: a translation has no
            // title or badge above it, so it takes the answer colour, not the definition's secondary
            // (UI-SPEC.md 4.3).
            ChainText {
                id: definitionText
                objectName: "definitionText"
                visible: root.bubble.atRoot && root.bubble.senses.length === 0 && root.bubble.english !== ""
                animated: root.bubble.revealAnimation
                characterMs: root.bubble.revealCharacterMs
                width: parent.width
                topPadding: 6
                text: root.bubble.english
                color: root.bubble.type === "translation" ? root.bubble.tokens.text : root.bubble.tokens.body
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

            // The mother-tongue line the held globe asked for (UI-SPEC.md 4.3): the meaning the
            // card is already carrying, said once in the reader's own words. It takes the set-off
            // form the origin and the example use, without their label -- a second telling of the
            // same answer is not a section of its own -- and it is drawn whole while the hold
            // lasts, never written out: nothing is arriving, it was held for. The settings
            // preview's mother-tongue row is the one place it shows without a hold: the line the
            // payload carries is what that row has to show (UI-SPEC.md 4.4).
            Column {
                id: peekBlock
                objectName: "peekBlock"
                visible: headerActions.peekVisible || (root.bubble.preview && root.bubble.peekLine !== "")
                width: parent.width
                topPadding: 6
                spacing: 5

                Rectangle {
                    objectName: "peekSeparator"
                    width: parent.width
                    height: 1
                    color: root.bubble.tokens.line2
                }

                Text {
                    id: peekText
                    objectName: "peekText"
                    width: parent.width
                    text: root.bubble.peekLine
                    color: root.bubble.tokens.muted
                    font.pixelSize: root.bubble.sizeAt(12)
                    lineHeight: 1.5
                    wrapMode: Text.WordWrap
                }
            }

            // The word's origin, under the explanation it belongs to. A hairline and a small label
            // set it apart from the definition: the reader asked for two different things and sees
            // two (UI-SPEC.md 4.3). The label is the card's label colour and weight; the sentence
            // sits one step down from the definition's colour, an origin being context for the
            // meaning rather than another meaning.
            // Nothing is drawn when there is none: an empty labelled slot would read as a model
            // that failed.
            // The gap above the rule is the 6 px a sense break uses, not the 12 first drawn:
            // measured on the rendered card, 12 put 24 px of blank between the definition and the
            // hairline, the widest gap on the card.
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
                    color: root.bubble.tokens.body
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
                    color: root.bubble.tokens.body
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
                    color: root.bubble.tokens.body
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
                    color: root.bubble.tokens.body
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

            // The Not-the-Sense? entry (UI-SPEC.md 4.3): the single-sense card's way onto the
            // word's next distinct common sense, for words with pictures and words without alike.
            // The offer and the word beside it are one press: the cycle swaps the sense in place,
            // and an open picture strip follows it. Hovering asks for the next sense before the
            // click does, so the click itself has nothing to wait for; the quiet the duty keeps
            // about it is the held peek's. The rows are the selector while multiple senses are
            // on, so the entry stands only when they are not.
            Item {
                id: senseCycleEntry
                objectName: "senseCycleEntry"
                visible: root.bubble.atRoot && !root.bubble.preview
                         && root.bubble.senseCycle === true
                         && Controller.settings.multiSense !== true
                width: parent.width
                // The 6 px a sense break uses above the pill's own 22, and none at all when the
                // duty has retired the entry: an exhausted cycle with one sense has nothing to offer.
                height: visible ? 28 : 0
                activeFocusOnTab: true

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 6
                    radius: root.bubble.tokens.radiusPill
                    color: cycleHover.hovered || senseCycleEntry.activeFocus ? root.bubble.tokens.panel2 : "transparent"
                    scale: cyclePress.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: root.bubble.tokens.motion.press
                            easing.type: root.bubble.tokens.motion.easing
                        }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 3
                    text: qsTr("Not the Sense?")
                    color: cycleHover.hovered || senseCycleEntry.activeFocus ? root.bubble.tokens.text : root.bubble.tokens.faint
                    font.pixelSize: 11
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 3
                    // The word the next press would swap in, with the way it points. Not drawn
                    // while the sense it names is still being asked for: an empty right side is
                    // the entry's waiting.
                    visible: root.bubble.senseHint !== ""
                    text: root.bubble.senseHint + " →"
                    color: root.bubble.tokens.okText
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }

                HoverHandler {
                    id: cycleHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (hovered)
                            Controller.explanationDuty.ensureNextSense();
                    }
                }
                TapHandler {
                    id: cyclePress
                    onTapped: {
                        root.bubble.readerActed();
                        Controller.explanationDuty.cycleSense();
                    }
                }
                Keys.onSpacePressed: {
                    root.bubble.readerActed();
                    Controller.explanationDuty.cycleSense();
                }
                Keys.onReturnPressed: {
                    root.bubble.readerActed();
                    Controller.explanationDuty.cycleSense();
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Not the Sense?")
                Accessible.onPressAction: {
                    root.bubble.readerActed();
                    Controller.explanationDuty.cycleSense();
                }
            }

            // The verdict buttons, drawn shut until the pointer is on the card (UI-SPEC.md 4.3).
            // A card the duty settled as offering no verdict draws no row at all -- the word
            // list's replay and the pet's answer alike -- so the pointer there is only for reading.
            BubbleVerdictRow {
                id: verdictRow
                visible: root.bubble.atRoot && root.verdictsAvailable
                tokens: root.bubble.tokens
                bubbleWindow: root.bubble
                lemma: root.bubble.title
                hovering: root.bubble.hovering
                available: root.verdictsAvailable
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
            id: headerActions
            anchors.fill: parent
            bubbleWindow: root.bubble
        }
    }
}
