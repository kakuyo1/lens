import QtQuick

/**
 * The verdict buttons a bubble opens under the pointer: Known and New, and the hairline above them
 * (UI-SPEC.md 4.3).
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
    /// The bubble window this row belongs to, whose reading clock a screen-reader press refills.
    required property var bubbleWindow
    /// The word a press writes a verdict for.
    required property string lemma
    /// Whether the pointer is on the card.
    required property bool hovering
    /// Whether this card has a verdict to give, settled once by the card's own reading.
    required property bool available

    /// Whether the row is open: the pointer on the card, and a word to judge.
    readonly property bool show: root.hovering && root.available
    /// The one-time sentence under the pills. It is latched for this opening even though the
    /// document flag flips at once, and it never returns once the reader has seen it.
    property bool hintLatch: false
    readonly property bool showHint: show && hintLatch
    readonly property real hintHeight: hintText.implicitHeight + 8
    readonly property real openHeight: actions.height + 25 + (showHint ? hintHeight : 0)

    onShowChanged: {
        if (show && !Controller.settings.verdictHintSeen)
            hintLatch = true;
        else if (!show)
            hintLatch = false;
    }

    /// A pointer that crosses the row on its way somewhere else should not spend the one
    /// explanation: the flag is written only once the sentence has stood for a moment.
    Timer {
        interval: 1500
        running: root.showHint && !Controller.settings.verdictHintSeen
        onTriggered: Controller.setSetting("verdictHintSeen", true)
    }

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

    // The two pills carry names and a press for the screen reader but no tab stops or rings: the
    // bubble's window never takes focus (UI-SPEC.md 4.3), so no keyboard focus can reach them.
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

            Accessible.role: Accessible.Button
            Accessible.name: knownLabel.text
            Accessible.description: root.showHint ? hintText.text : ""
            Accessible.onPressAction: {
                root.bubbleWindow.readerActed();
                knownTap.tapped();
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

            Accessible.role: Accessible.Button
            Accessible.name: newLabel.text
            Accessible.description: root.showHint ? hintText.text : ""
            Accessible.onPressAction: {
                root.bubbleWindow.readerActed();
                newTap.tapped();
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

    // The first verdict row explains the two choices once. It sits under the pills rather than
    // beside them, so the choice stays the loudest thing on the row; after this opening the
    // sentence is gone and the row returns to its two-button height.
    Text {
        id: hintText
        objectName: "verdictHint"
        anchors.top: actions.bottom
        anchors.topMargin: 8
        width: parent.width
        visible: root.showHint
        text: qsTr("Known stops automatic scans; New keeps it in practice.")
        color: root.tokens.faint
        font.pixelSize: 11
        lineHeight: 1.3
        lineHeightMode: Text.ProportionalHeight
        wrapMode: Text.WordWrap
    }
}
