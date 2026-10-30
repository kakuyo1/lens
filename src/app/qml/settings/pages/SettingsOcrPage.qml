import QtQuick

/**
 * The local OCR configuration page; the surface spec is UI-SPEC.md 4.4.
 *
 * The page owns the engine, the recognition language and the local engine paths; the first capture
 * page keeps only the readiness summary and the link that opens this page. Auto is the default and
 * resolves to Tesseract. An explicit choice is never rewritten, so an unavailable one says why
 * rather than quietly falling back. RapidOCR is user-installed, needs an explicit detection, and
 * refuses a model folder whose path is not ASCII because the upstream binary crashes on one.
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

    /// @return The explicit RapidOCR detection's answer, or "" before it has run.
    function detectLine() {
        if (settings.rapidOcrDetectState === "idle" || settings.rapidOcrDetectState === "checking")
            return "";
        if (settings.rapidOcrDetectState === "ready")
            return qsTranslate("SettingsPopup", "RapidOCR is trusted and ready (%1 ms).").arg(settings.rapidOcrDetectMs);
        if (settings.rapidOcrDetectState === "path-unsupported")
            return qsTranslate("SettingsPopup", "RapidOCR's model folder path must contain only ASCII characters.");
        if (settings.rapidOcrDetectState === "runtime-missing")
            return qsTranslate("SettingsPopup", "RapidOCR is not installed.");
        if (settings.rapidOcrDetectState === "language-missing")
            return qsTranslate("SettingsPopup", "RapidOCR's model files are missing.");
        if (settings.rapidOcrDetectState === "empty-text")
            return qsTranslate("SettingsPopup", "RapidOCR could not read the test image.");
        if (settings.rapidOcrDetectState === "timeout")
            return qsTranslate("SettingsPopup", "RapidOCR did not finish in time.");
        return qsTranslate("SettingsPopup", "Detection failed; RapidOCR is not trusted.");
    }

    Column {
        id: content
        width: parent.width
        spacing: 14

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
                text: qsTranslate("SettingsPopup", "Experimental auto scan always uses this local engine and never an online service.")
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

        // RapidOCR is the only user-installed local engine in this build. The upstream v0.2.0
        // binary crashes before initialization on a model path with non-ASCII characters, so the
        // folder is rejected here rather than discovered as a silent process crash.
        Column {
            width: parent.width
            spacing: 7
            visible: page.settings.ocrLocalEngine === "rapidocr"

            Text {
                text: qsTranslate("SettingsPopup", "RapidOCR executable")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            PathField {
                id: rapidOcrExecutableField
                width: parent.width
                objectName: "rapidocrExecutable"
                label: qsTranslate("SettingsPopup", "RapidOCR executable")
                path: page.settings.rapidOcrExecutable
                onChosen: (picked) => Controller.setRapidOcrExecutable(picked)
            }

            Text {
                text: qsTranslate("SettingsPopup", "RapidOCR model folder")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            PathField {
                id: rapidOcrModelsField
                width: parent.width
                objectName: "rapidocrModelsDirectory"
                label: qsTranslate("SettingsPopup", "RapidOCR model folder")
                path: page.settings.rapidOcrModelsDirectory
                folder: true
                onChosen: (picked) => Controller.setRapidOcrModelsDirectory(picked)
            }

            Text {
                objectName: "rapidocrPathWarning"
                width: parent.width
                visible: page.settings.rapidOcrModelsDirectory.length > 0
                         && !page.settings.rapidOcrModelsAscii
                text: qsTranslate("SettingsPopup", "RapidOCR's model folder path must contain only ASCII characters. Move the models to an ASCII-only path and choose it again.")
                color: page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Chip {
                objectName: "detectRapidOcrButton"
                label: page.settings.rapidOcrDetectState === "checking"
                       ? qsTranslate("SettingsPopup", "Detecting…")
                       : qsTranslate("SettingsPopup", "Detect and trust")
                enabled: page.settings.rapidOcrExecutable.length > 0
                         && page.settings.rapidOcrModelsDirectory.length > 0
                         && page.settings.rapidOcrModelsAscii
                         && page.settings.rapidOcrDetectState !== "checking"
                opacity: enabled ? 1.0 : 0.6
                onPicked: Controller.detectRapidOcr()
            }

            Text {
                objectName: "rapidOcrDetectResult"
                width: parent.width
                visible: text !== ""
                text: page.detectLine()
                color: page.settings.rapidOcrDetectState === "ready" ? page.tokens.muted : page.tokens.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "RapidOCR-json is a third-party program. It runs with your user permissions and can read other files or use the network; Lens does not install or update it.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        SettingsLinkRow {
            objectName: "onlineOcrLink"
            label: qsTranslate("SettingsPopup", "Online OCR")
            note: page.settings.onlineConfigured
                  ? page.settings.ocrOnlineHost
                  : qsTranslate("SettingsPopup", "Not configured")
            onPicked: page.childRequested("ocr-online")
        }

        // The bundled runtime is what the installer lays down; these two are for a reader who
        // already keeps their own Tesseract. They are hidden for an explicit RapidOCR choice,
        // which does not use them.
        Column {
            width: parent.width
            spacing: 7
            visible: page.settings.ocrLocalEngine !== "rapidocr"
            Text { text: qsTranslate("SettingsPopup", "Tesseract executable"); color: page.tokens.muted; font.pixelSize: 12 }
            PathField {
                id: tesseractField
                width: parent.width
                objectName: "tesseractExecutable"
                label: qsTranslate("SettingsPopup", "Tesseract executable")
                path: page.settings.tesseractExecutable
                fallback: page.settings.resolvedTesseractExecutable
                onChosen: (picked) => Controller.setTesseractExecutable(picked)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            visible: page.settings.ocrLocalEngine !== "rapidocr"
            Text { text: qsTranslate("SettingsPopup", "Tesseract data folder"); color: page.tokens.muted; font.pixelSize: 12 }
            PathField {
                id: tesseractDataField
                width: parent.width
                objectName: "tesseractDataDirectory"
                label: qsTranslate("SettingsPopup", "Tesseract data folder")
                path: page.settings.tesseractDataDirectory
                fallback: page.settings.resolvedTesseractDataDirectory
                folder: true
                onChosen: (picked) => Controller.setTesseractDataDirectory(picked)
            }
        }

    }
}
