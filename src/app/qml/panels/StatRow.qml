import QtQuick

/**
 * One labelled figure in the statistics and cost panels (UI-SPEC.md 4.6 / 4.8).
 *
 * A clickable row widens its hover area past the text and grows a chevron, which is how the
 * two drill-downs announce themselves.
 *
 * `note` is a second, quieter figure on the same line, left of `value`; the cost rows put the
 * tokens their amount was bought with there. It is its own slot rather than more words in `value`:
 * the amount keeps the bold right-aligned face and the count is context, and one string wide enough
 * for both runs into the label on a card this narrow.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string label: ""
    property string value: ""
    property string note: ""
    property bool clickable: false

    signal tapped()

    height: 29

    // Both faces read as one line: label, the quieter note, then the figure. The clickable ones
    // press; the rest are plain text the screen reader reads the same way.
    Accessible.role: root.clickable ? Accessible.Button : Accessible.StaticText
    Accessible.name: root.label + (root.note !== "" ? ", " + root.note : "") + ", " + root.value
    activeFocusOnTab: root.clickable
    Accessible.onPressAction: if (root.clickable) root.tapped()
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                && !event.isAutoRepeat && root.clickable) {
            event.accepted = true;
            root.tapped();
        }
    }

    Rectangle {
        // The hover fill reaches 8px past the text on both sides, so the target is bigger
        // than the words in it.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.clickable ? -8 : 0
        anchors.rightMargin: root.clickable ? -8 : 0
        height: parent.height - 2
        radius: 8
        color: root.clickable && rowPress.pressed ? root.tokens.line
             : root.clickable && rowHover.hovered ? root.tokens.panel2
             : "transparent"
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: root.tokens.muted
        font.pixelSize: 13
    }

    MixedText {
        id: noteText
        visible: root.note !== ""
        anchors.right: valueText.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        value: root.note
        color: root.tokens.faint
        pixelSize: 11
    }

    MixedText {
        id: valueText
        anchors.right: chevron.left
        anchors.rightMargin: root.clickable ? 6 : 0
        anchors.verticalCenter: parent.verticalCenter
        value: root.value
        color: root.tokens.text
        pixelSize: 13
        weight: Font.Bold
    }

    Icon {
        id: chevron
        visible: root.clickable
        anchors.right: parent.right
        anchors.rightMargin: root.clickable ? -8 : 0
        anchors.verticalCenter: parent.verticalCenter
        width: 13
        height: 13
        source: "qrc:/icons/ui-chevron-right.svg"
        color: root.tokens.faint
    }

    FocusRing {
        radius: 12
        anchors.leftMargin: root.clickable ? -12 : -4
        anchors.rightMargin: root.clickable ? -12 : -4
    }

    HoverHandler { id: rowHover; cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler {
        id: rowPress
        enabled: root.clickable
        onTapped: root.tapped()
    }
}
