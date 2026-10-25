import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Main.qml: the root that owns every surface, and the placement and dismissal arithmetic the
 * surfaces are put up and taken down by.
 *
 * The root reads Controller.settings on its first binding, and a Controller that was never
 * provided either reads as null or, in a Debug build, trips an assert in its factory and takes
 * the process down -- so this target is what makes the surface drivable at all. See TEST.md
 * section 5 for what a person was doing instead.
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

        /// @return Whether the two window rectangles share no pixel: one lies wholly on one side of the other.
        function separate(a, b) {
            return a.x + a.width <= b.x || b.x + b.width <= a.x
                || a.y + a.height <= b.y || b.y + b.height <= a.y;
        }

        function test_theColourTableFollowsTheStoredTheme() {
            make();
            compare(Controller.settings.theme, "dark", "the fixture should be a non-default theme");

            compare(Tokens.theme, Controller.settings.theme, "Tokens did not follow the settings");
            compare(Tokens.dark, true);
        }

        /// The hook reports physical pixels and the windows are placed in device-independent
        /// ones, and the payload has to be rebuilt rather than edited in place: writing to the
        /// JS view of a QVariantMap silently keeps the old value.
        ///
        /// @note This runs on the offscreen platform, whose device pixel ratio is 1, so the
        ///       division here is the identity: the case pins the copy and the field set, not
        ///       the arithmetic. Run with QT_SCALE_FACTOR set, the same assertion would also
        ///       cover the arithmetic.
        function test_toDipDividesThePositionAndKeepsEveryOtherField() {
            const main = make();

            const payload = { x: 250, y: 500, kind: "word", text: "ubiquitous",
                              anchorWidth: 80, anchorHeight: 40 };
            const points = panel(main, "ScreenPoints");
            const at = points.toDip(payload);
            const ratio = Screen.devicePixelRatio || 1;

            verify(at !== payload, "toDip handed back the payload itself; the callers divide in place through it");
            compare(at.x, 250 / ratio);
            compare(at.y, 500 / ratio);
            compare(at.anchorWidth, 80 / ratio);
            compare(at.anchorHeight, 40 / ratio);
            compare(at.kind, "word");
            compare(at.text, "ubiquitous");
            compare(payload.x, 250, "toDip wrote through to the payload the caller still holds");
            compare(payload.anchorWidth, 80, "toDip wrote the size through to the caller's payload");
        }

        /// The clamp is the whole point of the function: the panels are wider than the tray
        /// icon's distance from the screen edge, so without it a panel opened from the corner
        /// hangs off the side.
        function test_placeBesideKeepsThePanelOnTheScreenItBelongsTo() {
            const main = make();

            // The tray icon's corner, which is where every panel is opened from and the only
            // anchor placeBeside is ever handed.
            const anchor = Util.surfaceHost(main).trayAnchor();
            const screen = Util.surfaceHost(main).screenFor(anchor);
            verify(screen, "the anchor is on no screen");

            const width = 332;
            const height = 300;
            const at = Util.surfaceHost(main).placeBeside(anchor, width, height);

            verify(at.x >= screen.virtualX + 8, "the panel hangs off the left edge");
            verify(at.x + width <= screen.virtualX + screen.width - 8, "the panel hangs off the right edge");
            verify(at.y >= screen.virtualY + 8, "the panel hangs off the top edge");
            verify(at.y + height <= Util.surfaceHost(main).workArea(screen).bottom - 8, "the panel hangs under the taskbar");
        }

        /// A panel that grows after it was placed -- a row that opens, a list that loads -- keeps the corner it
        /// hangs from, so it grows up and away from the bottom edge. The corner is read from the window, so a
        /// panel the reader has moved keeps the corner it was left at.
        function test_aPanelThatGrowsKeepsItsBottomRightCorner() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            // The screen-layout check runs on a 250 ms timer, and its first tick re-places every visible panel from
            // its anchor. Let that tick run before the panel is shown, or it lands after the resize below and moves
            // the panel out from under the comparison.
            tryVerify(function () { return Util.surfaceHost(main).screenLayoutSignature !== ""; }, 2000);
            Util.surfaceHost(main).showStats();
            wait(50);

            const area = Util.surfaceHost(main).workArea(Util.surfaceHost(main).screenFor(Qt.point(stats.x, stats.y)));
            stats.y = area.bottom - stats.height - 20;
            const right = stats.x + stats.width;
            const bottom = stats.y + stats.height;
            stats.height = stats.height + 40;
            // A window's size change reaches its onHeightChanged on the next event-loop turn.
            wait(50);

            compare(stats.x + stats.width, right, "growing moved the right edge");
            compare(stats.y + stats.height, bottom, "growing moved the bottom edge");
        }

        /// The stack opens each panel at the corner of the one it replaces, so the card stays where it was when
        /// the reader switches and comes back, even after the first one has been moved off the tray icon.
        function test_aPanelOpenedFromAnotherLandsOnItsCorner() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const cost = panel(main, "CostPopup");

            Util.surfaceHost(main).showStats();
            stats.x = stats.x - 40;
            const corner = Qt.point(stats.x + stats.width - stats.shadowMargin, stats.y + stats.height - stats.shadowMargin);

            stats.costRequested();
            compare(cost.x + cost.width - cost.shadowMargin, corner.x, "the cost panel's right edge is not the statistics panel's");
            compare(cost.y + cost.height - cost.shadowMargin, corner.y, "the cost panel's bottom edge is not the statistics panel's");
        }

        /// The monitor above the primary, at 125%: Qt draws a window on it about its own origin, so a
        /// point on it converts from that origin, not from zero. Values from the reader's machine.
        function test_aPortraitScreenAboveThePrimaryConvertsFromItsOrigin() {
            const main = make();
            const portrait = {virtualX: 197, virtualY: -2560, width: 1152, height: 2048, devicePixelRatio: 1.25};

            // The release point of a selection on that monitor, in native pixels.
            const at = panel(main, "ScreenPoints").dipOn(portrait, Qt.point(876, -1336));
            verify(Math.abs(at.x - (197 + (876 - 197) / 1.25)) < 0.01, "x did not convert from the origin");
            verify(Math.abs(at.y - (-2560 + (-1336 + 2560) / 1.25)) < 0.01, "y did not convert from the origin");

            // Placed at that DIP point, a window is drawn back over the release point.
            const drawnY = portrait.virtualY + (at.y - portrait.virtualY) * portrait.devicePixelRatio;
            compare(Math.round(drawnY), -1336);
        }

        /// UI-SPEC.md's "click outside closes", which no photograph can check: the four panels and
        /// the action bar each have to go when a press misses them, and the one the press
        /// landed on has to stay.
        function test_aPressOutsideClosesThePanelItMissed() {
            const main = make();
            const stats = panel(main, "StatsPopup");

            Util.surfaceHost(main).showStats();
            compare(stats.visible, true, "showStats() did not put the panel up");

            const card = Qt.point(stats.x + stats.width / 2, stats.y + stats.height / 2);
            Util.surfaceHost(main).dismissOutside(card);
            compare(stats.visible, true, "a press on the panel closed it");

            Util.surfaceHost(main).dismissOutside(Qt.point(0, 0));
            compare(stats.visible, false, "a press away from the panel left it up");
        }

        /// Opening one panel takes the others down, so the reader never has two on screen
        /// stacked beside each other.
        function test_openingAPanelTakesTheOtherDown() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const settings = panel(main, "SettingsPopup");

            Util.surfaceHost(main).showStats();
            compare(stats.visible, true);

            Util.surfaceHost(main).showSettings();
            compare(settings.visible, true);
            tryVerify(function () { return !stats.visible; }, 1000, "the statistics panel stayed up behind the settings panel");
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
            if (lensQaSnapshotDir) {
                // The bar fades and lifts into place (SurfaceFade.qml), so a grab taken before
                // the token is out photographs a half-transparent card.
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, bar.contentItem, lensQaSnapshotDir, "selection-bar-valid"));
            }

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "abc", letterCount: 3 });
            tryCompare(bar, "visible", false);

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "words", letterCount: 5 });
            tryCompare(bar, "visible", true);
            compare(bar.selectionText, "words");

            // The minimum is a count of letters, and Chinese is counted in characters: a
            // two-character word is a whole word, so it reaches the bar the letter minimum
            // would have hidden it behind -- the common size of a Chinese word
            // (UI-SPEC.md section 4.10).
            Controller.selectionBarRequested({ x: 300, y: 300, kind: "translation", text: "苹果", letterCount: 2 });
            tryCompare(bar, "visible", true);
            compare(bar.selectionText, "苹果");
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

            Util.surfaceHost(main).showStats();

            Util.surfaceHost(main).showCost();
            compare(cost.visible, true, "showCost() did not put the cost panel up");
            tryCompare(stats, "visible", false);

            Util.surfaceHost(main).showWords();
            compare(words.visible, true, "showWords() did not put the words panel up");
            tryCompare(cost, "visible", false);

            Util.surfaceHost(main).showYearWords();
            compare(yearWords.visible, true, "showYearWords() did not put the annual panel up");
            tryCompare(words, "visible", false);

            Util.surfaceHost(main).showTrayMenu();
            compare(menu.visible, true, "showTrayMenu() did not put the menu up");
            tryCompare(words, "visible", false);
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

            Util.surfaceHost(main).showStats();
            stats.costRequested();
            compare(cost.visible, true, "the cost arrow did not open the cost panel");

            cost.backRequested();
            compare(stats.visible, true, "the cost panel's back arrow did not return to statistics");

            Util.surfaceHost(main).showStats();
            stats.wordsRequested();
            compare(words.visible, true, "the words arrow did not open the words panel");
            compare(words.filter, 0, "the all-time arrow should open the unfiltered list");

            Util.surfaceHost(main).showStats();
            stats.knownRequested();
            compare(words.visible, true, "the Known row did not open the words panel");
            compare(words.filter, 1, "the Known row did not carry its filter");

            Util.surfaceHost(main).showStats();
            stats.newRequested();
            compare(words.visible, true, "the New row did not open the words panel");
            compare(words.filter, 2, "the New row did not carry its filter");

            words.backRequested();
            compare(stats.visible, true, "the words panel's back arrow did not return to statistics");
        }

        /// A panel handed over to another fades out where it stands while the one opening fades in on the same corner,
        /// so the card on screen never goes faint between them. Each card is found under its window, as elsewhere here.
        function test_aHandedOverPanelFadesWhileTheNextOneFadesIn() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const cost = panel(main, "CostPopup");
            const cardOf = function (window) {
                return Util.findAll(window.contentItem, function (item) {
                    return typeof item.shown === "boolean" && typeof item.dragging === "boolean";
                })[0];
            };
            const statsCard = cardOf(stats);
            const costCard = cardOf(cost);

            Util.surfaceHost(main).showStats();

            // CI runs with reduced motion: the runner reports Windows' client-area animations
            // off. hidePanels then makes the panel go at once instead of leaving, and there is
            // no cross-fade left to measure -- so that branch is asserted here rather than
            // skipped, since nothing else covers it.
            if (Tokens.motion.reduced) {
                tryCompare(stats, "visible", true);
                stats.costRequested();
                compare(stats.leaving, false, "a panel should not be leaving when nothing fades");
                compare(stats.visible, false, "with motion reduced the panel handed over goes at once");
                compare(cost.visible, true);
                return;
            }

            tryCompare(statsCard, "opacity", 1);

            stats.costRequested();
            compare(stats.leaving, true);
            compare(stats.visible, true, "the panel handed over went at once");
            compare(cost.visible, true);

            let faintest = 1;
            let leavingFaded = 1;
            for (let i = 0; i < 15; ++i) {
                wait(16);
                faintest = Math.min(faintest, Math.max(stats.visible ? statsCard.opacity : 0, cost.visible ? costCard.opacity : 0));
                if (stats.visible)
                    leavingFaded = Math.min(leavingFaded, statsCard.opacity);
            }
            verify(faintest >= 0.4, "the stack went faint during the hand-over: " + faintest);
            verify(leavingFaded < 0.5, "the panel handed over did not fade: " + leavingFaded);
            tryCompare(stats, "visible", false);
        }

        /// A panel taken back while it is still fading stays up: its fade reverses, and the window does not go.
        function test_aPanelTakenBackMidFadeStaysUp() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const cost = panel(main, "CostPopup");

            Util.surfaceHost(main).showStats();
            stats.costRequested();
            cost.backRequested();
            compare(stats.leaving, false);
            wait(400);
            compare(stats.visible, true, "the panel taken back mid-fade went away");
            compare(cost.visible, false, "the panel that handed over stayed up");
        }

        /// The tray menu's two entries are the same pair driven from the menu instead: the menu
        /// only asks, and this file decides where the panel goes.
        function test_theTrayMenuEntriesOpenThePanelsTheyName() {
            const main = make();
            const stats = panel(main, "StatsPopup");
            const settings = panel(main, "SettingsPopup");
            const menu = panel(main, "TrayMenu");

            Util.surfaceHost(main).showTrayMenu();
            menu.statsRequested();
            compare(stats.visible, true, "the Statistics entry did not open the panel");

            Util.surfaceHost(main).showTrayMenu();
            menu.settingsRequested();
            compare(settings.visible, true, "the Settings entry did not open the panel");
            tryCompare(stats, "visible", false);
        }

        /// The tray icon's own gesture, which is the only way the menu opens in the product.
        function test_theTrayIconGestureOpensTheMenu() {
            const main = make();
            const menu = panel(main, "TrayMenu");

            Tray.menuRequested();

            compare(menu.visible, true, "the tray icon's gesture did not open the menu");
        }

        /// The pet's own gesture, wired the way the tray menu's entry is. A right press on the pet opens the settings
        /// panel on the page the pet's block lives on (UI-SPEC.md section 4.4's Extensions category), beside the pet rather
        /// than at the tray anchor the tray's own entry keeps.
        function test_aRightClickOnThePetOpensSettingsOnExtensions() {
            const main = make();
            const settings = panel(main, "SettingsPopup");
            const petWindow = panel(main, "PetWindow");

            // The window follows the pet switch, and a press reaches a window that is not on screen from nowhere.
            Pet.setEnabled(true);
            tryCompare(petWindow, "visible", true);
            wait(50);

            // The left button keeps its own job -- a press that does not move is the pet's reaction -- so it is the
            // right button and only the right button that opens the panel.
            mouseClick(petWindow.contentItem, petWindow.width / 2, petWindow.height / 2, Qt.LeftButton);
            wait(50);
            compare(settings.visible, false, "a left-click on the pet opened the settings panel");

            mouseClick(petWindow.contentItem, petWindow.width / 2, petWindow.height / 2, Qt.RightButton);

            compare(settings.visible, true, "the pet's right-click did not open the settings panel");
            compare(settings.category, "extensions", "the panel opened on a page other than the pet's own");
            verify(separate(settings, petWindow), "the panel the pet opened covers part of the pet: "
                   + settings.x + "," + settings.y + " " + settings.width + "x" + settings.height
                   + " over the pet at " + petWindow.x + "," + petWindow.y
                   + " " + petWindow.width + "x" + petWindow.height);
            Pet.setEnabled(false);
        }

        /// The side is read off where the pet is at that click, not remembered from the last one: against the left
        /// edge the card takes the room on the pet's right, and against the right edge it flips to its left. The pet
        /// is placed by hand rather than dragged, since what is under test is the arithmetic the click runs.
        function test_thePetPanelTakesTheSideThePetLeavesRoomOn() {
            const main = make();
            const settings = panel(main, "SettingsPopup");
            const petWindow = panel(main, "PetWindow");

            Pet.setEnabled(true);
            tryCompare(petWindow, "visible", true);
            wait(50);
            verify(petWindow.width > 0 && petWindow.height > 0, "the pet has no rectangle to put a card beside");

            const area = Util.surfaceHost(main).workArea(Util.surfaceHost(main).screenFor(Qt.point(petWindow.x, petWindow.y)));
            const middle = Math.round(area.top + (area.bottom - area.top - petWindow.height) / 2);
            const click = function () {
                mouseClick(petWindow.contentItem, petWindow.width / 2, petWindow.height / 2, Qt.RightButton);
                compare(settings.visible, true, "the pet's right-click did not open the settings panel");
            };

            petWindow.x = area.left + 8;
            petWindow.y = middle;
            click();
            verify(settings.x >= petWindow.x + petWindow.width, "the card did not take the room to the pet's right");
            verify(separate(settings, petWindow), "the card covers a pet parked at the left edge");

            // The same click again, with the pet at the other edge: the card is placed afresh rather than settling
            // where the first one left it.
            petWindow.x = area.right - 8 - petWindow.width;
            petWindow.y = middle;
            click();
            verify(settings.x + settings.width <= petWindow.x, "the card did not flip to the pet's left");
            verify(separate(settings, petWindow), "the card covers a pet parked at the right edge");

            Pet.setEnabled(false);
        }

        /// A throw is a window-level behaviour: the pet leaves the ground, lands, plays its dizzy reaction and stays
        /// at the landing spot, which becomes home. A still release stays the ordinary drop (ADR 0090, 0092).
        function test_aThrowLandsAndStaysThere() {
            const main = make();
            const petWindow = panel(main, "PetWindow");
            Pet.setEnabled(true);
            tryCompare(petWindow, "visible", true);
            // The entrance ceremony starts on the next frame; without this wait the idle check below can read the
            // action from before the ceremony and take the off-screen entrance start for home.
            wait(100);
            tryVerify(() => Pet.action === "idle", 6000, "the pet never settled after its entrance");
            wait(50);
            const home = Qt.point(petWindow.x, petWindow.y);

            Pet.dragStart();
            petWindow.x = home.x + 10;
            petWindow.y = home.y + 10;

            // The CI runner reports Windows' client-area animations off, so motion is reduced there and the
            // controller turns a throw into a drop. The flight geometry is covered offline (pet_flight_test);
            // assert the refusal on that branch so the integration case still says what it found.
            if (Tokens.motion.reduced) {
                verify(!Pet.release(Qt.point(1200, -1100)), "reduced motion should refuse a throw");
                Pet.dragEnd();
                Pet.savePosition(Qt.point(petWindow.x, petWindow.y));
                Pet.setEnabled(false);
                return;
            }

            verify(Pet.release(Qt.point(1200, -1100)), "the controller refused a throw at 1628 px/s");
            verify(Pet.action !== "idle", "the pet reads as idle during the throw");

            // The controller owns the window for the whole flight: it moves without the pointer.
            const launched = Qt.point(petWindow.x, petWindow.y);
            wait(80);
            verify(petWindow.x !== launched.x || petWindow.y !== launched.y, "the thrown window did not move");

            // The dog ships the optional dizzy art, so the landing is the stars rather than the poke reaction.
            tryVerify(() => Pet.action === "dizzy", 6000, "the landing did not play the dizzy action");

            // The landing is the new home: once the daze is over the pet stays on the spot it landed on.
            const landed = Qt.point(petWindow.x, petWindow.y);
            tryVerify(() => Pet.action === "idle", 6000, "the landing did not finish");
            tryVerify(() => Math.abs(petWindow.x - landed.x) <= 2
                      && Math.abs(petWindow.y - landed.y) <= 2, 1000,
                      "the pet moved after landing: at " + petWindow.x + "," + petWindow.y
                      + " instead of " + landed.x + "," + landed.y);
            verify(landed.x !== home.x || landed.y !== home.y, "the throw landed on the old home");

            // A still release is the old drop path: the controller refuses it and the caller saves the spot.
            Pet.dragStart();
            petWindow.x = home.x + 40;
            petWindow.y = home.y + 40;
            verify(!Pet.release(Qt.point(0, 0)), "a still release was taken as a throw");
            Pet.dragEnd();
            Pet.savePosition(Qt.point(petWindow.x, petWindow.y));

            Pet.setEnabled(false);
        }

        /// The bubble and the action bar hang on the side the reader chose. With room on both sides
        /// the chosen side is the one taken; with no room on it, the other side is. Numbers are DIPs
        /// in a 1000-high work area, a 100-high surface, a 10 px gap, and a 26 px shadow margin.
        function test_aTransientSurfaceHangsOnTheChosenSideAndFlipsOnlyWhenItHasNoRoom() {
            const main = make();
            const top = 0, bottom = 1000, height = 100, gap = 10, shadow = 26;

            // The selection sits mid-screen: both sides fit, so the reader's choice decides.
            compare(Util.surfaceHost(main).transientTop(500, gap, height, shadow, top, bottom, false), 500 - gap - height + shadow,
                    "above was not taken when there is room above");
            compare(Util.surfaceHost(main).transientTop(500, gap, height, shadow, top, bottom, true), 500 + gap - shadow,
                    "below was not taken when the reader chose it and there is room below");

            // The selection sits near the top: nothing fits above, so the chosen-above surface drops below.
            compare(Util.surfaceHost(main).transientTop(40, gap, height, shadow, top, bottom, false), 40 + gap - shadow,
                    "above did not flip below when there is no room above");

            // The selection sits near the bottom: nothing fits below, so the chosen-below surface rises.
            compare(Util.surfaceHost(main).transientTop(960, gap, height, shadow, top, bottom, true), 960 - gap - height + shadow,
                    "below did not flip above when there is no room below");
        }
    }
}
