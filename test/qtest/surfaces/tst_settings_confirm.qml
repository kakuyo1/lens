import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The settings panel's confirmation before Defaults writes anything: the shared ConfirmDialog's
 * danger key, Cancel and Escape, and the two themes for the eye (UI-SPEC.md 4.4).
 *
 * A file of its own because tst_settings_wave3.qml sits at the size cap (docs/QML.md section 1).
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "SettingsConfirm"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        function cleanup() {
            Controller.setSetting("level", 2);
            Controller.setSetting("theme", "light");
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        /// @brief Press Defaults and return the question it raises.
        function askDefaults(panel) {
            const defaults = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaults, "the settings panel has no Defaults action");
            mouseClick(defaults.parent, defaults.parent.width / 2, defaults.parent.height / 2);
            wait(20);
            const question = Util.ofType(panel, "ConfirmDialog");
            verify(question, "Defaults wrote without asking");
            return question;
        }

        /// @return The pill @p name labels on the question.
        function button(question, name) {
            const found = Util.findAll(question, function (o) { return o.objectName === name; })[0];
            verify(found, "the question has no " + name);
            return found;
        }

        function test_defaultsRestoreAllLocalSettingsWithoutTouchingProtectedState() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            const hasApiKey = Controller.settings.hasApiKey;
            const autostart = Controller.settings.autostart;

            Controller.setSetting("level", 4);
            Controller.setSetting("explanationLang", "zh");
            Controller.setSetting("multiSense", true);
            Controller.setSetting("etymology", true);
            Controller.setSetting("example", true);
            Controller.setSetting("theme", "dark");
            Controller.setSetting("animationsEnabled", false);
            Controller.setSetting("selectionCapture", false);
            Controller.setSetting("clipboardPolicy", "silent");
            Controller.setSetting("bubbleDismissSeconds", 20);
            Controller.setSetting("bubblePlacement", "below");
            Controller.setReviewFrequency(0);
            Controller.setSetting("frostedBlur", 50);
            Controller.modelCatalogDuty.setProvider("openai");
            Controller.setModel("gpt-4.1-nano");
            Controller.captureDuty.setAutoScan(true);
            wait(20);

            const question = askDefaults(panel);
            compare(question.visible, true, "Defaults reset on the first press without asking");
            compare(Controller.settings.level, 4, "one press wrote the defaults before confirming");

            const confirm = button(question, "confirmButton");
            mouseClick(confirm, confirm.width / 2, confirm.height / 2);
            tryCompare(Controller.settings, "level", 2);
            compare(Controller.settings.explanationLang, "en");
            compare(Controller.settings.multiSense, false);
            compare(Controller.settings.etymology, false);
            compare(Controller.settings.example, false);
            compare(Controller.settings.theme, "light");
            compare(Controller.settings.animationsEnabled, true);
            compare(Controller.settings.selectionCapture, true);
            compare(Controller.settings.clipboardPolicy === "silent", false);
            compare(Controller.settings.bubbleDismissSeconds, 5);
            compare(Controller.settings.bubblePlacement, "above");
            compare(Controller.settings.reviewFrequency, 2);
            compare(Controller.settings.frostedBlur, 10);
            compare(Controller.settings.provider, "DeepSeek");
            compare(Controller.settings.model, "deepseek-flash");
            compare(Controller.settings.autoScan, true);
            compare(Controller.settings.hasApiKey, hasApiKey);
            compare(Controller.settings.autostart, autostart);
        }

        function test_defaultsCanBeCancelledBeforeAnythingIsWritten() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);

            Controller.setSetting("level", 4);
            wait(20);
            const question = askDefaults(panel);
            compare(question.visible, true, "Defaults did not ask before writing");

            const cancel = button(question, "cancelButton");
            mouseClick(cancel, cancel.width / 2, cancel.height / 2);
            tryCompare(question, "visible", false, 1000, "Cancel left the question up");

            compare(Controller.settings.level, 4, "Cancel wrote the defaults anyway");
            const defaultsAgain = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaultsAgain && defaultsAgain.parent.visible, "Defaults did not return after Cancel");
            const status = Util.findAll(panel, function (o) {
                return o.objectName === "defaultsStatus";
            })[0];
            verify(status, "the footer has no Defaults status line");

            // Escape answers the question too, and the panel under it stands.
            mouseClick(defaultsAgain.parent, defaultsAgain.parent.width / 2, defaultsAgain.parent.height / 2);
            tryCompare(question, "visible", true, 1000, "the question did not open again");
            keyClick(Qt.Key_Escape);
            tryCompare(question, "visible", false, 1000, "Escape left the question up");
            compare(Controller.settings.level, 4, "Escape wrote the defaults anyway");
            verify(panel.visible, "the question's Escape also closed the panel under it");
        }

        /// The confirmation Defaults raises, for the eye: the same card as the words panel's,
        /// narrowed to the settings card, with the danger key reading in both themes.
        function test_captureTheConfirmation() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the settings snapshots");
                return;
            }

            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            const settled = Tokens.motion.pop + 80;
            wait(settled);

            Controller.setSetting("theme", "light");
            wait(settled);
            const question = askDefaults(panel);
            wait(settled);
            verify(Util.saveSnapshot(testCase, question.contentItem, lensQaSnapshotDir, "settings-confirm-light"));

            Controller.setSetting("theme", "dark");
            wait(settled);
            verify(Util.saveSnapshot(testCase, question.contentItem, lensQaSnapshotDir, "settings-confirm-dark"));

            question.dismiss();
        }
    }
}
