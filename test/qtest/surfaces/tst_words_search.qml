import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The words popup's search row (UI-SPEC.md 4.8, docs/adr/0176): a query narrows the rows, a miss
 * shows the hint in their place, a request leaves only through the miss, and with a word in the
 * focused field Escape stands down so the panel's close cannot answer the same key.
 *
 * TestCase's keyClick reaches the application's shortcuts but nothing inside another window's
 * focus, so the field's Enter and Escape are asserted through the states they read and the calls
 * they make; the pill's press goes through a real mouse click, and the panel-close Escape through
 * the shortcut itself. The field's own keys are driven on the dev surface (docs/qa.md).
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
        name: "WordsSearch"
        when: windowShown

        function make() {
            Controller.wordsModel.filter = 0;
            Controller.wordsModel.query = "";
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
            const panel = createTemporaryObject(wordsComponent, root);
            verify(panel);
            wait(60);
            return panel;
        }

        /// The search field, found by name and focused: the states the row's keys read live on it.
        /// The window is activated first -- focus inside a window nothing activated does not stick.
        function fieldOf(popup) {
            const field = Util.findAll(popup, function (o) { return o.objectName === "searchField"; })[0];
            verify(field !== undefined, "the search field is missing");
            popup.requestActivate();
            field.forceActiveFocus();
            tryVerify(function () { return field.activeFocus; }, 1000, "the search field never took focus");
            return field;
        }

        function viewOf(popup) {
            return Util.findAll(popup, function (o) { return o.toString().indexOf("ListView") !== -1; })[0];
        }

        function pillOf(popup) {
            return Util.findAll(popup, function (o) { return o.objectName === "searchMissPill"; })[0];
        }

        function escapeOf(popup) {
            return Util.findAll(popup, function (o) { return o.objectName === "wordsEscape"; })[0];
        }

        /// A verdict the model makes a row from: only mastered and known words draw rows without
        /// a history entry, so the seeds mark mastered and leave as new.
        function seed(words) {
            for (const word of words)
                Controller.explanationDuty.mark(word, "mastered");
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
        }

        /// Marks back to new, which clears the verdict and takes the row out.
        function unseed(words) {
            for (const word of words)
                Controller.explanationDuty.mark(word, "new");
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
        }

        function test_theQueryNarrowsTheList() {
            const popup = make();
            popup.visible = true;
            seed(["searchfoo", "searchbar", "unrelated"]);
            const view = viewOf(popup);

            const field = fieldOf(popup);
            field.text = "search";
            tryVerify(function () { return view.count === 2; }, 2000,
                      "the query did not narrow to the two seeded words");

            field.text = "searchfoo";
            tryVerify(function () { return view.count === 1; }, 2000);

            // The match is case-insensitive: the rows are lemmas, the typing is the reader's.
            field.text = "SEARCHBAR";
            tryVerify(function () { return view.count === 1; }, 2000);

            popup.visible = false;
            unseed(["searchfoo", "searchbar", "unrelated"]);
        }

        function test_aMissShowsTheHintInPlaceOfTheRows() {
            const popup = make();
            popup.visible = true;
            seed(["searchfoo"]);
            const emptyLine = Util.findAll(popup, function (o) { return o.objectName === "filteredEmptyText"; })[0];

            const field = fieldOf(popup);
            field.text = "zzqq";
            tryVerify(function () { return popup.searchMiss; }, 2000, "the miss never settled");
            const hint = Util.findAll(popup, function (o) { return o.objectName === "searchMissHint"; })[0];
            verify(hint !== undefined && hint.visible, "the miss hint is not shown");
            verify(!emptyLine.visible, "the filter's empty line must yield to the miss hint");
            verify(pillOf(popup) !== undefined, "the hint carries no pill");

            field.text = "";
            tryVerify(function () { return !popup.searchMiss; }, 2000);
            verify(!hint.visible, "the hint outlived its miss");

            popup.visible = false;
            unseed(["searchfoo"]);
        }

        function test_enterOnAMissAsksAboutTheWord() {
            const popup = make();
            popup.visible = true;
            const field = fieldOf(popup);
            const view = viewOf(popup);
            field.text = "zqentered";
            tryVerify(function () { return popup.searchMiss; }, 2000, "a query over no rows is not a miss");

            // The row's Enter asks through lookUpTyped(), and a lookup records its word as it is
            // sent -- so the word joins the rows under the same query it was asked from.
            popup.lookUpTyped();
            tryVerify(function () { return !popup.searchMiss; }, 5000,
                      "the looked-up word never joined the list");
            compare(view.count, 1);
            compare(view.itemAtIndex(0).word, "zqentered");

            // The word leaves with the case: history, verdicts and cache together.
            Controller.removeWord("zqentered");
            tryVerify(function () { return view.count === 0; }, 5000);
        }

        function test_enterWithMatchesAsksNothing() {
            const popup = make();
            popup.visible = true;
            seed(["searchfoo", "searchbar"]);
            const field = fieldOf(popup);

            // Enter reads searchMiss: rows under the query are not a miss, so nothing is asked.
            field.text = "search";
            tryVerify(function () { return !popup.searchMiss; }, 2000,
                      "rows under the query must not read as a miss");
            compare(viewOf(popup).count, 2, "the matched rows disappeared");

            popup.visible = false;
            unseed(["searchfoo", "searchbar"]);
        }

        function test_theMissPillAsksAboutTheWord() {
            const popup = make();
            popup.visible = true;
            const field = fieldOf(popup);
            field.text = "zqpillword";
            tryVerify(function () { return popup.searchMiss; }, 2000, "the miss hint never settled");

            const pill = pillOf(popup);
            verify(pill !== undefined);
            mouseClick(pill, pill.width / 2, pill.height / 2);
            tryVerify(function () { return !popup.searchMiss; }, 5000,
                      "the pill's request never landed");
            compare(viewOf(popup).itemAtIndex(0).word, "zqpillword");

            Controller.removeWord("zqpillword");
            tryVerify(function () { return viewOf(popup).count === 0; }, 5000);
        }

        /// The user's bug: a word outside the active segment read as a miss, and the miss hint
        /// invited a paid re-explain of a word the list already holds. The query suspends the
        /// segment instead -- the search spans the whole vocabulary, the segment greys out while
        /// it stands, and the reader's slice comes back when the query clears.
        function test_theQuerySuspendsTheSegment() {
            const popup = make();
            popup.visible = true;
            seed(["searchfoo"]);
            Controller.explanationDuty.mark("knownword", "known");
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
            Controller.wordsModel.filter = 1;
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
            const view = viewOf(popup);
            const seg = Util.findAll(popup, function (o) { return o.toString().indexOf("Segment") !== -1; })[0];
            verify(seg !== undefined, "the segment control is missing");

            // On the Known segment, the mastered seed is outside the slice -- and the query
            // still finds it.
            const field = fieldOf(popup);
            field.text = "searchfoo";
            tryVerify(function () { return view.count === 1; }, 2000,
                      "the query did not find a word outside the active segment");
            verify(!popup.searchMiss, "a word the list holds must not read as a miss");
            verify(!seg.enabled, "the segment must stand down while the query stands");
            compare(seg.opacity, 0.45);

            // Clearing the query brings the reader's slice back, segment control with it. The
            // slice's size is the fixture's, not a pin: the seed must be out of it, the slice's
            // own word in it.
            field.text = "";
            tryVerify(function () {
                const words = [];
                for (let i = 0; i < view.count; ++i) {
                    const it = view.itemAtIndex(i);
                    if (it) words.push(it.word);
                }
                return words.indexOf("searchfoo") === -1 && words.indexOf("knownword") !== -1;
            }, 2000, "the slice did not come back");
            verify(seg.enabled);
            compare(seg.opacity, 1);

            popup.visible = false;
            Controller.explanationDuty.mark("knownword", "new");
            unseed(["searchfoo"]);
        }

        function test_escapeStandsDownUntilTheFieldIsEmpty() {
            const popup = make();
            popup.visible = true;
            const field = fieldOf(popup);
            const esc = escapeOf(popup);
            verify(esc !== undefined && esc.enabled, "the panel's Escape is disabled with nothing to hold it");

            field.text = "search";
            verify(popup.searchHoldsEscape, "a word in the focused field must hold Escape");
            tryVerify(function () { return !esc.enabled; }, 1000,
                      "the panel's close must stand down while the field holds Escape");

            // The field's own Escape clears the query (driven on the dev surface); the state it
            // leaves is one the panel's close reads again.
            field.text = "";
            verify(!popup.searchHoldsEscape);
            tryVerify(function () { return esc.enabled; }, 1000);

            keyClick(Qt.Key_Escape);
            tryCompare(popup, "visible", false, 1000, "Escape left the words panel up");

            // A fresh panel opens with the query it closed with: nothing.
            popup.visible = true;
            compare(field.text, "", "the panel kept the query across a close");
        }
    }
}
