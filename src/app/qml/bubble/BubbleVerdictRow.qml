import QtQuick

/**
 * The verdict buttons a bubble opens under the pointer: Known and New, and the hairline above them
 * (UI.md 4.3).
 *
 * Only a real explanation has a verdict to give -- a notice's title is not a word, so a press there
 * would write a sentence into the word store -- and the row is drawn shut rather than left out, so
 * that opening it moves the card's contents down instead of changing the card's shape.
 *
 * A component rather than part of Bubble.qml because that file sits at the repository's QML size cap
 * (docs/QML.md section 1), the way SenseRows and PreviewFrame are.
 */
Item {
    id: root

    required property var tokens
    /// The word a press writes a verdict for.
    required property string lemma
    /// Whether the pointer is on the card.
    required property bool hovering
    /// "new", "known", or "" for a card that has no verdict to give.
    required property string status

    /// Whether the row is open: the pointer on the card, and a word to judge.
    readonly property bool show: root.hovering && root.status !== ""
    /// The height the row reaches when open.
    readonly property real openHeight: actions.height + 25

    width: parent ? parent.width : 0
    height: show ? openHeight : 0
    clip: true
    opacity: show ? 1 : 0
    Behavior on height {
        NumberAnimation {
            duration: root.tokens.motion.bubble
            easing.type: root.tokens.motion.easing
        }
    }
    Behavior on opacity {
        NumberAnimation {
            duration: root.tokens.motion.bubble
            easing.type: root.tokens.motion.easing
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: 12
        width: parent.width
        height: 1
        color: root.tokens.line2
    }

    Row {
        id: actions
        anchors.top: parent.top
        anchors.topMargin: 25
        spacing: 8

        Rectangle {
            width: knownLabel.width + 26
            height: knownLabel.height + 12
            radius: root.tokens.radiusPill
            color: root.tokens.panel2
            border.width: 1
            border.color: root.tokens.line
            scale: knownTap.pressed ? 0.97 : 1.0
            Behavior on scale {
                NumberAnimation {
                    duration: root.tokens.motion.press
                    easing.type: root.tokens.motion.easing
                }
            }
            Text {
                id: knownLabel
                anchors.centerIn: parent
                text: qsTr("Known")
                color: root.tokens.text
                font.pixelSize: 12
                font.weight: Font.Bold
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler {
                id: knownTap
                onTapped: Controller.explanationDuty.mark(root.lemma, true)
            }
        }

        Rectangle {
            width: newLabel.width + 26
            height: newLabel.height + 12
            radius: root.tokens.radiusPill
            color: root.tokens.ink
            scale: newTap.pressed ? 0.97 : 1.0
            Behavior on scale {
                NumberAnimation {
                    duration: root.tokens.motion.press
                    easing.type: root.tokens.motion.easing
                }
            }
            Text {
                id: newLabel
                anchors.centerIn: parent
                text: qsTr("New")
                color: root.tokens.on
                font.pixelSize: 12
                font.weight: Font.Bold
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler {
                id: newTap
                onTapped: Controller.explanationDuty.mark(root.lemma, false)
            }
        }
    }
}
