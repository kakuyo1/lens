import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal childRequested(string route)
    readonly property var openChildRect: styleField.openChildRect
                                         ? styleField.openChildRect
                                         : lengthField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        styleField.closeList();
        lengthField.closeList();
    }

    /// The card a hovered style row raises beside the list. The option carries the form it
    /// stands for, so the preview needs nothing from this page but the option itself.
    Component {
        id: stylePreviewComponent
        SelectionBarPreview {}
    }

    Column {
        id: content
        width: parent.width
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Trigger channels")
                color: root.tokens.muted
                font.pixelSize: 11
            }
            Rectangle {
                width: parent.width
                implicitHeight: switches.implicitHeight + 8
                radius: root.tokens.radiusGroup
                color: root.tokens.panel2
                border.width: 1
                border.color: root.tokens.line

                Column {
                    id: switches
                    x: 12
                    y: 4
                    width: parent.width - 24
                    spacing: 0

                    SwitchRow {
                        objectName: "selectionCapture"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Selection")
                        checked: Controller.settings.selectionCapture
                        onToggled: (on) => Controller.setSelectionCapture(on)
                    }
                    SwitchRow {
                        objectName: "ocrCapture"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "OCR")
                        checked: Controller.settings.ocrCapture
                        interactive: Controller.settings.ocrAvailable
                        onToggled: (on) => Controller.captureDuty.setOcrCapture(on)
                    }
                    SwitchRow {
                        objectName: "autoScan"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Auto scan")
                        checked: Controller.settings.autoScan
                        interactive: Controller.settings.ocrAvailable
                        onToggled: (on) => Controller.captureDuty.setAutoScan(on)
                    }
                }
            }
        }

        // The shape of the action bar the selection channel raises. It sits right after the
        // channels because it is the surface the first switch owns, and before the OCR key
        // because that key belongs to the second switch.
        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Selection bar style")
                color: root.tokens.muted
                font.pixelSize: 11
            }
            DropdownField {
                id: styleField
                objectName: "selectionBarStyle"
                width: parent.width
                options: [
                    {value: "bar", label: qsTranslate("SettingsPopup", "Classic bar"),
                     group: "", note: "", previewStyle: "bar"},
                    {value: "tab", label: qsTranslate("SettingsPopup", "Side tab"),
                     group: "", note: "", previewStyle: "tab"},
                    {value: "hand", label: qsTranslate("SettingsPopup", "Hand of cards"),
                     group: "", note: "", previewStyle: "hand"},
                    {value: "tag", label: qsTranslate("SettingsPopup", "Hang tag"),
                     group: "", note: "", previewStyle: "tag"},
                    {value: "fan", label: qsTranslate("SettingsPopup", "Fan"),
                     group: "", note: "", previewStyle: "fan"}
                ]
                currentValue: Controller.settings.selectionBarStyle
                optionPreview: stylePreviewComponent
                onPicked: (value) => Controller.setSelectionBarStyle(value)
            }
        }

        // Beside the trigger-channel group rather than inside it: those three are switches, and
        // this one is the key that stands in for a gesture, which is a value rather than an on/off.
        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "OCR trigger key"); color: root.tokens.muted; font.pixelSize: 11 }
            HotkeyField {
                id: hotkeyField
                width: parent.width
                objectName: "ocrHotkey"
                combination: Controller.settings.ocrHotkey
                interactive: Controller.settings.ocrAvailable
                // A combination the shell would not take says so until the reader picks one that
                // it will: the property is where the controller's own answer is kept.
                conflicted: Controller.settings.ocrHotkeyConflicted || hotkeyField.refused
                property bool refused: false
                onKeyChosen: (key, modifiers) => hotkeyField.refused = Controller.setOcrHotkey(key, modifiers) !== ""
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Popup & clipboard")
            note: Controller.settings.clipboardSilent
                  ? qsTranslate("SettingsPopup", "Give up silently")
                  : qsTranslate("SettingsPopup", "Raise to top")
            onPicked: root.childRequested("clipboard")
        }

        Text {
            width: parent.width
            visible: !Controller.settings.ocrAvailable
            text: Controller.settings.ocrStatus === "checking" ? qsTr("Checking OCR")
                  : Controller.settings.ocrStatus === "english-data-missing" ? qsTr("English OCR data is missing")
                  : qsTr("OCR runtime is unavailable")
            color: root.tokens.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // The bundled runtime is what the installer lays down; these two are for a reader who
        // already keeps their own Tesseract and would rather point at it. Each is chosen rather
        // than typed -- the path is picked in the system's own dialog and shown here, and the
        // folder glyph at its right end is what says so. An unchosen one draws what it will
        // resolve to, in the faint colour a placeholder uses, so the reader can see where OCR
        // looks before deciding to move it. `Defaults` in the footer is the way back to that.
        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Tesseract executable"); color: root.tokens.muted; font.pixelSize: 12 }
            PathField {
                id: tesseractField
                width: parent.width
                objectName: "tesseractExecutable"
                label: qsTr("Tesseract executable")
                path: Controller.settings.tesseractExecutable
                fallback: Controller.settings.resolvedTesseractExecutable
                onChosen: (picked) => Controller.setTesseractExecutable(picked)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Tesseract data folder"); color: root.tokens.muted; font.pixelSize: 12 }
            PathField {
                id: tesseractDataField
                width: parent.width
                objectName: "tesseractDataDirectory"
                label: qsTr("Tesseract data folder")
                path: Controller.settings.tesseractDataDirectory
                fallback: Controller.settings.resolvedTesseractDataDirectory
                folder: true
                onChosen: (picked) => Controller.setTesseractDataDirectory(picked)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Minimum word length"); color: root.tokens.muted; font.pixelSize: 12 }
            DropdownField {
                id: lengthField
                objectName: "minimumWordLength"
                width: parent.width
                options: [
                    {value: 2, label: "2", group: "", note: ""},
                    {value: 3, label: "3", group: "", note: ""},
                    {value: 4, label: "4", group: "", note: ""},
                    {value: 5, label: "5", group: "", note: ""}
                ]
                currentValue: Controller.settings.minimumWordLength
                onPicked: (value) => Controller.captureDuty.setMinimumWordLength(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Scan whitelist"); color: root.tokens.muted; font.pixelSize: 12 }
            TextField {
                id: whitelistField
                objectName: "scanWhitelist"
                width: parent.width
                text: Controller.settings.scanWhitelist
                font.pixelSize: 13
                color: root.tokens.text
                selectByMouse: true
                maximumLength: 4096
                Accessible.name: qsTr("Scan whitelist")
                background: Rectangle {
                    radius: root.tokens.radiusField
                    color: root.tokens.panel2
                    border.width: 1
                    border.color: whitelistField.activeFocus ? root.tokens.ink : root.tokens.line
                }
                onEditingFinished: {
                    Controller.captureDuty.setScanWhitelist(text);
                    text = Qt.binding(() => Controller.settings.scanWhitelist);
                }
            }
        }
    }
}
