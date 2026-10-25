import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The action bar's press, which is the case the components target had to leave behind.
 *
 * Pressing one of the items also arms the card's DragHandler, whose grab reads
 * Controller.cursorPos(); with no Controller behind it that read trips an assert in the
 * singleton's factory and takes the process down, so only this target can check the press. The
 * bug pinned here: the item read `pressed` off a HoverHandler, which has no such property, so
 * the press never shrank it and nothing but an eye could tell.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: barComponent
        SelectionBar { selectionText: "ubiquitous" }
    }

    TestCase {
        name: "SelectionBar"
        when: windowShown

        function opened() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            // Through open(), not `visible = true`: the content is disabled while the bar is not
            // meant to be up, so a direct show would draw it without taking the press.
            bar.open();
            wait(50);
            return bar;
        }

        /// @return The bar's three items, in the order they are drawn.
        function itemsOf(bar) {
            return Util.textsUnder(bar).map(function (t) { return t.parent.parent; });
        }

        function test_holdingAnItemShrinksIt() {
            const bar = opened();
            const item = itemsOf(bar)[0];

            compare(item.scale, 1.0, "an item should sit at its full size until it is pressed");

            mousePress(item, item.width / 2, item.height / 2, Qt.LeftButton);
            // The scale animates over 150 ms, so the press is a state to wait for rather than
            // a value to read.
            tryVerify(function () { return item.scale < 0.98; }, 1000);
            mouseRelease(item, item.width / 2, item.height / 2, Qt.LeftButton);

            tryVerify(function () { return item.scale > 0.99; }, 1000);
        }

        /// With nothing selected there is no action to take, so the controller declines and the
        /// clipboard is untouched -- which is why this case can tap an item at all.
        function test_tappingAnItemTakesTheBarDown() {
            const bar = opened();

            const item = itemsOf(bar)[2];
            mouseClick(item, item.width / 2, item.height / 2);

            // The tap starts the fade; the window goes when the fade has run (SurfaceFade.qml).
            tryCompare(bar, "visible", false, 1000, "the bar stayed up after one of its items was tapped");
        }

        /// @return The bar, up in @p style. The stored style is left as the test found it only
        ///         when the test runs to its end, so these cases stay last in the file.
        function openedWithStyle(style) {
            Controller.setSetting("selectionBarStyle", style);
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            bar.open();
            wait(50);
            return bar;
        }

        /// The first arrival of a shape teaches it; the next one in the same run is half the time
        /// (ADR 0109). The factor is set before the body starts, so the animation reads one number.
        function test_aShapeEntersInFullOnceThenInHalf() {
            const bar = openedWithStyle("tag");
            compare(bar.entranceFactor, 1, "the first tag entrance was already shortened");

            bar.dismiss();
            bar.open();
            compare(bar.entranceFactor, 0.5, "the second tag entrance was not halved");

            Controller.setSetting("selectionBarStyle", "fan");
            bar.open();
            compare(bar.entranceFactor, 1, "a different shape's first entrance was halved");

            bar.dismiss();
            bar.open();
            compare(bar.entranceFactor, 0.5, "the second fan entrance was not halved");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_theTabItemShiftsAwayFromItsSpine() {
            const bar = openedWithStyle("tab");
            const body = bar.bodyItem;
            verify(body);
            // The staggered entrance slides the items along the same x; it has to have settled
            // before the shift's direction can be read on its own.
            wait(500);

            const item = itemsOf(bar)[0];
            bar.sideLeft = false;
            body.playPress("translate");
            tryVerify(function () { return item.transform[0].x > 5; }, 1000,
                      "a press on the right-side tab should send the item outward, to the right");
            wait(250);

            bar.sideLeft = true;
            body.playPress("translate");
            tryVerify(function () { return item.transform[0].x < -5; }, 1000,
                      "with the tab flipped to the left the same press should lift the other way");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// The window takes no focus, so the actions cannot be Tabbed to; the screen reader still
        /// has to find all three in every form. The names are the action labels themselves.
        function test_everyFormNamesItsThreeActions() {
            for (const style of ["bar", "tab", "hand", "tag", "fan"]) {
                const bar = openedWithStyle(style);
                wait(500);
                const names = Util.findAll(bar, function (o) {
                    // Only Items carry the attached property; asking a ShapePath or a Timer for
                    // it prints a warning rather than answering.
                    return o.hasOwnProperty("activeFocusOnTab")
                           && o.Accessible !== undefined && o.Accessible.name !== "";
                }).map(function (o) { return o.Accessible.name; });
                for (const label of ["Translate", "Explain", "Copy text"]) {
                    verify(names.indexOf(label) >= 0, style + " has no accessible '" + label + "' action");
                }
                bar.visible = false;
            }
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// The tag's press is the swing of the whole card, not a per-item shift and fade; the
        /// state that names the pressed action clears with the swing instead of waiting for the
        /// next show (UI-SPEC 4.10).
        function test_theTagAnswersAPressWithItsSwingAlone() {
            const bar = openedWithStyle("tag");
            const body = bar.bodyItem;
            verify(body);
            wait(800);
            const item = itemsOf(bar)[0];

            compare(item.opacity, 1.0, "the tag item should not fade on press");
            compare(item.transform[0].x, 0.0, "the tag item should not shift on press");

            body.playPress("translate");
            compare(body.flashing, "translate", "the pressed action was not recorded");
            tryVerify(function () { return body.flashing === ""; }, 1000,
                      "the tag's press state outlived its swing");
            compare(item.opacity, 1.0, "the tag item faded as well as swinging");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// §4.10's "2 px" is how much higher the middle card sits, not its offset from the
        /// hand's top edge; the two sides are equal and the difference is exactly 2.
        function test_theHandLiftsItsMiddleCardTwoPixels() {
            const bar = openedWithStyle("hand");
            const body = bar.bodyItem;
            verify(body);
            compare(body.cardY[0], body.cardY[2], "the two outer cards should sit at one height");
            compare(body.cardY[0] - body.cardY[1], 2,
                    "the middle card should sit 2 px above its neighbours");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_theTabSpineAndFoldReadAtRest() {
            const bar = openedWithStyle("tab");
            wait(500);
            const tokens = bar.tokens;

            const spine = Util.findAll(bar, function (o) {
                return o.width === 1 && o.height > 40 && o.color !== undefined;
            })[0];
            verify(spine, "the tab has no spine");
            compare(spine.color, tokens.muted, "the spine should read before hover");

            const fold = Util.findAll(bar, function (o) {
                return o.strokeColor !== undefined && o.strokeWidth === 1 && o.strokeColor.a > 0;
            })[0];
            verify(fold, "the tab has no fold");
            compare(fold.strokeColor, tokens.line, "the fold should carry its own edge");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_theFanRibsReadAtRest() {
            const bar = openedWithStyle("fan");
            wait(800);
            const tokens = bar.tokens;
            // The fill paths carry a transparent stroke at the default width; only the drawn
            // outline and the two ribs have ink in the stroke.
            const paths = Util.findAll(bar, function (o) {
                return o.strokeColor !== undefined && o.strokeWidth === 1 && o.strokeColor.a > 0;
            });
            const ribs = paths.filter(function (path) {
                return String(path.strokeColor) === String(tokens.muted);
            });

            compare(paths.length, 3, "the fan should draw an outline and two ribs");
            compare(ribs.length, 2, "the two ribs should read before hover");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_thePressedCardTossesUpAndFades() {
            const bar = openedWithStyle("hand");
            // The hand's label sits a level deeper than the rows do -- inside the card's face --
            // so the tossed card is reached by the property only its slot carries.
            const card = Util.findAll(bar, function (o) { return o.pressFade !== undefined; })[0];
            verify(card, "the hand dealt no card");
            // The dealing entrance ends in the card at rest, fully opaque.
            wait(600);

            compare(card.opacity, 1.0);
            bar.bodyItem.playPress("translate");
            tryVerify(function () { return card.opacity < 0.9 && card.transform[0].y < 0; }, 1000,
                      "the pressed card should lift off the hand and fade as it is tossed");
            Controller.setSetting("selectionBarStyle", "bar");
        }
    }
}
