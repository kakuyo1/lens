import QtQuick

/**
 * The local engine paths: the RapidOCR download and its folders, and the reader's own Tesseract.
 *
 * It is the sub-page of the OCR category that the category's "Local engine paths" row opens (UI-SPEC.md
 * 4.4). The engine choice and the recognition language stay on the OCR page; this page only holds the
 * paths and the RapidOCR state that depends on them. RapidOCR refuses a model folder whose path is not
 * ASCII because the upstream binary crashes on one (docs/adr/0149).
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    readonly property var settings: Controller.settings
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    /// The pinned distribution's download state, straight off the duty: it moves during a
    /// download, so it is read live rather than out of the settings projection.
    readonly property var bundle: Controller.captureDuty.rapidOcrBundle
                                  || ({installed: false, downloading: false, percent: 0,
                                       failure: "", ascii: true, archive: ""})

    /// @return The one line the download group says, picked in the order the voices' status picks.
    function bundleLine() {
        if (bundle.downloading)
            return qsTranslate("SettingsPopup", "Downloading %1%").arg(bundle.percent);
        if (bundle.failure === "proxy")
            return qsTranslate("UpdateCard", "Downloads from GitHub usually need a proxy in mainland China. Turn on your proxy and try again.");
        if (bundle.failure === "generic")
            return qsTranslate("SettingsPopup", "The download failed; nothing was kept.");
        if (bundle.failure === "extract")
            return qsTranslate("SettingsPopup", "The downloaded archive could not be unpacked on this Windows; it is saved at %1: unpack it and choose the folders below.").arg(bundle.archive);
        if (bundle.installed)
            return qsTranslate("SettingsPopup", "Downloaded and verified.");
        return qsTranslate("SettingsPopup", "Not downloaded. Download a local copy, or choose your own below.");
    }

    /// @return The RapidOCR detection's answer, or "" before it has run.
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

        // RapidOCR is the only user-installed local engine in this build. The upstream v0.2.0
        // binary crashes before initialization on a model path with non-ASCII characters, so the
        // folder is rejected here rather than discovered as a silent process crash.
        Column {
            width: parent.width
            spacing: 7

            Text {
                objectName: "rapidOcrBundleState"
                width: parent.width
                text: page.bundleLine()
                color: page.bundle.failure !== "" && !page.bundle.downloading ? page.tokens.danger : page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8

                Chip {
                    objectName: "downloadRapidOcrButton"
                    visible: !page.bundle.installed && !page.bundle.downloading
                    enabled: page.bundle.ascii
                    opacity: enabled ? 1.0 : 0.6
                    label: qsTranslate("SettingsPopup", "Download RapidOCR-json")
                    onPicked: Controller.captureDuty.downloadRapidOcrBundle()
                }

                Chip {
                    objectName: "cancelRapidOcrButton"
                    visible: page.bundle.downloading
                    label: qsTranslate("SettingsPopup", "Cancel")
                    onPicked: Controller.captureDuty.cancelRapidOcrBundleDownload()
                }

                Chip {
                    objectName: "removeRapidOcrButton"
                    visible: page.bundle.installed && !page.bundle.downloading
                    label: qsTranslate("SettingsPopup", "Remove")
                    onPicked: Controller.captureDuty.removeRapidOcrBundle()
                }
            }

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
                text: qsTranslate("SettingsPopup", "RapidOCR-json is a third-party program. It runs with your user permissions and can read other files or use the network; the copy the download button fetches is pinned and checksum-verified, and it is not signed.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
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
