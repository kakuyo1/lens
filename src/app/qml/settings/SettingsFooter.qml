import QtQuick

/**
 * The settings panel's footer: the Defaults action and the one status line beside it.
 *
 * A file of its own because SettingsPopup.qml sits at the QML size cap (docs/QML.md section 1),
 * and this is a closed block: nothing else in the panel reads it but the body's height.
 *
 * A press on Defaults only asks. The reset is wide (sixteen settings), so the question comes
 * before the write; the confirmation's sentence says what the reset leaves alone.
 */
Item {
    id: root

    /// The settings window this footer belongs to.
    required property var panel

    x: 0
    y: root.panel.cardHeight - root.height
    width: root.panel.cardWidth
    height: 56

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: root.panel.tokens.line2
    }

    Row {
        id: defaultsRow
        objectName: "defaultsRow"
        x: 20
        y: 15
        spacing: 8

        Chip {
            id: defaultsButton
            objectName: "defaultsButton"
            label: qsTranslate("SettingsPopup", "Defaults")
            onPicked: {
                // A stale "restored" line under a new question reads as a result of the press
                // that only asked, so it goes with the question.
                root.panel.saveStatus = "";
                root.panel.askConfirm(
                    qsTranslate("SettingsPopup", "Restore defaults"),
                    qsTranslate("SettingsPopup", "API key, launch at login and daily budget stay as they are."),
                    qsTranslate("SettingsPopup", "Restore"),
                    () => root.panel.restoreDefaults());
            }
        }
    }

    Text {
        id: defaultsStatus
        objectName: "defaultsStatus"
        anchors.left: defaultsRow.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        height: 40
        verticalAlignment: Text.AlignVCenter
        text: root.panel.saveStatus
        // Status, not decoration: the second step of the text ramp, so a confirmation reads
        // beside the value rows it confirms.
        color: root.panel.tokens.muted
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 3
    }
}
