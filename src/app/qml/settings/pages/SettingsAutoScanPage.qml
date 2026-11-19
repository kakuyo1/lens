import QtQuick

/**
 * Auto scan has its own category: an on/off switch for the reader, and the apps it may read
 * (UI-SPEC.md 4.4). Chrome, Notepad++, Sumatra PDF and PotPlayer each have their own switch.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Auto scan")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                implicitHeight: masterSwitches.implicitHeight + 8
                radius: root.tokens.radiusGroup
                color: root.tokens.panel2
                border.width: 1
                border.color: root.tokens.line

                Column {
                    id: masterSwitches
                    x: 12
                    y: 4
                    width: parent.width - 24
                    spacing: 0

                    SwitchRow {
                        objectName: "autoScan"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Enable auto scan")
                        checked: Controller.settings.autoScan
                        onToggled: (on) => Controller.captureDuty.setAutoScan(on)
                    }
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Marks words above your level in supported apps. It reads the text those apps expose, without OCR or an online service.")
                color: root.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Supported apps")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                implicitHeight: appSwitches.implicitHeight + 8
                radius: root.tokens.radiusGroup
                color: root.tokens.panel2
                border.width: 1
                border.color: root.tokens.line

                Column {
                    id: appSwitches
                    x: 12
                    y: 4
                    width: parent.width - 24
                    spacing: 0

                    SwitchRow {
                        objectName: "autoScanChrome"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Chrome")
                        checked: Controller.settings.autoScanChrome
                        onToggled: (on) => Controller.captureDuty.setAutoScanChrome(on)
                    }
                    SwitchRow {
                        objectName: "autoScanNotepad"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Notepad++")
                        checked: Controller.settings.autoScanNotepad
                        onToggled: (on) => Controller.captureDuty.setAutoScanNotepad(on)
                    }
                    SwitchRow {
                        objectName: "autoScanSumatra"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Sumatra PDF 3.7+")
                        checked: Controller.settings.autoScanSumatra
                        onToggled: (on) => Controller.captureDuty.setAutoScanSumatra(on)
                    }
                    SwitchRow {
                        objectName: "autoScanPotplayer"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "PotPlayer")
                        checked: Controller.settings.autoScanPotplayer
                        onToggled: (on) => Controller.captureDuty.setAutoScanPotplayer(on)
                    }
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Chrome web pages, Notepad++ documents, Sumatra PDF files and PotPlayer video subtitles are supported.")
                color: root.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
