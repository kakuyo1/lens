pragma ComponentBehavior: Bound

import QtQuick

/**
 * The signpost of a word: the meaning in a few plain words, as a label rather than a sentence (UI.md 4.3).
 *
 * Drawn only when there is one. An entry explained before the chain existed has none, and an empty slot would
 * read as a model that failed to give one.
 */
Text {
    id: root

    /// The label to draw, or empty for none.
    required property string label
    /// The card's colours (SurfaceTokens).
    required property var tokens
    /// The multiple the label is drawn at, from the reader's font-size setting (UI.md 4.4).
    property real scale: 1

    visible: root.label !== ""
    topPadding: 5
    text: root.label
    color: root.tokens.okText
    font.pixelSize: Math.round(10 * root.scale)
    font.weight: Font.Bold
}
