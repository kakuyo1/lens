import QtQuick

/**
 * The OCR category: the screenshot switch and its trigger key, manual capture, the local engine and
 * its recognition language, a test, and the two sub-pages (UI-SPEC.md 4.4).
 *
 * The switch and the key moved here from the capture and shortcuts pages; their behaviour is unchanged.
 * The engine paths live on their own sub-page, opened by the local-engine row at the foot. Auto is the
 * default and resolves to Tesseract. An explicit choice is never rewritten, so an unavailable one says
 * why rather than quietly falling back.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    readonly property var settings: Controller.settings
    readonly property var openChildRect: modeField.openChildRect
                                         ? modeField.openChildRect
                                         : engineField.openChildRect
                                         ? engineField.openChildRect
                                         : languageField.openChildRect
    implicitHeight: content.implicitHeight
    signal childRequested(string route)

    function closeChild() {
        modeField.closeList();
        engineField.closeList();
        languageField.closeList();
    }

    /// @return The engine the result came from, in the reader's words.
    function engineName(value) {
        const providers = settings.ocrOnlineProviders || [];
        for (let i = 0; i < providers.length; ++i)
            if (providers[i].value === value)
                return providers[i].label;
        if (value === "rapidocr" || value === "rapidocr-json") return "RapidOCR";
        return qsTranslate("SettingsPopup", "Tesseract");
    }

    /// @return The one status sentence under the engine row.
    function availabilityLine() {
        if (settings.ocrStatus === "checking")
            return qsTranslate("SettingsPopup", "Checking OCR");
        if (settings.ocrStatus === "path-unsupported")
            return qsTranslate("SettingsPopup", "RapidOCR's model folder path must contain only ASCII characters. Move the models to an ASCII-only path and choose it again.");
        if (settings.ocrStatus === "untrusted") {
            if (settings.localEngineReason === "trust-changed")
                return qsTranslate("SettingsPopup", "RapidOCR's program or models changed. Detect and trust it again.");
            return qsTranslate("SettingsPopup", "RapidOCR is not trusted yet. Detect it before use.");
        }
        if (settings.ocrStatus === "language-missing") {
            if (settings.ocrLocalEngine === "rapidocr")
                return qsTranslate("SettingsPopup", "RapidOCR's model files are missing: %1").arg(settings.localEngineReason);
            return qsTranslate("SettingsPopup", "The OCR language data is missing: %1").arg(settings.localEngineReason);
        }
        if (settings.ocrStatus === "runtime-missing") {
            if (settings.ocrLocalEngine === "rapidocr")
                return qsTranslate("SettingsPopup", "RapidOCR is not installed.");
            return qsTranslate("SettingsPopup", "Tesseract is not installed.");
        }
        if (settings.ocrStatus === "ready")
            return qsTranslate("SettingsPopup", "%1 is ready.").arg(page.engineName(settings.actualLocalEngine));
        return qsTranslate("SettingsPopup", "OCR is not ready.");
    }

    /// @return The last explicit test's answer, or "" before one has run.
    function testLine() {
        if (settings.ocrTestState === "idle" || settings.ocrTestState === "checking")
            return "";
        if (settings.ocrTestState === "ready")
            return qsTranslate("SettingsPopup", "Recognized with %1 in %2 ms.")
                .arg(page.engineName(settings.ocrTestEngine))
                .arg(settings.ocrTestMs);
        if (settings.ocrTestState === "path-unsupported")
            return qsTranslate("SettingsPopup", "RapidOCR's model folder path must contain only ASCII characters.");
        if (settings.ocrTestState === "untrusted")
            return qsTranslate("SettingsPopup", "RapidOCR is not trusted yet. Detect it before use.");
        if (settings.ocrTestState === "language-missing") {
            if (settings.ocrLocalEngine === "rapidocr")
                return qsTranslate("SettingsPopup", "RapidOCR's model files are missing.");
            return qsTranslate("SettingsPopup", "The OCR language data is missing.");
        }
        if (settings.ocrTestState === "runtime-missing") {
            if (settings.ocrLocalEngine === "rapidocr")
                return qsTranslate("SettingsPopup", "RapidOCR is not installed.");
            return qsTranslate("SettingsPopup", "Tesseract is not installed.");
        }
        return qsTranslate("SettingsPopup", "The test could not read the image.");
    }

    Column {
        id: content
        width: parent.width
        spacing: 14

        // The switch and the trigger key that start a screenshot. Both are grey while the local OCR
        // runtime is unavailable, and the reason is on the availability line below the engine.
        Column {
            width: parent.width
            spacing: 7

            SwitchRow {
                objectName: "ocrCapture"
                width: parent.width
                label: qsTranslate("SettingsPopup", "OCR")
                checked: Controller.settings.ocrCapture
                interactive: Controller.settings.screenshotAvailable
                onToggled: (on) => Controller.captureDuty.setOcrCapture(on)
            }

            Column {
                width: parent.width
                spacing: 7
                Text { text: qsTranslate("SettingsPopup", "OCR trigger key"); color: page.tokens.muted; font.pixelSize: 12 }
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
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Manual capture")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: modeField
                objectName: "ocrMode"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Manual capture mode")
                options: [
                    {value: "local", label: qsTranslate("SettingsPopup", "Local"), group: "",
                     note: qsTranslate("SettingsPopup", "Recognize on this machine")},
                    {value: "online", label: qsTranslate("SettingsPopup", "Online"), group: "",
                     note: qsTranslate("SettingsPopup", "Ask before each upload")}
                ]
                currentValue: page.settings.ocrMode
                onPicked: (value) => Controller.captureDuty.setOcrMode(value)
            }
            Text {
                width: parent.width
                visible: page.settings.ocrMode === "online" && !page.settings.onlineConfigured
                text: qsTranslate("SettingsPopup", "Online OCR is not configured yet; screenshots stay disabled until it is.")
                color: page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Local engine")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: engineField
                objectName: "ocrLocalEngine"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Local engine")
                options: [
                    {value: "auto", label: qsTranslate("SettingsPopup", "Automatic"),
                     group: "", note: "Tesseract"},
                    {value: "tesseract", label: "Tesseract", group: "", note: ""},
                    {value: "rapidocr", label: "RapidOCR-json",
                     group: "", note: qsTranslate("SettingsPopup", "User-installed; ASCII model folder required")}
                ]
                currentValue: page.settings.ocrLocalEngine
                // The RapidOCR note is a sentence, and beside its label it runs into the label, so
                // it stays off the rows.
                notesInRows: false
                onPicked: (value) => Controller.captureDuty.setOcrLocalEngine(value)
            }
            Text {
                objectName: "ocrAvailability"
                width: parent.width
                text: page.availabilityLine()
                color: page.settings.ocrStatus === "ready" ? page.tokens.muted : page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                visible: page.settings.ocrLocalEngine === "auto"
                text: qsTranslate("SettingsPopup", "Auto scan reads the text its apps expose and never uses this engine or an online service.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Recognition language")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: languageField
                objectName: "ocrLocalLanguage"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Recognition language")
                options: [
                    {value: "en", label: qsTranslate("SettingsPopup", "English"), group: "", note: ""},
                    {value: "zh-Hans", label: qsTranslate("SettingsPopup", "Simplified Chinese"), group: "", note: ""},
                    {value: "zh-Hans+en", label: qsTranslate("SettingsPopup", "Chinese and English"), group: "", note: ""}
                ]
                currentValue: page.settings.ocrLocalLanguage
                onPicked: (value) => Controller.captureDuty.setOcrLocalLanguage(value)
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "This is the language in the picture, not the language the explanation is written in.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 7

            Chip {
                objectName: "testLocalOcrButton"
                label: page.settings.ocrTestState === "checking"
                       ? qsTranslate("SettingsPopup", "Testing…")
                       : qsTranslate("SettingsPopup", "Test OCR")
                enabled: page.settings.ocrTestState !== "checking"
                opacity: enabled ? 1.0 : 0.6
                onPicked: Controller.captureDuty.testLocalOcr()
            }

            Text {
                objectName: "ocrTestResult"
                width: parent.width
                visible: text !== ""
                text: page.testLine()
                color: page.settings.ocrTestState === "ready" ? page.tokens.muted : page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        // The two sub-pages of this category: the online service, and the local engine's paths.
        SettingsLinkRow {
            objectName: "onlineOcrLink"
            label: qsTranslate("SettingsPopup", "Online OCR")
            note: page.settings.onlineConfigured
                  ? page.settings.ocrOnlineHost
                  : qsTranslate("SettingsPopup", "Not configured")
            onPicked: page.childRequested("ocr-online")
        }

        SettingsLinkRow {
            objectName: "ocrPathsLink"
            label: qsTranslate("SettingsPopup", "Local engine paths")
            note: page.settings.ocrLocalEngine === "rapidocr" ? "RapidOCR-json" : "Tesseract"
            onPicked: page.childRequested("ocr-paths")
        }
    }
}
