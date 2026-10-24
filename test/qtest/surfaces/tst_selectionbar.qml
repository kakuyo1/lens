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
