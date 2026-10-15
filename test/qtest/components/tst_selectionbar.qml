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

        /// @return The bar, up on the word selection the component was built with.
        function opened() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            bar.visible = true;
            wait(50);
            return bar;
        }

        function test_theBarOffersTheThreeSelectionActions() {
            const bar = opened();

            compare(Util.textsUnder(bar).map((t) => t.text).sort(), ["Copy text", "Explain", "Translate"]);
        }
    }
}
