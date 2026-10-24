import QtQuick

Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string title: ""
    property string summary: ""
    signal picked()

    width: parent ? parent.width : 0
    height: 60

    // The summary is part of the row's name: it is the sentence that tells the categories apart.
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.title + (root.summary !== "" ? ", " + root.summary : "")
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
        radius: 9
        // A whole row reads as pressed through its fill: shrinking a band this wide would move
        // the text under the pointer. `line` is one step past the hover fill in both themes.
        color: press.pressed ? root.tokens.line
                             : hover.hovered ? root.tokens.panel2
                             : "transparent"
    }

    Column {
        anchors.left: parent.left
        // The text sits on the page's own left edge, the one every labelled control and link row
        // on the pages starts from (UI-SPEC.md 4.4).
        anchors.leftMargin: 0
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Text {
            text: root.title
            color: root.tokens.text
            font.pixelSize: 13
        }
        Text {
            text: root.summary
            color: root.tokens.muted
            font.pixelSize: 11
        }
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        color: root.tokens.faint
        font.pixelSize: 20
    }

    FocusRing { radius: 13 }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: press
        onTapped: root.picked()
    }
}
