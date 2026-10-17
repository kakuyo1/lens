import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

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
        name: "SettingsWave3"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        function cleanup() {
            Controller.setLevel(2);
            Controller.setExplanationLang("en");
            Controller.setMultiSense(false);
            Controller.setTheme("light");
            Controller.setAnimationsEnabled(true);
            Controller.setUiLanguage("zh");
            Controller.setSelectionCapture(true);
            Controller.setClipboardPolicy("topmost");
            Controller.modelCatalogDuty.setProvider("DeepSeek");
            Controller.setModel("deepseek-flash");
            Controller.captureDuty.setAutoScan(false);
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        function categoryRow(panel, title) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SettingsCategoryRow_QMLTYPE") === 0;
            });
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].title === title)
                    return rows[i];
            }
            return null;
        }

        function openCategory(panel, title) {
            const row = categoryRow(panel, title);
            verify(row, "the settings catalog has no " + title + " category");

            // The catalog is longer than the body, so the last categories start below the fold:
            // a press on a row that is not on screen goes past the panel. Bring it up first.
            const body = scrollerOf(row);
            if (body && body.contentHeight > body.height) {
                const bottom = row.mapToItem(body, 0, row.height).y;
                if (bottom > body.height)
                    body.contentY = Math.min(bottom - body.height, body.contentHeight - body.height);
                wait(20);
            }

            mouseClick(row, row.width / 2, row.height / 2);
            wait(20);
        }

        function back(panel) {
            const icons = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Icon_QMLTYPE") === 0 && o.source.indexOf("ui-back.svg") >= 0;
            });
            verify(icons.length > 0, "the settings page has no back control");
            mouseClick(icons[0], icons[0].width / 2, icons[0].height / 2);
            wait(20);
        }

        /// The scrolling body a page is loaded into, found by walking up from the page.
        function scrollerOf(item) {
            for (let it = item.parent; it; it = it.parent) {
                if (it.contentY !== undefined && it.contentHeight !== undefined)
                    return it;
            }
            return null;
        }

        function test_defaultsRestoreAllLocalSettingsWithoutTouchingProtectedState() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            const hasApiKey = Controller.settings.hasApiKey;
            const autostart = Controller.settings.autostart;

            Controller.setLevel(4);
            Controller.setExplanationLang("zh");
            Controller.setTheme("dark");
            Controller.setAnimationsEnabled(false);
            Controller.setSelectionCapture(false);
            Controller.setClipboardPolicy("silent");
            Controller.setBubbleDismissSeconds(20);
            Controller.setBubblePlacement("below");
            Controller.captureDuty.setAutoScan(true);
            wait(20);

            const defaults = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaults, "the settings panel has no Defaults action");
            mouseClick(defaults.parent, defaults.parent.width / 2, defaults.parent.height / 2);
            tryCompare(Controller.settings, "level", 2);
            compare(Controller.settings.explanationLang, "en");
            compare(Controller.settings.theme, "light");
            compare(Controller.settings.animationsEnabled, true);
            compare(Controller.settings.selectionCapture, true);
            compare(Controller.settings.clipboardPolicy, "topmost");
            compare(Controller.settings.bubbleDismissSeconds, 5);
            compare(Controller.settings.bubblePlacement, "above");
            compare(Controller.settings.autoScan, false);
            compare(Controller.settings.hasApiKey, hasApiKey);
            compare(Controller.settings.autostart, autostart);
        }

        function test_apiKeyFieldUsesOneCursorAcrossItsFullInputArea() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Model service");
            const apiLink = Util.ofType(panel, "SettingsLinkRow");
            verify(apiLink, "the model page has no API configuration link");
            mouseClick(apiLink, apiLink.width / 2, apiLink.height / 2);
            wait(20);
            const inputs = Util.findAll(panel, function (o) {
                return typeof o.text === "string" && o.echoMode === TextInput.Password;
            });
            verify(inputs.length > 0, "the settings panel has no API key input");

            const handlers = Util.findAll(inputs[0], function (o) {
                return o.cursorShape === Qt.PointingHandCursor;
            });
            verify(handlers.length > 0, "the API key input has no full-field hover handler");
            compare(handlers[0].cursorShape, Qt.PointingHandCursor);
        }

        function test_catalogUsesAStableFixedSurfaceAndReturnsFromAChildPage() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            compare(panel.width, 432);
            compare(panel.height, 532);

            openCategory(panel, "Capture & popups");
            const link = Util.ofType(panel, "SettingsLinkRow");
            verify(link, "the capture page has no popup link");
            mouseClick(link, link.width / 2, link.height / 2);
            wait(20);

            compare(panel.category, "capture");
            compare(panel.route, "clipboard");
            verify(panel.openChildRect === null, "a child page must not expose a dropdown window");

            const back = Util.textWith(Util.textsUnder(panel), ["Settings"]);
            verify(back, "the settings header disappeared on a child page");
        }

        /// The Account & sync row sits among labelled switches: its label starts on the same left
        /// edge as theirs. It used to carry the 9 px the catalog's rows do, which read as a line
        /// belonging to nothing beside it. The label is found by its own text rather than by being
        /// the page's only SettingsLinkRow: Font is the page's other door and sits above it.
        function test_theAccountRowLabelSharesThePagesLeftEdge() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const label = Util.textWith(Util.textsUnder(panel), ["Account & sync"]);
            verify(label, "the general page has no account row");
            const sibling = Util.textWith(Util.textsUnder(panel), ["Launch at startup"]);
            verify(sibling, "the general page has no labelled switch to compare the row with");

            compare(label.mapToItem(null, 0, 0).x, sibling.mapToItem(null, 0, 0).x,
                    "the account row's label is not on the page's left edge");
        }

        /// The about page ends with its own footer -- the name, the licence and the build stamp --
        /// and it is the panel's last line rather than one more block under the version. The panel
        /// stretches a page that ends early to the body it is loaded into, so the footer's bottom
        /// edge lands on the body's, less the 18 px the body keeps under a page.
        function test_theAboutFooterSitsOnTheBodyBottom() {
            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "About");

            const page = Util.ofType(panel, "SettingsAboutPage");
            verify(page, "the settings panel has no about page");
            const footer = Util.findAll(page, function (o) {
                return o.objectName === "aboutFooter";
            })[0];
            verify(footer, "the about page has no footer");
            const body = scrollerOf(page);
            verify(body, "the about page is not inside the panel's body");

            compare(Math.round(footer.mapToItem(page, 0, footer.height).y), Math.round(page.height),
                    "the footer is not on the page's bottom edge");
            compare(Math.round(footer.mapToItem(body, 0, footer.height).y), Math.round(body.height - 18),
                    "the footer is not on the body's bottom edge");
        }

        function test_captureTheCatalogAndDetailStates() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the settings snapshots");
                return;
            }

            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);
            const settled = Tokens.motion.pop + 80;

            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-catalog"));

            openCategory(panel, "Reading & learning");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-learning"));
            back(panel);

            openCategory(panel, "General");
            wait(settled);
            Controller.setTheme("light");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-general-light"));
            Controller.setTheme("dark");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-general-dark"));
            back(panel);

            openCategory(panel, "Capture & popups");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-capture"));
            const popupLink = Util.ofType(panel, "SettingsLinkRow");
            verify(popupLink);
            mouseClick(popupLink, popupLink.width / 2, popupLink.height / 2);
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-clipboard"));

            panel.visible = false;
            wait(20);
            main.showSettings();
            wait(settled);
            openCategory(panel, "Daily budget");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-budget"));
            back(panel);
            openCategory(panel, "About");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-about"));
            back(panel);
            openCategory(panel, "Model service");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-model"));
            const apiLink = Util.ofType(panel, "SettingsLinkRow");
            verify(apiLink);
            mouseClick(apiLink, apiLink.width / 2, apiLink.height / 2);
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-connection"));
        }

        function test_captureSettingsInBothThemes() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the settings snapshots");
                return;
            }

            const main = make();
            main.showSettings();
            const panel = settingsPanel(main);

            // The panel is a card that fades and lifts into place (ShadowCard.qml), so a grab
            // taken before that has finished photographs a half-transparent card.
            const settled = Tokens.motion.pop + 80;

            Controller.setTheme("light");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-light"));

            Controller.setTheme("dark");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-dark"));
        }
    }
}
