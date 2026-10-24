import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The outlined two-way switch, and the delegate wiring it is built out of.
 *
 * The half that matters is the delegate wiring: a Repeater's `modelData` and `index` have to
 * reach a Text nested one level inside the delegate. Written unqualified, they resolve to
 * nothing, the cells come up empty, and qmllint is documented as blind to it (docs/QML.md
 * section 8) -- these cases are the check that stands in for it.
 */
Item {
    id: root
    width: 320
    height: 240

    readonly property var labels: ["whole", "phrase", "rare"]

    Component {
        id: segmentComponent
        Segment { width: 240; height: 32 }
    }

    TokenHost { id: host }

    TestCase {
        name: "Segment"
        when: windowShown

        function make(properties) {
            const segment = createTemporaryObject(segmentComponent, host.contentItem, properties);
            waitForRendering(host.contentItem);
            verify(segment);
            return segment;
        }

        function test_everyCellShowsItsOwnLabel() {
            const segment = make({ labels: root.labels });

            const shown = Util.textsUnder(segment).map((t) => t.text);
            compare(shown.sort(), root.labels.slice().sort(),
                    "a cell rendered nothing, so modelData did not reach the nested Text");
        }

        function test_onlyTheSelectedCellTakesTheInkFill() {
            const segment = make({ labels: root.labels, currentIndex: 1 });

            const inked = Util.textsUnder(segment).filter((t) => t.parent.color === Tokens.ink);
            compare(inked.length, 1, "exactly one cell should carry the ink fill, which is index doing its job");
            compare(inked[0].text, root.labels[1]);
        }

        function test_tappingACellPicksItsIndex() {
            const segment = make({ labels: root.labels });

            let picked = -1;
            segment.picked.connect(function (index) { picked = index; });

            const cell = Util.textWith(Util.textsUnder(segment), ["rare"]).parent;
            mouseClick(cell, cell.width / 2, cell.height / 2);

            compare(picked, 2);
        }

        /// The keyboard half: the arrows move the choice the way a radio group's do, and the
        /// ends hold rather than wrap.
        function test_arrowKeysMoveTheChoiceWithoutWrapping() {
            const segment = make({ labels: root.labels, currentIndex: 0 });
            let picked = -1;
            segment.picked.connect(function (index) { picked = index; });
            host.requestActivate();
            tryVerify(function () { return host.active; });
            segment.forceActiveFocus();
            tryVerify(function () { return segment.activeFocus; });

            keyClick(Qt.Key_Left);
            compare(segment.currentIndex, 0, "the choice wrapped below the first cell");

            keyClick(Qt.Key_Right);
            compare(segment.currentIndex, 1);
            compare(picked, 1, "the arrow moved the fill without reporting it");

            keyClick(Qt.Key_Right);
            keyClick(Qt.Key_Right);
            compare(segment.currentIndex, 2, "the choice wrapped past the last cell");
        }

        /// The screen-reader half: one cell per option, each named and carrying its own
        /// checked state, so the group reads as radios even though it takes one tab stop.
        function test_eachCellIsANamedCheckedRadio() {
            const segment = make({ labels: root.labels, currentIndex: 2 });
            const cells = Util.textsUnder(segment).map((t) => t.parent);

            for (let i = 0; i < cells.length; ++i) {
                compare(cells[i].Accessible.role, Accessible.RadioButton);
                compare(cells[i].Accessible.name, root.labels[i]);
                compare(cells[i].Accessible.checked, i === 2);
            }
        }
    }
}
