import QtQuick

Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string label: ""
    property string note: ""
    signal picked()

    width: parent ? parent.width : 0
    height: 44

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
        // The press reads through the fill rather than a scale: the row is a band the width of
        // the panel, and shrinking it would move its label under the pointer.
        color: press.pressed ? root.tokens.line
                             : hover.hovered ? root.tokens.panel2
                             : "transparent"
        radius: root.tokens.radiusField
    }

    /// The label starts on the page's own left edge. The row sits among labelled switches and
    /// fields, and the 9 px the catalog's rows carry left it out of line with them (UI-SPEC.md 4.4).
    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: root.tokens.text
        font.pixelSize: 13
    }

    Text {
        anchors.right: arrow.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.note
        color: root.tokens.faint
        font.pixelSize: 11
        visible: root.note !== ""
    }

    Text {
        id: arrow
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        color: root.tokens.faint
        font.pixelSize: 20
    }

    FocusRing { radius: root.tokens.radiusField + 4 }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: press
        onTapped: root.picked()
    }
}
