import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Main.qml: the root that owns every surface, and the placement and dismissal arithmetic the
 * surfaces are put up and taken down by.
 *
 * None of this could be driven at all before this target existed -- the root reads
 * Controller.settings on its first binding, and a Controller that was never provided either
 * reads as null or, in a Debug build, trips an assert in its factory and takes the process
 * down. See TEST.md section 5 for what a person was doing instead.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "Main"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// The root is a Window rather than an Item, so it is the one place a panel cannot be
        /// reached through an id: Util.ofType is what gets at one from outside.
        function panel(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        function test_theColourTableFollowsTheStoredTheme() {
            make();
            compare(Controller.settings.theme, "dark", "the fixture should be a non-default theme");

            compare(Tokens.theme, Controller.settings.theme, "Tokens did not follow the settings");
            compare(Tokens.dark, true);
        }

        /// The two halves that were measured on the real window: the hook reports physical
        /// pixels and the windows are placed in device-independent ones, and the payload has
        /// to be rebuilt rather than edited in place, because writing to the JS view of a
        /// QVariantMap silently keeps the old value.
        ///
        /// @note This runs on the offscreen platform, whose device pixel ratio is 1, so the
        ///       division here is the identity: what the case pins down is the copy and the
        ///       field set, not the arithmetic. Run with QT_SCALE_FACTOR set and the same
        ///       assertion would also cover the arithmetic.
        function test_toDipDividesThePositionAndKeepsEveryOtherField() {
            const main = make();

            const payload = { x: 250, y: 500, kind: "word", text: "ubiquitous" };
            const at = main.toDip(payload);
            const ratio = Screen.devicePixelRatio || 1;

            verify(at !== payload, "toDip handed back the payload itself; the callers divide in place through it");
            compare(at.x, 250 / ratio);
            compare(at.y, 500 / ratio);
            compare(at.kind, "word");
            compare(at.text, "ubiquitous");
            compare(payload.x, 250, "toDip wrote through to the payload the caller still holds");
        }

        /// The clamp is the whole point of the function: the panels are wider than the tray
        /// icon's distance from the screen edge, so without it a panel opened from the corner
        /// hangs off the side.
        function test_placeBesideKeepsThePanelOnTheScreenItBelongsTo() {
            const main = make();

            // The tray icon's corner, which is where every panel is opened from and the only
            // anchor placeBeside is ever handed.
            const anchor = main.trayAnchor();
            const screen = main.screenFor(anchor);
            verify(screen, "the anchor is on no screen");

            const width = 332;
            const height = 300;
            const at = main.placeBeside(anchor, width, height);

            verify(at.x >= screen.virtualX + 8, "the panel hangs off the left edge");
            verify(at.x + width <= screen.virtualX + screen.width - 8, "the panel hangs off the right edge");
            verify(at.y >= screen.virtualY + 8, "the panel hangs off the top edge");
            verify(at.y + height <= main.workArea(screen).bottom - 8, "the panel hangs under the taskbar");
        }

        /// A panel that grows after it was placed -- a row that opens, a list that loads -- keeps its
        /// top-left corner and runs off the bottom edge by what it grew. The clamp has to bring it back.
        function test_aPanelThatGrowsPastTheBottomComesBackInside() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            main.showStats();

            const area = main.workArea(main.screenFor(Qt.point(stats.x, stats.y)));
            stats.y = area.bottom - stats.height - 20;
            stats.height = stats.height + 40;
            // A window's size change reaches its onHeightChanged on the next event-loop turn.
            wait(50);

            verify(stats.y + stats.height <= area.bottom - main.screenMargin,
                   "the panel runs off the bottom edge after it grew");
        }

        /// The monitor above the primary, at 125%: Qt draws a window on it about its own origin, so a
        /// point on it converts from that origin, not from zero. Values from the reader's machine.
        function test_aPortraitScreenAboveThePrimaryConvertsFromItsOrigin() {
            const main = make();
            const portrait = {virtualX: 197, virtualY: -2560, width: 1152, height: 2048, devicePixelRatio: 1.25};

            // The release point of a selection on that monitor, in native pixels.
            const at = main.dipOn(portrait, Qt.point(876, -1336));
            verify(Math.abs(at.x - (197 + (876 - 197) / 1.25)) < 0.01, "x did not convert from the origin");
            verify(Math.abs(at.y - (-2560 + (-1336 + 2560) / 1.25)) < 0.01, "y did not convert from the origin");

            // Placed at that DIP point, a window is drawn back over the release point.
            const drawnY = portrait.virtualY + (at.y - portrait.virtualY) * portrait.devicePixelRatio;
            compare(Math.round(drawnY), -1336);
        }

        /// UI.md's "click outside closes", which no photograph can check: the four panels and
        /// the action bar each have to go when a press misses them, and the one the press
        /// landed on has to stay.
        function test_aPressOutsideClosesThePanelItMissed() {
            const main = make();
            const stats = panel(main, "StatsPopup");

            main.showStats();
            compare(stats.visible, true, "showStats() did not put the panel up");

            const card = Qt.point(stats.x + stats.width / 2, stats.y + stats.height / 2);
            main.dismissOutside(card);
            compare(stats.visible, true, "a press on the panel closed it");

            main.dismissOutside(Qt.point(0, 0));
            compare(stats.visible, false, "a press away from the panel left it up");
        }

        /// Opening one panel takes the others down, so the reader never has two on screen
        /// stacked beside each other.
        function test_openingAPanelTakesTheOtherDown() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const settings = panel(main, "SettingsPopup");

            main.showStats();
            compare(stats.visible, true);

            main.showSettings();
            compare(settings.visible, true);
            compare(stats.visible, false, "the statistics panel stayed up behind the settings panel");
        }

        function test_selectionBarRequiresTheConfiguredLetterCount() {
            const main = make();
            const bar = panel(main, "SelectionBar");
            const originalMinimum = Controller.settings.minimumWordLength;
            Controller.captureDuty.setMinimumWordLength(4);

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "abc", letterCount: 3 });
            wait(20);
            compare(bar.visible, false);

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "word", letterCount: 4 });
            tryCompare(bar, "visible", true);
            compare(bar.selectionText, "word");
            if (lensQaSnapshotDir)
                verify(Util.saveSnapshot(testCase, bar.contentItem, lensQaSnapshotDir, "selection-bar-valid"));

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "abc", letterCount: 3 });
            tryCompare(bar, "visible", false);

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "words", letterCount: 5 });
            tryCompare(bar, "visible", true);
            compare(bar.selectionText, "words");
            bar.visible = false;
            Controller.captureDuty.setMinimumWordLength(originalMinimum);
        }

        /// The menu and the two panels the statistics panel drills into all go through the same
        /// hide-then-place pair, so each one has to take the others down as well.
        function test_theMenuAndTheDrillDownPanelsAlsoClearEachOther() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const cost = panel(main, "CostPopup");
            const words = panel(main, "WordsPopup");
            const yearWords = panel(main, "YearWordsPopup");
            const menu = panel(main, "TrayMenu");

            main.showStats();

            main.showCost();
            compare(cost.visible, true, "showCost() did not put the cost panel up");
            compare(stats.visible, false);

            main.showWords();
            compare(words.visible, true, "showWords() did not put the words panel up");
            compare(cost.visible, false);

            main.showYearWords();
            compare(yearWords.visible, true, "showYearWords() did not put the annual panel up");
            compare(words.visible, false);

            main.showTrayMenu();
            compare(menu.visible, true, "showTrayMenu() did not put the menu up");
            compare(words.visible, false);
            compare(menu.contentActive, true, "the menu's rows are built only once it is opened");
            compare(menu.listVisible, false, "the language list should start folded");
        }

        /// The two arrows in the statistics panel's own corner, and the back arrows they lead to:
        /// each is a panel replacing one of the others, which no photograph can check.
        function test_theDrillDownArrowsAndTheirBackArrows() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const cost = panel(main, "CostPopup");
            const words = panel(main, "WordsPopup");

            main.showStats();
            stats.costRequested();
            compare(cost.visible, true, "the cost arrow did not open the cost panel");

            cost.backRequested();
            compare(stats.visible, true, "the cost panel's back arrow did not return to statistics");

            main.showStats();
            stats.wordsRequested();
            compare(words.visible, true, "the words arrow did not open the words panel");

            words.backRequested();
            compare(stats.visible, true, "the words panel's back arrow did not return to statistics");
        }

        /// The tray menu's two entries are the same pair driven from the menu instead: the menu
        /// only asks, and this file decides where the panel goes.
        function test_theTrayMenuEntriesOpenThePanelsTheyName() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const settings = panel(main, "SettingsPopup");
            const menu = panel(main, "TrayMenu");

            main.showTrayMenu();
            menu.statsRequested();
            compare(stats.visible, true, "the Statistics entry did not open the panel");

            main.showTrayMenu();
            menu.settingsRequested();
            compare(settings.visible, true, "the Settings entry did not open the panel");
            compare(stats.visible, false);
        }

        /// The tray icon's own gesture, which is the only way the menu opens in the product.
        function test_theTrayIconGestureOpensTheMenu() {
            const main = make();
            const menu = panel(main, "TrayMenu");

            Tray.menuRequested();

            compare(menu.visible, true, "the tray icon's gesture did not open the menu");
        }

        /// The bubble and the action bar hang on the side the reader chose. With room on both sides
        /// the chosen side is the one taken; with no room on it, the other side is. Numbers are DIPs
        /// in a 1000-high work area, a 100-high surface, a 10 px gap, and a 26 px shadow margin.
        function test_aTransientSurfaceHangsOnTheChosenSideAndFlipsOnlyWhenItHasNoRoom() {
            const main = make();
            const top = 0, bottom = 1000, height = 100, gap = 10, shadow = 26;

            // The selection sits mid-screen: both sides fit, so the reader's choice decides.
            compare(main.transientTop(500, gap, height, shadow, top, bottom, false), 500 - gap - height + shadow,
                    "above was not taken when there is room above");
            compare(main.transientTop(500, gap, height, shadow, top, bottom, true), 500 + gap - shadow,
                    "below was not taken when the reader chose it and there is room below");

            // The selection sits near the top: nothing fits above, so the chosen-above surface drops below.
            compare(main.transientTop(40, gap, height, shadow, top, bottom, false), 40 + gap - shadow,
                    "above did not flip below when there is no room above");

            // The selection sits near the bottom: nothing fits below, so the chosen-below surface rises.
            compare(main.transientTop(960, gap, height, shadow, top, bottom, true), 960 - gap - height + shadow,
                    "below did not flip above when there is no room below");
        }
    }
}
