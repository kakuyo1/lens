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
    }
}
