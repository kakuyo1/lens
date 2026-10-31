import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

Item {
    id: root
    width: 380
    height: 720

    Component {
        id: pageComponent
        SettingsCapturePage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    Component {
        id: ocrPageComponent
        SettingsOcrPage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    Component {
        id: onlineOcrPageComponent
        SettingsOnlineOcrPage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    Component {
        id: consentComponent
        UploadConsent {}
    }

    Component {
        id: pathFieldComponent
        PathField { width: 328 }
    }

    TokenHost { id: host; width: root.width; height: root.height }

    TestCase {
        id: testCase
        name: "CaptureSettings"
        when: windowShown

        function make() {
            const page = createTemporaryObject(pageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            host.requestActivate();
            tryVerify(() => host.active);
            verify(!!page, "Capture page was created");
            return page;
        }

        function child(page, name) {
            const control = findChild(page, name);
            verify(!!control, "Object exists: " + name);
            return control;
        }

        function makeOcrPage() {
            const page = createTemporaryObject(ocrPageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            verify(!!page, "OCR page was created");
            return page;
        }

        function cleanup() {
            Controller.explanationDuty.dismissNotice();
            Controller.restoreCaptureDefaults();
            Controller.setSetting("theme", "dark");
            Tokens.theme = "dark";
        }

        function test_dragSensitivityIsNotExposedAsASetting() {
            const page = make();
            compare(Controller.settings.dragSensitivity, undefined);
            compare(findChild(page, "dragSensitivity"), null);
        }

        function test_minimumLengthWritesTheSelectedBoundary() {
            const page = make();
            const field = child(page, "minimumWordLength");
            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null);
            const row = Util.textWith(Util.textsInNestedWindow(field), ["5"]);
            verify(!!row, "The five-letter option exists");
            mouseClick(row.parent, row.parent.width / 2, row.parent.height / 2);
            tryVerify(() => Controller.settings.minimumWordLength === 5);
        }

        function test_whitelistEditingNormalizesAndRejectsInvalidInput() {
            const page = make();
            const field = child(page, "scanWhitelist");
            field.forceActiveFocus();
            field.text = "CHROME.EXE, firefox.exe";
            host.focusSink.forceActiveFocus();
            tryVerify(() => Controller.settings.scanWhitelist === "chrome.exe;firefox.exe");
            field.forceActiveFocus();
            field.text = "../invalid.exe";
            host.focusSink.forceActiveFocus();
            tryCompare(field, "text", "chrome.exe;firefox.exe");
        }

        /// The two paths are the reader's own Tesseract: they persist through the controller and
        /// the engine is rebuilt around them, which the availability the switches read follows.
        ///
        /// The picker itself is the running system's dialog -- FileDialog and FolderDialog are
        /// native windows a case cannot accept -- so what is checked here is the half that is
        /// this side of it: the path the controller is given is the path the field then shows,
        /// and an unchosen field shows what it would resolve to instead.
        function test_tesseractPathsReachTheController() {
            const page = makeOcrPage();
            const executable = child(page, "tesseractExecutable");
            const data = child(page, "tesseractDataDirectory");

            // Nothing chosen yet: both draw what OCR would use, in the placeholder colour.
            compare(executable.path, "", "an unchanged setting should start out unchosen");
            compare(shownPath(executable), Controller.settings.resolvedTesseractExecutable);
            compare(shownPath(data), Controller.settings.resolvedTesseractDataDirectory);
            compare(shownColour(executable), Tokens.faint, "an unchosen path is a placeholder");

            // What the dialog hands over is a url; what the setting holds is a local path. The
            // separator is the platform's, so this checks the conversion happened rather than
            // spelling out a path shape.
            Controller.setTesseractExecutable(Qt.url("file:///no-such-directory/tesseract.exe"));
            Controller.setTesseractDataDirectory(Qt.url("file:///no-such-directory/tessdata"));
            tryVerify(() => Controller.settings.tesseractExecutable.endsWith("tesseract.exe")
                             && Controller.settings.tesseractExecutable.indexOf("file:") < 0);
            tryVerify(() => Controller.settings.tesseractDataDirectory.endsWith("tessdata")
                             && Controller.settings.tesseractDataDirectory.indexOf("file:") < 0);

            compare(shownPath(executable), Controller.settings.tesseractExecutable);
            compare(shownColour(executable), Tokens.text, "a chosen path is not a placeholder");

            // Nothing is at either path, so the probe that follows the change cannot succeed and
            // the switches say so rather than offering a runtime that is not there.
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(Controller.settings.ocrAvailable, false);
        }

        /// An unchosen field is not blank: it draws where the program would look instead, in the
        /// placeholder colour, so the reader sees the default before replacing it. Driven with a
        /// fallback this case owns rather than the machine's -- on a machine with no bundled
        /// Tesseract both sides of that comparison are empty and it would prove nothing.
        function test_thePathFieldShowsWhereItWouldLook() {
            const field = createTemporaryObject(pathFieldComponent, host.contentItem);
            waitForRendering(host.contentItem);
            verify(!!field, "Path field was created");
            field.fallback = "app/ocr/tesseract.exe";

            compare(shownPath(field), field.fallback, "an unchosen field should draw the default");
            compare(shownColour(field), Tokens.faint, "a path nobody picked is a placeholder");

            field.path = "mine/tesseract.exe";
            compare(shownPath(field), field.path, "a chosen field should draw the choice");
            compare(shownColour(field), Tokens.text, "a picked path is not a placeholder");
        }

        /// @return The path @p field draws, chosen or resolved.
        function shownPath(field) {
            const text = Util.findAll(field, function (o) { return typeof o.text === "string"; })[0];
            verify(text, "the field draws no path");
            return text.text;
        }

        /// @return The colour that path is drawn in.
        function shownColour(field) {
            const text = Util.findAll(field, function (o) { return typeof o.text === "string"; })[0];
            verify(text, "the field draws no path");
            return text.color;
        }

        function test_ocrControlsUseControllerAndAvailability() {
            const page = make();
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(child(page, "ocrCapture").interactive, Controller.settings.screenshotAvailable);
            compare(child(page, "autoScan").interactive, Controller.settings.scanAvailable);
            // Auto scan is switchable as an experiment, so the status has to travel with the row.
            compare(child(page, "autoScan").badge, qsTranslate("SettingsPopup", "Experimental"));
            verify(child(page, "autoScanNote").visible);
        }

        /// Offline OCR offers exactly Auto, Tesseract and RapidOCR; the removed Windows choice
        /// must not come back through the settings list.
        function test_theLocalEngineListKeepsThreeChoices() {
            const page = makeOcrPage();
            const field = child(page, "ocrLocalEngine");
            compare(field.options.length, 3);
            compare(field.options[0].value, "auto");
            compare(field.options[1].value, "tesseract");
            compare(field.options[2].value, "rapidocr");
        }

        /// A note that is drawn sits clear of its label: the RapidOCR note is a sentence, and it
        /// used to run into "RapidOCR-json" in the list.
        function test_theLocalEngineRowsDoNotOverlapTheirNotes() {
            const page = makeOcrPage();
            const field = child(page, "ocrLocalEngine");
            field.openList();
            tryCompare(field, "listVisible", true);
            const rows = Util.findAll(field, function (o) {
                return o.toString().indexOf("DropdownRow_QMLTYPE") === 0;
            });
            compare(rows.length, 3, "the engine list did not build its three rows");
            for (let i = 0; i < rows.length; ++i) {
                const texts = Util.textsUnder(rows[i]);
                const label = texts[0];
                const note = texts[1];
                if (!note.visible)
                    continue;
                verify(label.x + label.width <= note.x,
                       "the note '" + note.text + "' runs into the label '" + label.text + "'");
            }
            field.closeList();
        }

        /// RapidOCR's v0.2.0 binary crashes before initialization when the model folder path
        /// contains non-ASCII characters. The page must reject that folder visibly, and the gate
        /// must stay shut, rather than discovering the crash in a child process.
        function test_rapidOcrRejectsANonAsciiModelFolder() {
            const page = makeOcrPage();
            const realExecutable = Qt.application.arguments[0];
            verify(realExecutable.length > 0 && realExecutable.toLowerCase().endsWith(".exe"),
                   "the test binary supplies an existing .exe for the path test");
            Controller.captureDuty.setOcrLocalEngine("rapidocr");
            Controller.setRapidOcrExecutable(Qt.url("file:///" + realExecutable.replace(/\\/g, "/")));
            Controller.setRapidOcrModelsDirectory(Qt.url("file:///C:/tmp/模型"));

            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(Controller.settings.ocrStatus, "path-unsupported");
            compare(Controller.settings.ocrAvailable, false);
            compare(Controller.settings.rapidOcrModelsAscii, false);

            const models = child(page, "rapidocrModelsDirectory");
            compare(shownPath(models), Controller.settings.rapidOcrModelsDirectory);
            const warning = child(page, "rapidocrPathWarning");
            verify(warning.visible, "the non-ASCII folder warning is shown");
            const detect = child(page, "detectRapidOcrButton");
            compare(detect.enabled, false, "detection is disabled while the folder is invalid");
        }

        /// Detection cannot succeed without an executable, and the page reports that fixed reason
        /// instead of leaving the button in a checking state.
        function test_rapidOcrDetectionWithoutAnExecutableReportsWhy() {
            Controller.captureDuty.setOcrLocalEngine("rapidocr");
            Controller.setRapidOcrExecutable(Qt.url("file:///no-such-directory/RapidOCR-json.exe"));
            Controller.setRapidOcrModelsDirectory(Qt.url("file:///C:/tmp/models"));
            Controller.detectRapidOcr();
            tryVerify(() => Controller.settings.rapidOcrDetectState !== "checking", 15000);
            compare(Controller.settings.rapidOcrDetectState, "runtime-missing");
        }

        /// The pinned download's chips follow the duty's state map: nothing downloaded in the
        /// fixture means Download alone is on, and the state line says so. No case starts a
        /// download -- the transport would reach GitHub.
        function test_theBundleChipsFollowTheDownloadState() {
            // The download group lives in the RapidOCR-only block, so the engine is named first:
            // a child of an invisible column reports visible false, whatever its own state is.
            Controller.captureDuty.setOcrLocalEngine("rapidocr");
            const page = makeOcrPage();
            const state = Controller.captureDuty.rapidOcrBundle;
            verify(!state.installed && !state.downloading,
                   "the fixture starts with no distribution on disk");
            verify(state.ascii, "the profile root is an ASCII path on this machine");

            const download = child(page, "downloadRapidOcrButton");
            compare(download.visible, true);
            verify(download.enabled, "the ASCII profile root lets the download start");
            compare(child(page, "cancelRapidOcrButton").visible, false);
            compare(child(page, "removeRapidOcrButton").visible, false);

            const stateLine = child(page, "rapidOcrBundleState");
            verify(stateLine.visible && stateLine.text !== "", "the download group says its state");
        }

        function test_theRecognitionLanguageIsWrittenAsChosen() {
            Controller.captureDuty.setOcrLocalLanguage("zh-Hans");
            tryVerify(() => Controller.settings.ocrLocalLanguage === "zh-Hans");
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(Controller.settings.ocrLocalLanguage, "zh-Hans");
        }

        /// Online mode without a configured endpoint is refused at the gate, before a rectangle
        /// is even grabbed. The refusal token is what the surface reports.
        function test_onlineModeWithoutAConfigurationRefusesTheCapture() {
            Controller.setSetting("ocrCapture", true);
            Controller.setSetting("ocrMode", "online");
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(Controller.captureRegion(Qt.rect(0, 0, 100, 50)), "online-unconfigured");
            Controller.setSetting("ocrMode", "local");
        }

        function test_theOnlinePageDrawsItsFields() {
            const page = createTemporaryObject(onlineOcrPageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            verify(page);
            const provider = child(page, "ocrOnlineProvider");
            compare(provider.options.length, 5);
            compare(provider.options[0].value, "baidu");
            compare(provider.options[1].value, "tencent");
            compare(provider.options[2].value, "aliyun");
            compare(provider.options[3].value, "ocrspace");
            compare(provider.options[4].value, "google");
            verify(findChild(page, "ocrOnlineLanguage"));
            verify(findChild(page, "testOnlineOcrButton"));
            verify(findChild(page, "clearOnlineOcrButton"));
            compare(Controller.settings.ocrOnlineLanguage, "eng");
        }

        /// Each service declares its own credential fields; the page shows what the chosen one
        /// asks for and nothing else.
        function test_theCredentialFieldsFollowTheChosenService() {
            Controller.setSetting("ocrOnlineProvider", "ocrspace");
            const page = createTemporaryObject(onlineOcrPageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            verify(page);
            verify(child(page, "ocrOnlineApiKeyField").visible);
            compare(child(page, "ocrOnlineSecretKeyField").visible, false);

            Controller.setSetting("ocrOnlineProvider", "baidu");
            tryVerify(() => child(page, "ocrOnlineSecretKeyField").visible);
            verify(child(page, "ocrOnlineApiKeyField").visible);

            // The vendor's own names for the fields, as its console spells them.
            Controller.setSetting("ocrOnlineProvider", "tencent");
            tryVerify(() => page.credentialLabel("apiKey") === "SecretId");
            compare(page.credentialLabel("secretKey"), "SecretKey");
        }

        /// The trigger-channel group with the Experimental badge on Auto scan, for the offscreen
        /// visual pass; skipped unless scripts/qa/qml-snapshot.ps1 sets LENS_QA_SNAPSHOT_DIR.
        function test_snapshotTheCapturePage() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshots");
                return;
            }
            const page = make();
            waitForRendering(host.contentItem);
            verify(Util.saveSnapshot(testCase, page, lensQaSnapshotDir, "settings-capture"));
        }

        /// Pixel stills for the two OCR pages; skipped unless scripts/qa/qml-snapshot.ps1 sets
        /// LENS_QA_SNAPSHOT_DIR. They answer glyphs, alignment and overflow, not placement.
        function test_snapshotTheOcrSettingsPages() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshots");
                return;
            }
            const local = makeOcrPage();
            waitForRendering(host.contentItem);
            verify(Util.saveSnapshot(testCase, local, lensQaSnapshotDir, "settings-ocr"));
        }

        /// The RapidOCR section with an invalid model folder, for the offscreen visual pass.
        function test_snapshotTheRapidOcrSection() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshots");
                return;
            }
            Controller.captureDuty.setOcrLocalEngine("rapidocr");
            Controller.setRapidOcrExecutable(Qt.url("file:///C:/RapidOCR/RapidOCR-json.exe"));
            Controller.setRapidOcrModelsDirectory(Qt.url("file:///C:/tmp/模型"));
            const rapid = makeOcrPage();
            waitForRendering(host.contentItem);
            verify(Util.saveSnapshot(testCase, rapid, lensQaSnapshotDir, "settings-ocr-rapidocr"));
        }

        function test_snapshotTheOnlineOcrPage() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshots");
                return;
            }
            const online = createTemporaryObject(onlineOcrPageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            verify(Util.saveSnapshot(testCase, online, lensQaSnapshotDir, "settings-ocr-online"));
        }

        /// The card without its preview: the image provider belongs to the application engine,
        /// which this target does not register. Glyphs, wrapping and button order are still the
        /// card's own.
        function test_snapshotTheUploadConsentCard() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshots");
                return;
            }
            const card = createTemporaryObject(consentComponent, host.contentItem);
            verify(card);
            card.open({requestId: 7, host: "api.ocr.space", previewUrl: ""});
            wait(350);
            verify(Util.saveSnapshot(testCase, card.contentItem, lensQaSnapshotDir, "upload-consent"));
            card.close();
        }

        /// The consent card starts on Cancel and only the explicit confirmation emits the upload
        /// request; Escape and the Cancel chip never do.
        function test_theConsentCardRequiresAnExplicitChoice() {
            const card = createTemporaryObject(consentComponent, host.contentItem);
            verify(card);
            let confirmed = 0;
            let cancelled = 0;
            card.confirmed.connect(function (id) { confirmed = id; });
            card.cancelled.connect(function () { ++cancelled; });
            card.open({requestId: 7, host: "api.ocr.space", previewUrl: "image://ocr-consent/7"});
            compare(card.visible, true);
            compare(card.request.requestId, 7);

            const cancel = findChild(card, "cancelUploadButton");
            verify(cancel);
            mouseClick(cancel, cancel.width / 2, cancel.height / 2);
            compare(confirmed, 0);
            compare(cancelled, 1);
            compare(card.visible, false);
        }

        function test_theConsentCardConfirmsWithItsRequestId() {
            const card = createTemporaryObject(consentComponent, host.contentItem);
            verify(card);
            let confirmed = 0;
            card.confirmed.connect(function (id) { confirmed = id; });
            card.open({requestId: 11, host: "api.ocr.space", previewUrl: "image://ocr-consent/11"});

            const upload = findChild(card, "confirmUploadButton");
            verify(upload);
            mouseClick(upload, upload.width / 2, upload.height / 2);
            compare(confirmed, 11);
            compare(card.busy, true);
        }

        function test_theConsentCardCanAskForOneRetry() {
            const card = createTemporaryObject(consentComponent, host.contentItem);
            verify(card);
            card.open({requestId: 12, host: "api.ocr.space", previewUrl: "image://ocr-consent/12"});
            card.showRetry({status: "timeout", canRetry: true});
            compare(card.retrying, true);
            compare(card.visible, true);
            compare(card.statusText, qsTranslate("UploadConsent", "The request timed out."));
        }
    }
}
