import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The bar that comes up when the reader finishes selecting text.
 *
 * Only the half that needs no Controller is here. Pressing one of the items also arms the
 * card's DragHandler, whose grab calls Controller.cursorPos(), and a Controller that was never
 * provided trips an assert in its factory -- so the press-and-hold scale, which is the case
 * worth having, belongs to lens_qtest_surfaces. See TEST.md section 2.
 */
Item {
    id: root
    width: 400
    height: 400

    Component {
        id: barComponent
        SelectionBar { selectionText: "ubiquitous" }
    }

    TestCase {
        name: "SelectionBar"
        when: windowShown

        /// @return The bar, opened at @p anchor.
        function opened(anchor) {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.openAt({ x: anchor.x, y: anchor.y, kind: "word", text: "ubiquitous" });
            wait(50);
            return bar;
        }

        function test_theBarOffersTheThreeSelectionActions() {
            const bar = opened({ x: 300, y: 300 });

            compare(Util.textsUnder(bar).map((t) => t.text).sort(), ["Copy text", "Explain", "Translate"]);
        }

        /// Placement belongs to Main.qml's placeTransient(), which also runs the bubble settings'
        /// side choice and the flip (tst_main covers that arithmetic). openAt() only records the
        /// kind and shows the bar, so it must not move the window on its own.
        function test_openAtLeavesThePlacementToTheRoot() {
            const bar = opened({ x: 300, y: 300 });
            const before = { x: bar.x, y: bar.y };

            bar.openAt({ x: 300, y: 300, kind: "word", text: "ubiquitous" });
            compare(bar.x, before.x, "openAt moved the bar sideways");
            compare(bar.y, before.y, "openAt moved the bar vertically");
        }
    }
}
