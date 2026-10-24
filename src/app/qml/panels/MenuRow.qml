import QtQuick

/**
 * One row of the tray menu (UI-SPEC.md section 4.2): a glyph and a label, an optional value on the
 * right in the monospace face, and the hover fill. The dangerous one -- Quit -- is the same row
 * in the danger colour.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string source: ""
    property string label: ""
    property string note: ""
    property bool danger: false

    /// The language row watches it to unfold its list.
    readonly property alias hovered: hover.hovered

    signal picked()

    implicitHeight: 32

    // The value on the right is part of what the row says: a screen reader that names only the
    // label leaves the day's tally and the chosen language unread.
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label + (root.note !== "" ? ", " + root.note : "")
    Accessible.onPressAction: root.picked()
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                && !event.isAutoRepeat) {
            event.accepted = true;
            root.picked();
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.tokens.radiusField
        // A whole row reads as pressed through its fill rather than its size: shrinking a band
        // this wide would move the text under the pointer. `line` is one step past the hover
        // fill in both themes, which is what makes the press visible without a new token.
        color: press.pressed ? root.tokens.line
                             : hover.hovered ? root.tokens.panel2
                             : "transparent"
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: press
        onTapped: root.picked()
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        spacing: 9

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            source: root.source
            color: root.danger ? root.tokens.danger : root.tokens.muted
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.danger ? root.tokens.danger : root.tokens.text
            font.pixelSize: 13
        }
    }

    MixedText {
        visible: root.note !== ""
        id: noteText
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        value: root.note
        color: root.tokens.faint
        pixelSize: 11
    }

    FocusRing { radius: root.tokens.radiusField + 4 }
}
