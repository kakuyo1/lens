import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The settings panel's category navigation, sign-in switch and clipboard policy page
 * (UI.md section 4.4).
 *
 * The sign-in switch is only read, never driven: its setter writes HKCU, and a case that wrote
 * there would change the machine it runs on. What is assertable is the half that matters -- the
 * switch shows what the controller read back from the registry, not what was last asked for.
 *
 * The clipboard row is driven, because its setter writes the settings document and this target
 * runs against a copy of the fixture (see test/qtest/setup.cpp's writableSettingsCopy).
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
        name: "Settings"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// Every engine in the run shares one Controller and one settings document, so a case
        /// that writes one puts it back rather than leaving the next suite reading it.
        function cleanup() {
            Controller.setClipboardPolicy(false);
            Controller.setDailyBudget(0);
            Controller.setTheme("dark");
            Controller.setGlassQuality(0);
            Pet.setEnabled(false);
        }

        /// Every object under `root` that `predicate` accepts, through `data` and `children` both: a Loader's page is
        /// a child item and is not in its `data`, so Util.findAll does not reach it.
        function findDeep(root, predicate) {
            const found = [];
            const visited = [];
            (function collect(node) {
                const kids = [].concat(node.data || [], node.children || []);
                for (let i = 0; i < kids.length; ++i) {
                    if (visited.indexOf(kids[i]) >= 0)
                        continue;
                    visited.push(kids[i]);
                    if (predicate(kids[i]))
                        found.push(kids[i]);
                    collect(kids[i]);
                }
            })(root);
            return found;
        }

        /// The settings page the field scrolls in: the nearest ancestor that has a content position.
        function scrollerOf(field) {
            for (let it = field.parent; it; it = it.parent) {
                if (it.contentY !== undefined && it.contentHeight !== undefined)
                    return it;
            }
            return null;
        }

        /// The panel on the extensions page with the pet type dropdown in it, the way the reader reaches it.
        function openPetPanel() {
            Pet.setEnabled(true);
            const main = make();
            const panel = settingsPanel(main);
            Util.surfaceHost(main).showSettings();
            wait(25);
            panel.openCategory("extensions");
            wait(25);
            return {main: main, panel: panel};
        }

        function petTypeField(panel) {
            const fields = findDeep(panel, function (o) {
                return o.toString().indexOf("DropdownField_QMLTYPE") === 0 && o.options.length > 0;
            });
            verify(fields.length > 0, "the extensions page has no dropdown to test");
            return fields[0];
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
            mouseClick(row, row.width / 2, row.height / 2);
            wait(20);
        }

        function switchRowLabelled(panel, label) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SwitchRow_QMLTYPE") === 0;
            });
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].label === label)
                    return rows[i];
            }
            return null;
        }

        function sliderRowLabelled(panel, label) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SliderRow_QMLTYPE") === 0;
            });
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].label === label)
                    return rows[i];
            }
            return null;
        }

        /// @return The Segment whose first label is `first`, or null.
        function segmentLabelled(panel, first) {
            const segments = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Segment_QMLTYPE") === 0;
            });
            for (let i = 0; i < segments.length; ++i) {
                if (segments[i].labels[0] === first)
                    return segments[i];
            }
            return null;
        }

        function test_theSignInSwitchShowsWhatTheRegistrySays() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const row = switchRowLabelled(panel, "Launch at startup");
            verify(row, "the settings panel has no sign-in switch");
            compare(row.checked, Controller.settings.autostart,
                    "the switch is not showing the state read back from the registry");
        }

        function test_theClipboardSegmentPicksThePolicyAndFollowsItBack() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Capture & popups");

            const link = Util.ofType(panel, "SettingsLinkRow");
            verify(link, "the capture page has no popup link");
            mouseClick(link, link.width / 2, link.height / 2);
            wait(20);

            // The fixture carries no policy, so it starts on the default. Pinned here rather
            // than assumed, so the case reads the same whatever order the suites run in.
            Controller.setClipboardPolicy(false);
            wait(20);

            const segment = segmentLabelled(panel, "Raise to top");
            verify(segment, "the settings panel has no clipboard segment");
            compare(segment.labels.length, 2, "the clipboard policy is a two-way choice");
            compare(segment.currentIndex, 0, "the segment did not start on the default policy");

            const cell = Util.textWith(Util.textsUnder(segment), ["Give up silently"]).parent;
            mouseClick(cell, cell.width / 2, cell.height / 2);
            wait(20);

            compare(Controller.settings.clipboardSilent, true,
                    "the pick did not reach setClipboardPolicy");
            compare(segment.currentIndex, 1, "the segment did not follow the policy it wrote");
            compare(Util.textWith(Util.textsUnder(panel), ["Popup frequency"]), null);
        }

        function test_animationSwitchControlsTheMotionTokens() {
            Controller.setAnimationsEnabled(true);
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const row = switchRowLabelled(panel, "Animations");
            verify(row, "the General page has no animation switch");
            compare(row.checked, Controller.settings.animationsEnabled);

            const switcher = Util.ofType(row, "Switch");
            verify(switcher, "the animation row has no switch control");
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);
            tryCompare(Controller.settings, "animationsEnabled", false);
            compare(Tokens.motion.press, 0);
            compare(Tokens.motion.pop, 0);
            compare(Tokens.motion.bubble, 0);

            Controller.setAnimationsEnabled(true);
            tryCompare(row, "checked", true);
            compare(Tokens.motion.press, SystemMotion.reduced ? 0 : 150);
            compare(Tokens.motion.pop, SystemMotion.reduced ? 0 : 180);
            compare(Tokens.motion.bubble, SystemMotion.reduced ? 0 : 220);
        }

        /// The typing speed has no pace to set once motion is off, so its row is shown with the
        /// switch that governs motion and hidden with it, rather than left as a control that moves
        /// without doing anything.
        function test_theTypingSpeedRowFollowsTheAnimationSwitch() {
            Controller.setAnimationsEnabled(true);
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const row = sliderRowLabelled(panel, "Typing speed");
            verify(row, "the General page has no typing speed row");
            compare(row.visible, true, "the row was hidden while animations were on");
            verify(row.height > 0, "the row took no room while animations were on");

            Controller.setAnimationsEnabled(false);
            tryCompare(row, "visible", false);
            tryCompare(row, "height", 0);

            Controller.setAnimationsEnabled(true);
            tryCompare(row, "visible", true);
        }

        function test_theDailyBudgetIsReachableAndPersistsThroughTheController() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Daily budget");

            const page = Util.ofType(panel, "SettingsBudgetPage");
            verify(page, "the settings catalog has no daily budget page");
            compare(page.dailyBudget, Controller.settings.dailyBudget);

            Controller.setDailyBudget(1.25);
            wait(20);
            compare(Controller.settings.dailyBudget, 1.25);
            compare(page.dailyBudget, 1.25);
        }

        /// The list is a window of its own. When the page scrolls under it the field moves, and the list has to follow to
        /// keep the same distance below the field. It follows within one timer tick, so the case waits for it to settle.
        function test_theListStaysUnderItsFieldWhenThePageScrolls() {
            const shown = openPetPanel();
            const field = petTypeField(shown.panel);
            const scroller = scrollerOf(field);
            verify(scroller, "the field is not inside a scrolling page");
            verify(scroller.contentHeight > scroller.height, "the page does not scroll, so there is nothing to test");

            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null, 2000, "the field did not open its list");

            scroller.contentY = Math.min(40, scroller.contentHeight - scroller.height);

            // The card's top sits the field's height and six pixels below the field's own top, in window coordinates
            // plus the window's place on screen.
            const below = () => {
                const at = field.mapToItem(null, 0, 0);
                return Math.round(field.Window.window.y + at.y + field.height + 6);
            };
            // One pixel of slack: the field's fractional position and the list window's integer
            // one round on opposite sides at 125% scaling. The rule is "the list follows the
            // field", not "both round the same way".
            tryVerify(() => Math.abs(Math.round(field.openChildRect.y) - below()) <= 1, 2000,
                      "the list did not follow the field when the page scrolled");
        }

        /// A press outside the panel puts down the list the panel opened, the same as it puts the panel down. The pet page
        /// owns a dropdown but does not hand its list up to the panel, so the press went past the list and it stayed up.
        function test_aPressOutsideThePanelPutsThePetListDown() {
            const shown = openPetPanel();
            const field = petTypeField(shown.panel);
            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null, 2000, "the field did not open its list");

            Util.surfaceHost(shown.main).dismissOutside(Qt.point(shown.panel.x - 40, shown.panel.y - 40));

            compare(shown.panel.visible, false, "the press did not put the panel down, so the case proves nothing");
            tryVerify(() => field.openChildRect === null, 2000, "a press outside the panel left its dropdown list up");
        }

        /// The learning page's word review dropdown, which is the same kind of list as the pet page's.
        function openLearningPanel() {
            const main = make();
            const panel = settingsPanel(main);
            Util.surfaceHost(main).showSettings();
            wait(25);
            panel.openCategory("learning");
            wait(25);
            return {main: main, panel: panel};
        }

        function reviewFieldOf(panel) {
            const fields = findDeep(panel, function (o) {
                return o.toString().indexOf("DropdownField_QMLTYPE") === 0 && o.objectName === "reviewField";
            });
            verify(fields.length > 0, "the learning page has no word review dropdown");
            return fields[0];
        }

        function test_aPressOutsideThePanelPutsTheWordReviewListDown() {
            Pet.setEnabled(true);
            const shown = openLearningPanel();
            const field = reviewFieldOf(shown.panel);
            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null, 2000, "the field did not open its list");

            Util.surfaceHost(shown.main).dismissOutside(Qt.point(shown.panel.x - 40, shown.panel.y - 40));

            compare(shown.panel.visible, false, "the press did not put the panel down, so the case proves nothing");
            tryVerify(() => field.openChildRect === null, 2000, "a press outside the panel left the word review list up");
        }

        /// A press on the list belongs to the list: neither the panel nor the list goes down, so the pick can land.
        function test_aPressOnTheWordReviewListLeavesThePanelUp() {
            Pet.setEnabled(true);
            const shown = openLearningPanel();
            const field = reviewFieldOf(shown.panel);
            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null, 2000, "the field did not open its list");

            const rect = field.openChildRect;
            Util.surfaceHost(shown.main).dismissOutside(Qt.point(rect.x + rect.width / 2, rect.y + rect.height / 2));

            compare(shown.panel.visible, true, "a press on the list put the panel down");
            verify(field.openChildRect !== null, "a press on the list put the list down before it could be picked");
        }

        /// The backdrop refresh list was the one dropdown the General page neither reported as its open child nor
        /// closed with itself, because it arrived on a page that already had a dropdown. A list is a window of its
        /// own: unreported, a press outside it fell through to the panel, and unclosed, it outlived the panel.
        function test_theBackdropRefreshListGoesDownWithThePanel() {
            Controller.setTheme("frosted");
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const field = findDeep(panel, function (o) {
                return o.objectName === "glassQualityField";
            })[0];
            verify(field, "the General page has no backdrop refresh dropdown");

            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null, 2000, "the field did not open its list");
            verify(panel.openChildRect !== null,
                   "the panel did not report the refresh list as its open child");

            panel.visible = false;
            wait(50);
            tryVerify(() => field.openChildRect === null, 2000,
                      "closing the panel left the refresh list up");
        }

        function test_aQuestionOnThePanelTakesEscapeBeforeTheCategoryDoes() {
            const shown = openPetPanel();
            const question = findDeep(shown.panel, function (o) {
                return o.toString().indexOf("PetPackageDialog_QMLTYPE") === 0;
            });
            verify(question.length > 0, "the extensions page has no pet package question");
            question[0].open();
            wait(20);

            shown.panel.navigateBack();

            compare(question[0].opened, false, "Escape did not answer the question");
            compare(shown.panel.category, "extensions", "Escape answered the question and also backed out of the category");
            shown.panel.navigateBack();
            compare(shown.panel.category, "", "the next Escape did not back out of the category");
        }
    }
}
