import QtQuick

/**
 * The online OCR configuration from docs/ocr-development.md 10.1. One protocol is offered in the
 * first release; the endpoint is a full HTTPS URL the reader confirms, and the key is typed once
 * and never read back.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    readonly property var settings: Controller.settings
    property var draftOwner
    readonly property string apiDraft: draftOwner ? draftOwner.ocrApiDraft : ""
    readonly property var openChildRect: languageField.openChildRect
    property bool confirmingClear: false
    signal apiEdited(string value)
    signal apiCommitted()
    implicitHeight: content.implicitHeight

    function closeChild() {
        languageField.closeList();
    }

    function testLine() {
        if (settings.ocrTestState === "idle" || settings.ocrTestState === "checking")
            return "";
        if (settings.ocrTestState === "ready")
            return qsTranslate("SettingsPopup", "Recognized in %1 ms.").arg(settings.ocrTestMs);
        if (settings.ocrTestState === "online-unconfigured")
            return qsTranslate("SettingsPopup", "Online OCR is not configured.");
        if (settings.ocrTestState === "budget-paused")
            return qsTranslate("SettingsPopup", "Today's budget is used up.");
        return qsTranslate("SettingsPopup", "The test could not read the image.");
    }

    Column {
        id: content
        width: parent.width
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "Service"); color: page.tokens.muted; font.pixelSize: 12 }
            Text {
                text: "OCR.space"
                color: page.tokens.text
                font.pixelSize: 13
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Only this OCR protocol is supported in this build; the service's own terms and billing apply.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "Endpoint"); color: page.tokens.muted; font.pixelSize: 12 }
            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: 1
                border.color: page.tokens.line

                TextInput {
                    id: endpointInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.settings.ocrOnlineEndpoint
                    color: page.tokens.text
                    font.pixelSize: 12
                    selectByMouse: true
                    activeFocusOnTab: true
                    Accessible.name: qsTranslate("SettingsPopup", "OCR endpoint")
                    onEditingFinished: {
                        Controller.captureDuty.setOcrOnlineEndpoint(text);
                        text = Qt.binding(() => page.settings.ocrOnlineEndpoint);
                    }
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: Text.AlignVCenter
                    visible: endpointInput.text.length === 0
                    text: "https://api.ocr.space/parse/image"
                    color: page.tokens.faint
                    font: endpointInput.font
                    elide: Text.ElideRight
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "The full HTTPS address, including its path. Nothing is sent until a capture is confirmed.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "Recognition language"); color: page.tokens.muted; font.pixelSize: 12 }
            DropdownField {
                id: languageField
                objectName: "ocrOnlineLanguage"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Online recognition language")
                options: [
                    {value: "eng", label: qsTranslate("SettingsPopup", "English"), group: "", note: ""},
                    {value: "auto", label: qsTranslate("SettingsPopup", "Automatic (service)"), group: "", note: ""}
                ]
                currentValue: page.settings.ocrOnlineLanguage
                onPicked: (value) => Controller.captureDuty.setOcrOnlineLanguage(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "OCR API key"); color: page.tokens.muted; font.pixelSize: 12 }
            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: 1
                border.color: page.tokens.line

                TextInput {
                    id: keyInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.apiDraft
                    echoMode: TextInput.Password
                    color: page.tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    activeFocusOnTab: true
                    Accessible.name: qsTranslate("SettingsPopup", "OCR API key")
                    onTextEdited: page.apiEdited(text)
                    onEditingFinished: {
                        page.apiCommitted();
                        text = Qt.binding(() => page.apiDraft);
                    }
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: Text.AlignVCenter
                    visible: keyInput.text.length === 0
                    text: page.settings.hasOcrApiKey
                          ? "•".repeat(28)
                          : "****-****-****-****"
                    color: page.tokens.faint
                    font: keyInput.font
                    elide: Text.ElideRight
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Separate from the explanation key. Stored on this device only, never synced, exported or logged.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7

            Text {
                width: parent.width
                text: page.settings.onlineConfigured
                      ? qsTranslate("SettingsPopup", "Ready to ask for confirmation.")
                      : qsTranslate("SettingsPopup", "Not configured.")
                color: page.settings.onlineConfigured ? page.tokens.muted : page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8

                Chip {
                    objectName: "testOnlineOcrButton"
                    label: page.settings.ocrTestState === "checking"
                           ? qsTranslate("SettingsPopup", "Testing…")
                           : qsTranslate("SettingsPopup", "Test online OCR")
                    enabled: page.settings.ocrTestState !== "checking" && page.settings.onlineConfigured
                    opacity: enabled ? 1.0 : 0.6
                    onPicked: Controller.captureDuty.testOnlineOcr()
                }

                Chip {
                    objectName: "clearOnlineOcrButton"
                    visible: !page.confirmingClear
                    label: qsTranslate("SettingsPopup", "Clear configuration")
                    onPicked: page.confirmingClear = true
                }
                Chip {
                    visible: page.confirmingClear
                    label: qsTranslate("SettingsPopup", "Confirm")
                    selected: true
                    onPicked: {
                        page.confirmingClear = false;
                        Controller.clearOcrOnlineConfiguration();
                    }
                }
                Chip {
                    visible: page.confirmingClear
                    label: qsTranslate("SettingsPopup", "Cancel")
                    onPicked: page.confirmingClear = false
                }
            }

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "The test sends a small synthetic image, never the screen, and may be billed by the service.")
                color: page.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "onlineOcrTestResult"
                width: parent.width
                visible: text !== ""
                text: page.testLine()
                color: page.settings.ocrTestState === "ready" ? page.tokens.muted : page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
