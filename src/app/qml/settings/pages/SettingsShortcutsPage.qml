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
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "OCR trigger key"); color: root.tokens.muted; font.pixelSize: 12 }
            HotkeyField {
                id: ocrField
                width: parent.width
                objectName: "ocrHotkey"
                accessibleName: qsTranslate("SettingsPopup", "OCR trigger key")
                combination: Controller.settings.ocrHotkey
                interactive: Controller.settings.screenshotAvailable
                // A combination the shell would not take says so until the reader picks one that
                // it will: the property is where the controller's own answer is kept.
                conflicted: Controller.settings.ocrHotkeyConflicted || ocrField.refused
                property bool refused: false
                onKeyChosen: (key, modifiers) => ocrField.refused = Controller.setOcrHotkey(key, modifiers) !== ""
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "Hover lookup key"); color: root.tokens.muted; font.pixelSize: 12 }
            HotkeyField {
                id: hoverField
                width: parent.width
                objectName: "hoverHotkey"
                accessibleName: qsTranslate("SettingsPopup", "Hover lookup key")
                combination: Controller.settings.hoverHotkey
                conflicted: Controller.settings.hoverHotkeyConflicted || hoverField.refused
                property bool refused: false
                onKeyChosen: (key, modifiers) => hoverField.refused = Controller.setHoverHotkey(key, modifiers) !== ""
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Switches between selection lookup and hover lookup.")
                color: root.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
