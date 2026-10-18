import QtQuick

Item {
    id: root
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Bubble duration")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                height: 35
                radius: root.tokens.radiusField
                color: root.tokens.panel2
                border.width: 1
                border.color: secondsInput.activeFocus ? root.tokens.ink : root.tokens.line

                TextInput {
                    id: secondsInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11 + unitMark.width + 4
                    verticalAlignment: TextInput.AlignVCenter
                    text: String(Controller.settings.bubbleDismissSeconds)
                    color: root.tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 30 }

                    // The controller clamps, so an empty or out-of-range entry lands on the nearer edge.
                    onEditingFinished: {
                        Controller.setBubbleDismissSeconds(Number(text))
                        text = Qt.binding(() => String(Controller.settings.bubbleDismissSeconds))
                    }
                }

                Text {
                    id: unitMark
                    anchors.right: parent.right
                    anchors.rightMargin: 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: "s"
                    color: root.tokens.muted
                    font.pixelSize: 13
                }
            }
            Text {
                text: qsTranslate("SettingsPopup", "1 to 30 seconds.")
                color: root.tokens.faint
                font.pixelSize: 11
            }
        }
        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Bubble position")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "Above"), qsTranslate("SettingsPopup", "Below")]
                currentIndex: Controller.settings.bubblePlacement === "below" ? 1 : 0
                onPicked: (index) => Controller.setBubblePlacement(index === 1 ? "below" : "above")
            }
        }
        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "When the clipboard is occupied")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "Raise to top"), qsTranslate("SettingsPopup", "Give up silently")]
                currentIndex: Controller.settings.clipboardSilent ? 1 : 0
                onPicked: (index) => Controller.setClipboardPolicy(index === 1)
            }
        }
    }
}
