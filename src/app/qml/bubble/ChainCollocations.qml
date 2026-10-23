pragma ComponentBehavior: Bound

import QtQuick

/**
 * The collocations of a word, one line of mono at the foot: up to two, with the word written as "~" (UI-SPEC.md 4.3).
 */
Text {
    id: root

    /// The collocations to draw, as strings; none draws nothing.
    required property var words
    /// The card's colours and faces (SurfaceTokens).
    required property var tokens
    /// The multiple the line is drawn at, from the reader's font-size setting (UI-SPEC.md 4.4).
    property real scale: 1

    visible: root.words.length > 0
    topPadding: 9
    text: root.words.join(" · ")
    color: root.tokens.faint
    font.pixelSize: Math.round(10.5 * root.scale)
    font.family: root.tokens.monoFamily
}
