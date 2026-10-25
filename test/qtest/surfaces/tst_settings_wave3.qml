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
            Controller.setSetting("level", 2);
            Controller.setSetting("explanationLang", "en");
            Controller.setSetting("multiSense", false);
            Controller.setSetting("theme", "light");
            Controller.setSetting("animationsEnabled", true);
            Controller.setSetting("uiLanguage", "zh");
            Controller.setSetting("selectionCapture", true);
            Controller.setSetting("clipboardPolicy", "topmost");
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

            const defaults = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaults, "the settings panel has no Defaults action");
            mouseClick(defaults.parent, defaults.parent.width / 2, defaults.parent.height / 2);
            wait(20);
            verify(panel.confirmingDefaults, "Defaults reset on the first press without asking");
            compare(Controller.settings.level, 4, "one press wrote the defaults before confirming");

            const confirm = Util.findAll(panel, function (o) {
                return o.objectName === "confirmDefaultsButton";
            })[0];
            verify(confirm, "Defaults did not offer a Confirm after the first press");
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
            compare(Controller.settings.autoScan, false);
            compare(Controller.settings.hasApiKey, hasApiKey);
            compare(Controller.settings.autostart, autostart);
        }

        function test_defaultsCanBeCancelledBeforeAnythingIsWritten() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);

            Controller.setSetting("level", 4);
            wait(20);
            const defaults = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaults, "the settings panel has no Defaults action");
            mouseClick(defaults.parent, defaults.parent.width / 2, defaults.parent.height / 2);
            wait(20);

            const cancel = Util.findAll(panel, function (o) {
                return o.objectName === "cancelDefaultsButton";
            })[0];
            verify(cancel, "Defaults did not offer a Cancel after the first press");
            mouseClick(cancel, cancel.width / 2, cancel.height / 2);
            wait(20);

            compare(panel.confirmingDefaults, false, "Cancel left the question up");
            compare(Controller.settings.level, 4, "Cancel wrote the defaults anyway");
            const defaultsAgain = Util.textWith(Util.textsUnder(panel), ["Defaults"]);
            verify(defaultsAgain && defaultsAgain.parent.visible, "Defaults did not return after Cancel");
            const status = Util.findAll(panel, function (o) {
                return o.objectName === "defaultsStatus";
            })[0];
            verify(status, "the footer has no Defaults status line");
            compare(status.elide, Text.ElideNone, "the confirmation still ends in an ellipsis");
            compare(status.wrapMode, Text.WordWrap);
            const defaultsButton = Util.findAll(panel, function (o) {
                return o.objectName === "defaultsButton";
            })[0];
            verify(defaultsButton, "Defaults did not return as the button");
            compare(defaultsButton.activeFocus, false, "Cancel put the focus ring back on Defaults");
        }

        /// The daily budget has no stored default to restore, so the page that owns it must not
        /// offer a reset that would silently do nothing.
        function test_theBudgetPageOffersNoDefaultsAction() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Daily budget");

            compare(panel.defaultsAvailable, false, "the budget page still counts Defaults as available");
            const row = Util.findAll(panel, function (o) {
                return o.objectName === "defaultsRow";
            })[0];
            verify(row, "the settings footer has no Defaults row");
            compare(row.visible, false, "the budget page still draws the Defaults row");
        }

        /// The budget page reads back what today has cost and what the cap is, and says what
        /// happens at the cap. The numbers come from the duty's own stats, not a second calculation.
        function test_theBudgetPageShowsTodaysSpendAndWhatTheCapDoes() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Daily budget");

            const original = Controller.settings.dailyBudget;
            const spent = Controller.costDuty.stats.currency
                        + Controller.costDuty.stats.todayCost.toFixed(2);
            try {
                Controller.setDailyBudget(5);
                wait(20);
                const spend = Util.findAll(panel, function (o) {
                    return o.objectName === "budgetSpend";
                })[0];
                const cap = Util.findAll(panel, function (o) {
                    return o.objectName === "budgetCap";
                })[0];
                verify(spend && cap, "the budget page has no spend or cap line");
                compare(spend.text, "Today " + spent + " · " + Controller.costDuty.stats.currency + "5.00 cap");
                compare(cap.text, "At the cap, lookups and requests pause until tomorrow.");

                Controller.setDailyBudget(0);
                wait(20);
                compare(spend.text, "Today " + spent + " · no cap");
                compare(cap.text, "No cap; set an amount to pause at the limit.");
            } finally {
                Controller.setDailyBudget(original);
            }
        }

        /// A slider and a pick-only dropdown are keyboard surfaces, not pointer-only ones. This
        /// pins the arrow/Enter path the row's own Tab stop hands them.
        function test_keyboardDrivesASliderAndAChoiceList() {            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);

            Controller.setSetting("animationsEnabled", true);
            Controller.setSetting("revealSpeed", 3);
            openCategory(panel, "General");
            wait(20);
            const slider = Util.findAll(panel, function (o) {
                return o.toString().indexOf("StepSlider_QMLTYPE") === 0
                       && o.accessibleName === "Typing speed";
            })[0];
            verify(slider, "the general page has no typing-speed slider");
            compare(slider.activeFocusOnTab, true, "the slider is not in the tab order");
            compare(slider.nextValue(Qt.Key_Right, 3), 4, "Right did not step forward");
            compare(slider.nextValue(Qt.Key_Left, 3), 2, "Left did not step back");
            compare(slider.nextValue(Qt.Key_Home, 3), 1, "Home did not take the floor");
            compare(slider.nextValue(Qt.Key_End, 3), 5, "End did not take the ceiling");
            Controller.setSetting("revealSpeed", 3);

            panel.navigateBack();
            wait(20);
            Controller.setSetting("selectionBarStyle", "bar");
            openCategory(panel, "Capture & popups");
            const field = Util.findAll(panel, function (o) {
                return o.objectName === "selectionBarStyle";
            })[0];
            verify(field, "the capture page has no selection-bar-style field");
            field.openList();
            tryCompare(field, "listVisible", true);
            const down = {key: Qt.Key_Down, isAutoRepeat: false, accepted: false};
            verify(field.handleListKey(down), "Down was not treated as a list key");
            compare(field.highlightIndex, 1, "Down did not move the list's highlight");
            const enter = {key: Qt.Key_Return, isAutoRepeat: false, accepted: false};
            verify(field.handleListKey(enter), "Return was not treated as a list key");
            wait(20);
            compare(Controller.settings.selectionBarStyle, "tab",
                    "Return did not take the highlighted choice");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// A tall page shows its scrollbar, and focus on a control below the fold brings that
        /// control back into the viewport rather than leaving the ring off screen.
        function test_theBodyShowsAScrollbarAndFollowsFocus() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");
            wait(20);

            const thumb = Util.findAll(panel, function (o) {
                return o.objectName === "scrollThumb";
            })[0];
            verify(thumb, "the settings body has no scrollbar");
            compare(thumb.visible, true, "a page taller than the body drew no scrollbar");

            const body = Util.findAll(panel, function (o) {
                return o.contentY !== undefined && o.contentHeight !== undefined;
            })[0];
            verify(body, "the settings body is not a Flickable");
            const pictures = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Switch_QMLTYPE") === 0 && o.accessibleName === "Pictures";
            })[0];
            verify(pictures, "the learning page has no Pictures switch");
            pictures.forceActiveFocus();
            tryVerify(function () {
                const at = pictures.mapToItem(body.contentItem, 0, 0).y;
                return at >= body.contentY - 1
                       && at + pictures.height <= body.contentY + body.height + 1;
            }, 1000, "focus stayed below the visible body");
        }

        /// The thumb measures the body, so it has to stay in the body's viewport while the content
        /// moves under it. A thumb declared inside the Flickable scrolls away with the content and
        /// reads as a bar that never moves.
        function test_theScrollThumbTracksTheBodyAndStaysInItsViewport() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");
            wait(20);

            const thumb = Util.findAll(panel, function (o) {
                return o.objectName === "scrollThumb";
            })[0];
            verify(thumb, "the settings body has no scrollbar");
            const body = Util.findAll(panel, function (o) {
                return o.contentY !== undefined && o.contentHeight !== undefined;
            })[0];
            verify(body, "the settings body is not a Flickable");
            verify(body.contentHeight > body.height, "the fixture page does not scroll");

            const bodyTop = body.mapToItem(panel.contentItem, 0, 0).y;
            body.contentY = 0;
            wait(20);
            const atTop = thumb.mapToItem(panel.contentItem, 0, 0).y;

            body.contentY = Math.min(120, body.contentHeight - body.height);
            wait(20);
            const atDown = thumb.mapToItem(panel.contentItem, 0, 0).y;
            verify(atDown > atTop, "the thumb moved with the content instead of down the track");

            body.contentY = body.contentHeight - body.height;
            wait(20);
            const atBottom = thumb.mapToItem(panel.contentItem, 0, 0).y;
            verify(atBottom >= bodyTop - 1, "the thumb left the body at the top");
            verify(atBottom + thumb.height <= bodyTop + body.height + 1,
                   "the thumb left the body at the bottom");
        }

        function test_apiKeyFieldUsesOneCursorAcrossItsFullInputArea() {
            const main = make();
            Util.surfaceHost(main).showSettings();
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
            compare(inputs[0].clip, true, "a long API key can paint outside its field");

            const handlers = Util.findAll(inputs[0], function (o) {
                return o.cursorShape === Qt.PointingHandCursor;
            });
            verify(handlers.length > 0, "the API key input has no full-field hover handler");
            compare(handlers[0].cursorShape, Qt.PointingHandCursor);
        }

        function test_catalogUsesAStableFixedSurfaceAndReturnsFromAChildPage() {
            const main = make();
            Util.surfaceHost(main).showSettings();
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
        /// edge as theirs, not at the 9 px indent the catalog's rows carry. The label is found by
        /// its own text rather than by being the page's only SettingsLinkRow: Font is the page's
        /// other door and sits above it.
        function test_theAccountRowLabelSharesThePagesLeftEdge() {
            const main = make();
            Util.surfaceHost(main).showSettings();
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
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            // The route reaches the page without pressing a catalog row at the fold: this case is
            // about the footer's place, not about the scroll position a press needs.
            panel.routeTo("about");
            wait(20);

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
            Util.surfaceHost(main).showSettings();
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
            Controller.setSetting("theme", "light");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-general-light"));
            Controller.setSetting("theme", "dark");
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
            Util.surfaceHost(main).showSettings();
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
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);

            // The panel is a card that fades and lifts into place (ShadowCard.qml), so a grab
            // taken before that has finished photographs a half-transparent card.
            const settled = Tokens.motion.pop + 80;

            Controller.setSetting("theme", "light");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-light"));

            Controller.setSetting("theme", "dark");
            wait(settled);
            verify(Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "settings-dark"));
        }
    }
}
