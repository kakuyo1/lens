import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The words panel's filter switch: the two phases the model runs, and what the view does while it
 * runs them.
 *
 * Split out of tst_panels.qml, which is at the QML size cap (QML.md section 1), and kept together
 * because both cases are about the same move: the rows that leave and the rows that arrive. They
 * read the list through the text it draws rather than through the model, because what they cover is
 * a row drawn where it should not be and a card sized where it should not be.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    TestCase {
        id: testCase
        name: "PanelsSwitch"
        when: windowShown

        /// The three helpers below are tst_panels' own, repeated because a case file is a process of
        /// its own and the words panel is the only surface here that has to be settled before it is
        /// built on (see make()).
        function make() {
            // The panel opens on the whole list, and it is born on whatever the model holds at that
            // moment: a case that followed one which left a filter behind would be born on a
            // partial list, and a list that grows while the window is hidden keeps its holes until
            // the first frame.
            Controller.wordsModel.filter = 0;
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; },
                      5000, "the words model never came back to the whole list");

            const panel = createTemporaryObject(wordsComponent, root);
            verify(panel);
            wait(50);
            return panel;
        }

        /// @return The words the model holds under @p verdict, in the order it holds them.
        function verdictWords(verdict) {
            return Controller.wordsModel.allRows()
                .filter(function (entry) { return entry.verdict === verdict; })
                .map(function (entry) { return entry.word; });
        }

        /// @return The word each row is showing, for the rows the filter lets through.
        function wordsShown(popup) {
            const words = Controller.wordsModel.allRows().map(function (entry) { return entry.word; });
            return Util.findAll(popup, function (o) {
                return typeof o.text === "string" && o.visible && words.indexOf(o.text) >= 0;
            }).map(function (o) { return o.text; });
        }

        /// @return Whether the list is showing exactly @p expected, once the fold has settled.
        ///
        /// A switch lands its arrivals one fold after the leaving rows (docs/adr/0122), and the fold
        /// is drawn at the renderer's pace rather than the token's, so the wait is QTest's own
        /// default and the message names what the list actually drew.
        function settleOn(popup, expected) {
            const wanted = expected.slice().sort().join();
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; },
                      5000, "the filter switch never landed");
            tryVerify(function () { return wordsShown(popup).sort().join() === wanted; },
                      5000, "the list is not showing " + wanted
                      + "; it is showing " + wordsShown(popup).sort().join());
        }

        /// @return The words list itself, the scroller a wheel or a flick reaches.
        function listIn(popup) {
            return Util.findAll(popup, function (o) {
                return o.contentY !== undefined && o.count !== undefined
                       && o.contentHeight !== undefined;
            })[0];
        }

        /// @return The words whose row the viewport is actually drawing: a row left at an offset
        ///         the viewport no longer covers has no text inside it.
        function wordsInViewport(popup, list) {
            const words = Controller.wordsModel.allRows().map(function (entry) { return entry.word; });
            return Util.findAll(popup, function (o) {
                return typeof o.text === "string" && o.visible && words.indexOf(o.text) >= 0;
            }).filter(function (o) {
                const y = o.mapToItem(list, 0, 0).y;
                return y >= 0 && y < list.height;
            }).map(function (o) { return o.text; });
        }

        /// A filter switch moves in two phases: the rows it hides close, and the rows it wants
        /// arrive one fold later. Two words on one line is what the reader would call a ghost, so
        /// this asks, for as long as the model says it is between its phases, whether an arriving
        /// word is drawn while a leaving one still is.
        ///
        /// The window is the model's own `filterSwitching`, not a count of samples: a fold is drawn
        /// at the renderer's pace, so a fixed number of 25 ms samples covers a different part of the
        /// move on every machine. While the model holds its arrivals there is nothing to draw them
        /// from, and that is the whole promise of the two phases.
        function test_aFilterSwitchNeverDrawsTwoWordsOnOneLine() {
            const popup = make();
            popup.visible = true;
            wait(300);
            const known = verdictWords("known");
            const fresh = verdictWords("new");
            verify(known.length > 0 && fresh.length > 0, "the fixture needs both verdicts");

            Controller.wordsModel.filter = 1;
            settleOn(popup, known);

            Controller.wordsModel.filter = 2;
            let sample = 0;
            while (Controller.wordsModel.filterSwitching && sample < 400) {
                const shown = wordsShown(popup);
                const arriving = shown.filter(function (w) { return fresh.indexOf(w) >= 0; });
                verify(arriving.length === 0,
                       "sample " + sample + " of the switch drew " + JSON.stringify(arriving)
                       + " before the leaving rows had gone");
                wait(10);
                ++sample;
            }
            verify(sample < 400, "the switch never landed");
            verify(sample > 0, "the switch never held its arrivals: it landed in one phase");
            settleOn(popup, fresh);
            Controller.wordsModel.filter = 0;
        }

        /// A switch between two verdicts takes the list through no rows at all -- the two sets have
        /// nothing in common -- and the card must not follow it there. The viewport holds the height
        /// it had until the switch lands and then takes the count once; a hold one frame shorter than
        /// the model's swap let the card drop to the intersection's height and grow back, which the
        /// reader saw as the card blinking rather than as the rows moving.
        function test_theCardDoesNotShrinkPastItsNewHeightDuringASwitch() {
            const popup = make();
            popup.visible = true;

            Controller.wordsModel.filter = 1;
            settleOn(popup, verdictWords("known"));

            // Every height the card takes while the switch runs, in order. One below the height it
            // settles at is the blink: the card had found its size and lost it again on the way.
            let heights = [];
            const record = function () { heights.push(popup.height); };
            popup.heightChanged.connect(record);
            Controller.wordsModel.filter = 2;
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; },
                      5000, "the switch never landed");
            popup.heightChanged.disconnect(record);

            settleOn(popup, verdictWords("new"));
            const settled = popup.height;
            for (let i = 0; i < heights.length; ++i)
                verify(heights[i] >= settled,
                       "the card was " + heights[i] + " px on its way to " + settled
                       + ": the viewport took the intersection's count between the phases");
            Controller.wordsModel.filter = 0;
        }

        /// A switch from a scrolled list reads the new list from its first row. The rows the new
        /// filter keeps are the delegates the old list already held, and a view left where it was
        /// leaves them at the offsets they had -- while the model's shorter content then clamps the
        /// viewport above every one of them. The panel draws blank, and a wheel finds a word or two
        /// before the clamp takes them away again (2026-10-29, the reader's report).
        function test_aSwitchFromAScrolledListStartsAtTheTop() {
            const seeded = [];
            for (let i = 0; i < 40; ++i)
                seeded.push("zzswitchprobe" + i);
            const at = lensQaFiles.tempDir() + "/switch-" + Date.now() + ".txt";
            verify(lensQaFiles.write(at, seeded.join("\n") + "\n"), "the seed file was not written");
            verify(Controller.importWords(at), "the seed words were not imported");

            try {
                const popup = make();
                popup.visible = true;
                wait(300);
                const list = listIn(popup);
                verify(list, "the words list was not found");
                verify(list.contentHeight > list.height + 100,
                       "the list does not scroll: content " + list.contentHeight + " in " + list.height);

                list.contentY = list.contentHeight - list.height;
                wait(200);

                Controller.wordsModel.filter = 1;
                const known = verdictWords("known");
                settleOn(popup, known);
                wait(500); // the fold that lands the rows, at a loaded runner's pace

                compare(wordsInViewport(popup, list).sort().join(), known.slice().sort().join(),
                        "the viewport is not drawing the known words");
                verify(list.itemAtIndex(0) && Math.round(list.itemAtIndex(0).y) === 0,
                       "the first known row is not at the top of the list");
                verify(list.contentY <= Math.max(0, list.contentHeight - list.height) + 1,
                       "the viewport stands past the last word: contentY " + list.contentY
                       + " against content " + list.contentHeight);
            } finally {
                Controller.wordsModel.filter = 0;
                for (const word of seeded)
                    Controller.removeWord(word);
            }
        }
    }
}
