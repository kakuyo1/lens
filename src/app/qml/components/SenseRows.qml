pragma ComponentBehavior: Bound

import QtQuick

/**
 * The senses of a word, one two-line row each: the English line and the reader's own language
 * under it, with a hairline between rows (UI.md 4.3).
 *
 * A component rather than part of `Bubble.qml` because that file sits at the repository's QML size
 * cap (docs/QML.md section 1), not because this is shared: it is one surface's block lifted out
 * whole. The root is a Repeater so the rows are laid out in the column that holds this, exactly as
 * they were before the move.
 *
 * `revealing` is the block's own aggregate, so the card can hold its countdown while any line of
 * any row is still being written out.
 */
Repeater {
    id: root

    /// The senses to draw, in order; each is an object with an `en` and the reader's own `text`.
    required property var senses
    /// Whether the lines are written out a character at a time (RevealText), or arrive whole.
    required property bool animated
    /// The pace the lines are written at, in real milliseconds (RevealText).
    property int characterMs: 35
    required property color englishColor
    required property color translationColor
    required property color separatorColor

    /// True while any line of any row is still being written out.
    readonly property bool revealing: {
        // qmllint disable missing-property
        // A row's own `revealing`; the repeater hands its delegates back as QQuickItem.
        for (let i = 0; i < root.count; ++i) {
            const row = root.itemAt(i);
            if (row && row.revealing)
                return true;
        }
        // qmllint enable missing-property
        return false;
    }

    model: root.senses

    delegate: Column {
        id: senseRow
        required property int index
        required property var modelData
        // The column this block sits in lays the rows out, and each row is as wide as it is.
        width: parent ? parent.width : 0
        spacing: 0

        /// What this row contributes to the block's reveal.
        readonly property bool revealing: senseEnglishText.revealing
                                           || senseTranslationText.revealing

        Rectangle {
            objectName: "senseSeparator"
            visible: senseRow.index > 0
            width: parent.width
            height: 1
            color: root.separatorColor
        }

        RevealText {
            id: senseEnglishText
            objectName: "senseEnglish"
            visible: text !== ""
            animated: root.animated
            characterMs: root.characterMs
            width: parent.width
            topPadding: 6
            bottomPadding: senseRow.modelData.text === "" ? 6 : 0
            text: senseRow.modelData.en || ""
            color: root.englishColor
            pixelSize: 13
            lineHeight: 1.55
        }

        RevealText {
            id: senseTranslationText
            objectName: "senseTranslation"
            visible: text !== ""
            animated: root.animated
            characterMs: root.characterMs
            width: parent.width
            topPadding: 5
            bottomPadding: 6
            text: senseRow.modelData.text || ""
            color: root.translationColor
            pixelSize: 12
            lineHeight: 1.5
        }
    }
}
