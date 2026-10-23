pragma ComponentBehavior: Bound

import QtQuick

/**
 * The two edges a stepped card shows above itself: the cards it was stepped from, under this one (UI-SPEC.md 4.3).
 *
 * They are drawn behind the card, from its top edge and inset from its sides, so only the strip above it shows.
 * They stand for depth only; no reader action reaches them.
 */
Item {
    id: root

    /// The card the edges stand behind.
    required property Item card
    /// Whether the card is stepped: the edges show only then.
    property bool shown: false
    /// The fill of both edges.
    required property color edgeColor

    Rectangle {
        visible: root.shown
        x: root.card.x + 10
        y: root.card.y - 7
        width: root.card.width - 20
        height: 16
        radius: 8
        color: root.edgeColor
    }

    Rectangle {
        visible: root.shown
        x: root.card.x + 20
        y: root.card.y - 13
        width: root.card.width - 40
        height: 16
        radius: 8
        color: root.edgeColor
        opacity: 0.8
    }
}
