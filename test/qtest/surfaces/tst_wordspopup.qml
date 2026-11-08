import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The words popup's list geometry (UI-SPEC.md 4.8), in a file of its own because tst_panels.qml
 * sits at the .qml size cap (docs/QML.md section 1) and this surface has more geometry cases to
 * come.
 */
Item {
    id: root
    width: 400
    height: 700

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    TestCase {
        name: "WordsPopup"
        when: windowShown

        function make() {
            // The panel opens on the whole list and is born on whatever the model holds, so the
            // filter is put back before the panel exists and the switch gets the time to land.
            Controller.wordsModel.filter = 0;
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
            const panel = createTemporaryObject(wordsComponent, root);
            verify(panel);
            wait(60);
            return panel;
        }

        /// The list's viewport caps at a whole number of rows: a cap between rows slices the row
        /// that straddles it -- the verdict rows a mark appends at the list's end. Seeding marks
        /// itself: the fixture's row count is not a pin.
        function test_theViewportCapsAtWholeRowsSoTheLastRowIsNotSliced() {
            const popup = make();
            popup.visible = true;

            const list = Util.findAll(popup, function (o) { return o.toString().indexOf("ListView") !== -1; })[0];
            const box = list.parent;
            const rowHeight = popup.rowHeight;
            let seeded = 0;
            while (list.count * rowHeight <= box.maxViewport && seeded < 8) {
                Controller.explanationDuty.mark("clipseed" + seeded, "mastered");
                ++seeded;
            }
            verify(seeded > 0, "the case needs a list that reaches the viewport cap");
            tryVerify(function () { return list.contentHeight > list.height; }, 2000,
                      "seeding never reached the viewport cap");
            tryVerify(function () {
                const it = list.itemAtIndex(list.count - 1);
                return it !== null && Math.abs(it.y - (list.count - 1) * rowHeight) < 0.5;
            }, 2000, "the appended rows never settled into place");

            // Nothing straddles the viewport edge: every row sits wholly above the fold or
            // wholly below it, and the cap itself is a row multiple.
            compare(box.height % rowHeight, 0, "the viewport cap is not a whole number of rows");
            for (let i = 0; i < list.count; ++i) {
                const it = list.itemAtIndex(i);
                if (!it) continue;
                const top = it.mapToItem(list, 0, 0).y;
                const bottom = it.mapToItem(list, 0, it.height).y;
                verify(top >= list.height - 0.5 || bottom <= list.height + 0.5,
                       "row " + i + " straddles the viewport edge");
            }

            // The seeded marks leave with the case: the files after this one read the same
            // singletons, and a settled model is part of leaving.
            for (let i = 0; i < seeded; ++i)
                Controller.explanationDuty.mark("clipseed" + i, "new");
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000,
                      "the model never settled after the case's marks were cleared");
        }
    }
}
