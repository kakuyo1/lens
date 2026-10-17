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
        anchors.leftMargin: 9
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

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: press
        onTapped: root.picked()
    }
}
