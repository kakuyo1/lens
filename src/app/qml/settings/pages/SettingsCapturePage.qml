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
                font.pixelSize: 12
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
                        onToggled: (on) => Controller.setSetting("selectionCapture", on)
                    }
                    SwitchRow {
                        objectName: "autoScan"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Auto scan")
                        badge: qsTranslate("SettingsPopup", "Experimental")
                        checked: Controller.settings.autoScan
                        onToggled: (on) => Controller.captureDuty.setAutoScan(on)
                    }
                }
            }
        }

        // The scan switch stays usable as an experiment, so its warning belongs right under the
        // group rather than in the README alone: the reader meets the "Experimental" badge here
        // and learns what it means before flipping anything.
        Text {
            objectName: "autoScanNote"
            width: parent.width
            text: qsTranslate("SettingsPopup", "Auto scan is still experimental and not released yet; its results are not reliable.")
            color: root.tokens.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // The hover lookup key moved here from the shortcuts page: it switches between the two ways
        // of looking a word up, and both live on this page now.
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

        // The shape of the action bar the selection channel raises. It sits right after the
        // channels because it is the surface the first switch owns.
        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Selection bar style")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: styleField
                objectName: "selectionBarStyle"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Selection bar style")
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
                onPicked: (value) => Controller.setSetting("selectionBarStyle", value)
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Popup & clipboard")
            note: Controller.settings.clipboardPolicy === "silent"
                  ? qsTranslate("SettingsPopup", "Give up silently")
                  : qsTranslate("SettingsPopup", "Raise to top")
            onPicked: root.childRequested("clipboard")
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Minimum word length"); color: root.tokens.muted; font.pixelSize: 12 }
            DropdownField {
                id: lengthField
                objectName: "minimumWordLength"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Minimum word length")
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
                activeFocusOnTab: true
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
