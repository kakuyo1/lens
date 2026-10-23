pragma ComponentBehavior: Bound

import QtQuick

/**
 * The definition chain under the card (UI-SPEC.md 4.3), drawn inside the card that is already standing.
 *
 * The card has no chain of its own; this panel holds it. A step is one tap on a word of the answer:
 * the panel asks the duty, takes the answer, and shows where the reader is (a back arrow and the
 * words they came through). While a step is open the card hides its answer and this panel takes its
 * place -- same window, width and glass; a step is not a second card.
 *
 * A word the trail already holds is not offered again, and the steps stop at maxSteps, so a
 * definition that names its own words cannot loop.
 *
 * The header's right side is left free: the card's close cross and language shortcut are drawn over this row from
 * BubbleHeaderActions, and a breadcrumb that ran under them would be unreadable.
 */
Item {
    id: root

    /// The card this chain stands on: its colours, faces and reveal pace (Bubble.qml).
    required property var bubble

    /// The steps down from the card's own word, in order. The last one is what is drawn.
    property var chain: []
    /// The word a step is waiting for, or empty. Drawn as one line under the answer.
    property string pending: ""
    /// Whether the last step failed. Drawn where its answer would have gone.
    property bool failed: false
    /// The answer the chain was last kept for, so a refresh of the same answer keeps the steps.
    property string answerKey: ""

    /// The most steps a reader can take down from the card. Past it a definition offers no links.
    readonly property int maxSteps: 3
    readonly property var tokens: root.bubble.tokens
    readonly property string family: root.bubble.englishFont
    readonly property real fontScale: root.bubble.fontScale

    /// Whether the card is at its own answer rather than a step down it.
    readonly property bool atRoot: root.chain.length === 0
    /// The step being drawn, or nothing while the chain is empty.
    readonly property var entry: root.chain.length > 0 ? root.chain[root.chain.length - 1] : null
    /// The words from the card's own down to this step, which is what the breadcrumb reads.
    readonly property var trail: [root.bubble.title].concat(root.chain.map(level => level.title))
    /// The links the drawn definition offers: none for a word already on the trail, and none past maxSteps.
    readonly property var stepSpans: {
        if (root.entry === null || root.chain.length >= root.maxSteps)
            return [];
        const seen = root.trail.map(word => String(word).toLowerCase());
        return (root.entry.spans || []).filter(span => seen.indexOf(span.lemma) < 0);
    }
    /// Whether a step is still being written out. Holds the card's countdown.
    readonly property bool revealing: wordRow.revealing || definitionText.revealing

    /// @brief Forget the chain: a new card, or the same card with a different answer under it.
    function reset() {
        root.chain = [];
        root.pending = "";
        root.failed = false;
    }

    /// @brief A card's answer has arrived. A refresh of the same answer keeps the steps; anything else starts over.
    function refresh(payload) {
        const key = [payload.en, payload.zh, payload.translation].join("|");
        if (payload.sameCard !== true || key !== root.answerKey)
            root.reset();
        root.answerKey = key;
    }

    /// @brief The reader tapped a word of the answer. A tap while a step is in the air is not answered.
    function step(lemma) {
        if (root.pending !== "" || root.chain.length >= root.maxSteps)
            return;
        root.failed = false;
        root.pending = lemma;
        Controller.explanationDuty.explainChainWord(lemma);
    }

    /// @brief A step's answer. One for another word is not the step waited for: the reader went back meanwhile.
    function receive(answer) {
        if (answer.title !== root.pending)
            return;
        root.pending = "";
        root.chain = root.chain.concat([answer]);
    }

    /// @brief A step that could not be answered, for the word it was asked for.
    function fail(lemma) {
        if (lemma !== root.pending)
            return;
        root.pending = "";
        root.failed = true;
    }

    /// @brief Go back up one level. A step still in the air is given up.
    function back() {
        root.chain = root.chain.slice(0, -1);
        root.pending = "";
        root.failed = false;
    }

    implicitWidth: 270
    implicitHeight: column.height

    Column {
        id: column
        x: 0
        y: 0
        width: parent.width
        spacing: 0

        // The header: back on the left, the trail after it. The card's own close cross and language
        // shortcut sit over the right of this row, so the breadcrumb stops short of them.
        Item {
            width: parent.width
            height: 22

            IconButton {
                id: backButton
                anchors.left: parent.left
                anchors.leftMargin: -6
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                source: "qrc:/icons/ui-back.svg"
                color: root.tokens.faint
                onTapped: root.back()
            }

            Text {
                id: breadcrumb
                anchors.left: backButton.right
                anchors.leftMargin: 4
                anchors.right: parent.right
                anchors.rightMargin: 96
                anchors.verticalCenter: parent.verticalCenter
                // Nothing to say at the root: the panel is hidden there, and a trail that held the
                // card's own title would be a second Text reading as that title.
                text: root.atRoot ? "" : root.trail.join(" › ")
                color: root.tokens.faint
                font.pixelSize: Math.round(10.5 * root.fontScale)
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        BubbleTitleRow {
            id: wordRow
            visible: root.entry !== null
            width: parent.width
            title: root.entry ? root.entry.title : ""
            ipa: root.entry ? root.entry.ipa : ""
            family: root.family
            scale: root.fontScale
            animated: root.bubble.revealAnimation
            characterMs: root.bubble.revealCharacterMs
            tokens: root.tokens
        }

        ChainSignpost {
            width: parent.width
            label: root.entry ? (root.entry.signpost || "") : ""
            tokens: root.tokens
            scale: root.fontScale
        }

        ChainText {
            id: definitionText
            visible: root.entry !== null && (root.entry.text || "") !== ""
            width: parent.width
            topPadding: 9
            text: root.entry ? (root.entry.text || "") : ""
            spans: root.stepSpans
            pixelSize: Math.round(13 * root.fontScale)
            family: root.family
            color: root.tokens.muted
            lineHeight: 1.55
            animated: root.bubble.revealAnimation
            characterMs: root.bubble.revealCharacterMs
            markColor: root.tokens.faint
            // The card's green tint for a new word: the chain is the same affordance.
            hoverColor: root.tokens.okBg
            onWordTapped: lemma => root.step(lemma)
        }

        ChainCollocations {
            width: parent.width
            words: root.entry ? (root.entry.collocations || []) : []
            tokens: root.tokens
            scale: root.fontScale
        }

        // What the chain is doing, under the answer: one line naming the word, since a second tap
        // while the first is in the air is not answered and the reader would otherwise press a word
        // that does nothing.
        Text {
            id: pendingLine
            visible: root.pending !== "" && !root.failed
            width: parent.width
            topPadding: 9
            text: qsTr("Explaining %1…").arg(root.pending)
            color: root.tokens.faint
            font.pixelSize: 11
        }

        // The failure, in the same place. A notice would take the whole card away over one word
        // inside it, and the definition the reader was reading is still there.
        Text {
            id: failedLine
            visible: root.failed
            width: parent.width
            topPadding: 9
            text: qsTr("This word could not be explained.")
            color: root.tokens.faint
            font.pixelSize: 11
        }
    }
}
