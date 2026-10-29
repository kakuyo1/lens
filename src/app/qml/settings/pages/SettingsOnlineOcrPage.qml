import QtQuick

/**
 * The online OCR configuration page: which service is asked, at what address, in what language,
 * and with which credentials; the surface spec is UI-SPEC.md 4.4.
 *
 * The service list, its address, and the fields each service needs all come from the provider
 * table in src/ocr/online_providers.cpp; this page only renders them. A credential is typed once
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
    readonly property string secretDraft: draftOwner ? draftOwner.ocrSecretDraft : ""
    readonly property var openChildRect: providerField.openChildRect || languageField.openChildRect
    property bool confirmingClear: false
    signal credentialEdited(string slot, string value)
    signal credentialCommitted()
    implicitHeight: content.implicitHeight

    function closeChild() {
        providerField.closeList();
        languageField.closeList();
    }

    function hasSlot(slot) {
        const slots = page.settings.ocrOnlineCredentials || [];
        for (let i = 0; i < slots.length; ++i)
            if (slots[i].slot === slot)
                return true;
        return false;
    }

    function slotPresent(slot) {
        const slots = page.settings.ocrOnlineCredentials || [];
        for (let i = 0; i < slots.length; ++i)
            if (slots[i].slot === slot)
                return slots[i].present;
        return false;
    }

    function credentialLabel(slot) {
        const slots = page.settings.ocrOnlineCredentials || [];
        for (let i = 0; i < slots.length; ++i)
            if (slots[i].slot === slot)
                return slots[i].label;
        return slot;
    }

    function languageLabel(value) {
        if (value === "eng")
            return qsTranslate("SettingsPopup", "English");
        if (value === "auto")
            return qsTranslate("SettingsPopup", "Automatic (service default)");
        return value;
    }

    function languageOptions() {
        const values = page.settings.ocrOnlineLanguages || [];
        const options = [];
        for (let i = 0; i < values.length; ++i)
            options.push({value: values[i], label: page.languageLabel(values[i]), group: "", note: ""});
        return options;
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
        if (settings.ocrTestState === "timeout")
            return qsTranslate("SettingsPopup", "The service did not answer in time. Try again later.");
        if (settings.ocrTestState === "transport-failed")
            return qsTranslate("SettingsPopup", "The service could not be reached. Check the network or proxy.");
        if (settings.ocrTestState === "empty-text")
            return qsTranslate("SettingsPopup", "The service answered but found no text in the test image.");
        if (settings.ocrTestReason === "not-activated")
            return qsTranslate("SettingsPopup", "The service is not enabled for this account. Activate it in the service's console, then try again.");
        if (settings.ocrTestReason === "authentication")
            return qsTranslate("SettingsPopup", "The service rejected the key or its permissions. Check that the key is active and allowed to call this service.");
        if (settings.ocrTestReason === "quota")
            return qsTranslate("SettingsPopup", "The service's quota or rate limit is used up. Check the plan or billing state in the service's console.");
        if (settings.ocrTestReason === "image" || settings.ocrTestState === "invalid-image" || settings.ocrTestState === "too-large")
            return qsTranslate("SettingsPopup", "The service refused the image. Check its size and format limits.");
        if (settings.ocrTestReason === "unavailable")
            return qsTranslate("SettingsPopup", "The service is temporarily unavailable. Try again later.");
        return qsTranslate("SettingsPopup", "The service refused the test. Its error code is in the log.");
    }

    Column {
        id: content
        width: parent.width
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTranslate("SettingsPopup", "Service"); color: page.tokens.muted; font.pixelSize: 12 }
            DropdownField {
                id: providerField
                objectName: "ocrOnlineProvider"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Online OCR service")
                options: page.settings.ocrOnlineProviders || []
                currentValue: page.settings.ocrOnlineProvider
                onPicked: (value) => Controller.captureDuty.setOcrOnlineProvider(value)
            }
            Text {
                width: parent.width
                text: page.settings.ocrOnlineProvider === ""
                      ? qsTranslate("SettingsPopup", "Choose a service, then enter the credentials it needs.")
                      : qsTranslate("SettingsPopup", "The service's own terms and billing apply. Switching services clears the address and credentials below.")
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
                    text: page.settings.ocrOnlineEndpointDefault
                    color: page.tokens.faint
                    font: endpointInput.font
                    elide: Text.ElideRight
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "The full HTTPS address. Leave it empty to use the service's own; nothing is sent until a capture is confirmed.")
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
                options: page.languageOptions()
                currentValue: page.settings.ocrOnlineLanguage
                onPicked: (value) => Controller.captureDuty.setOcrOnlineLanguage(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            objectName: "ocrOnlineApiKeyField"
            visible: page.hasSlot("apiKey")
            Text { text: page.credentialLabel("apiKey"); color: page.tokens.muted; font.pixelSize: 12 }
            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: 1
                border.color: page.tokens.line

                TextInput {
                    id: apiKeyInput
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
                    Accessible.name: page.credentialLabel("apiKey")
                    onTextEdited: page.credentialEdited("apiKey", text)
                    onEditingFinished: {
                        page.credentialCommitted();
                        text = Qt.binding(() => page.apiDraft);
                    }
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: Text.AlignVCenter
                    visible: apiKeyInput.text.length === 0
                    text: page.slotPresent("apiKey") ? "•".repeat(28) : "****-****-****-****"
                    color: page.tokens.faint
                    font: apiKeyInput.font
                    elide: Text.ElideRight
                }
            }
        }

        Column {
            width: parent.width
            spacing: 7
            objectName: "ocrOnlineSecretKeyField"
            visible: page.hasSlot("secretKey")
            Text { text: page.credentialLabel("secretKey"); color: page.tokens.muted; font.pixelSize: 12 }
            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: 1
                border.color: page.tokens.line

                TextInput {
                    id: secretKeyInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.secretDraft
                    echoMode: TextInput.Password
                    color: page.tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    activeFocusOnTab: true
                    Accessible.name: page.credentialLabel("secretKey")
                    onTextEdited: page.credentialEdited("secretKey", text)
                    onEditingFinished: {
                        page.credentialCommitted();
                        text = Qt.binding(() => page.secretDraft);
                    }
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: Text.AlignVCenter
                    visible: secretKeyInput.text.length === 0
                    text: page.slotPresent("secretKey") ? "•".repeat(28) : "****-****-****-****"
                    color: page.tokens.faint
                    font: secretKeyInput.font
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            width: parent.width
            visible: page.settings.ocrOnlineCredentials.length > 0
            text: qsTranslate("SettingsPopup", "Separate from the explanation key. Stored on this device only, never synced, exported or logged.")
            color: page.tokens.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
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
