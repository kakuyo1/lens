import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Pointing at a word in the list, which is how a word the reader explained once is read again
 * without spending anything.
 *
 * What is checked is the contract between the row and the controller, not the surfaces that draw
 * it: the row is reached through the words panel and the answer is read off
 * `Controller.explanationDuty.bubble`, `.notice` and `.wordHint`, the way the panels
 * are read in tst_panels. Main.qml is what turns either of those into a window, and it is not built here.
 *
 * Two rows are enough for both halves, and the fixture has them: `panel` has one stored explanation
 * in the language the fixture explains in (`test/qtest/surfaces/settings.json`), and every other
 * word in its history was explained in a run that stored nothing. `panel` is not interchangeable:
 * the suite shares one controller and its document, and `tst_panels.qml` really deletes the words
 * it is given -- a deletion drops that lemma's stored explanations with it (Controller.removeWord)
 * -- so the word has to be one that file never names, and any replacement too.
 *
 * The dwell before an answer is 400 ms, so a case that wants one waits for it; a case that wants
 * the pointer to have moved on has to move it somewhere else, because a row that is never left
 * never reports leaving.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    Component {
        id: hintComponent
        WordHint {}
    }

    TestCase {
        id: testCase
        name: "Review"
        when: windowShown

        /// The stored word, and one that has nothing stored.
        readonly property string stored: "panel"
        readonly property string unstored: "counterrevolutionary"

        /// Pin what the store is pointed at before any of this runs. The suite shares one
        /// controller, and the settings cases set the explanation language to whatever they are
        /// exercising -- a stored explanation is keyed by the language it was stored in, so a case
        /// that reads one has to say which language it is reading. Multi-sense changes the key too.
        function initTestCase() {
            Controller.setSetting("explanationLang", "zh");
            Controller.setSetting("multiSense", false);
        }

        function make() {
            const popup = createTemporaryObject(wordsComponent, root);
            verify(popup);
            popup.visible = true;
            wait(50);
            return popup;
        }

        /// @return The row drawing @p word.
        function rowFor(popup, word) {
            const text = Util.textWith(Util.textsUnder(popup), [word]);
            verify(text, "the list is not showing " + word);
            return text.parent;
        }

        /// @brief Point at @p word and let the dwell run out. The pointer goes on the word itself
        ///        rather than on the row: the word is what the row watches for a reading, and a
        ///        row's middle is empty space beside a short word.
        function dwellOn(popup, word) {
            const drawn = Util.textWith(Util.textsUnder(popup), [word]);
            verify(drawn, "the list is not showing " + word);
            mouseMove(drawn, 2, drawn.height / 2);
            wait(500);
        }

        function cleanup() {
            Controller.explanationDuty.dismissBubble();
            Controller.explanationDuty.dismissNotice();
        }

        /// Hovering a row raises what that word is already stored as -- the whole point of the
        /// feature, and the reason no request is made: the copy is the one the reader read before.
        function test_hoveringAStoredWordRaisesItsStoredExplanation() {
            const popup = make();
            dwellOn(popup, stored);

            const bubble = Controller.explanationDuty.bubble;
            compare(bubble.title, stored);
            compare(bubble.type, "word");
            compare(bubble.ipa, "/ˈpænl/");
            compare(bubble.senses.length, 1, "the stored entry has one sense");
            compare(bubble.senses[0].text, "面板；专门小组");
            compare(Object.keys(Controller.explanationDuty.notice).length, 0, "a stored word leaves no notice behind");
            // Nothing is arriving for a card out of the cache, and the pointer crosses rows: the
            // card is whole on arrival rather than written out under the pointer.
            compare(bubble.reveal, false, "the reading under the pointer was asked for as a reveal");
        }

        /// A bubble raised by a pointer on a row belongs to that row: the pointer leaving is what
        /// takes it down, so the bubble carries no countdown of its own to disagree with.
        function test_theReadingGoesWhenThePointerLeavesTheRow() {
            const popup = make();
            dwellOn(popup, stored);
            compare(Controller.explanationDuty.bubble.title, stored);

            mouseMove(popup, 2, 2);
            tryVerify(function () { return Object.keys(Controller.explanationDuty.bubble).length === 0; }, 1000,
                      "the bubble outlived the pointer leaving its row");
        }

        /// Leaving the word for the card above it is a reading, not an exit: the dismissal the row
        /// asks for waits out a grace long enough for the pointer to arrive on the card, and the
        /// card reports the arrival itself. What is checked here is the duty's half of that --
        /// the card window is Main.qml's, which this harness does not build.
        function test_theReadingSurvivesThePointerMovingOntoTheCard() {
            const popup = make();
            dwellOn(popup, stored);
            compare(Controller.explanationDuty.bubble.title, stored);

            // The pointer's arrival on the card, as BubbleCard reports it, before the move.
            Controller.explanationDuty.holdReviewCard(true);
            mouseMove(popup, 2, 2);
            wait(600); // past the row's grace, into the waiting dismissal
            compare(Controller.explanationDuty.bubble.title, stored,
                    "the reading went while the pointer was on the card");

            // The pointer leaving the card lands the dismissal that waited for it.
            Controller.explanationDuty.holdReviewCard(false);
            tryVerify(function () { return Object.keys(Controller.explanationDuty.bubble).length === 0; }, 1000,
                      "the dismissal outlived the pointer leaving the card");
        }

        /// A pointer that leaves the word for nowhere still takes the reading with it: the grace
        /// is the moment it takes to reach the card, no more, and nothing reports an arrival.
        function test_leavingTheWordForNowhereStillTakesTheReading() {
            const popup = make();
            dwellOn(popup, stored);
            compare(Controller.explanationDuty.bubble.title, stored);

            mouseMove(popup, 2, 2);
            wait(120); // inside the grace
            compare(Controller.explanationDuty.bubble.title, stored,
                    "the grace was shorter than the move onto the card");
            tryVerify(function () { return Object.keys(Controller.explanationDuty.bubble).length === 0; }, 1000,
                      "the reading outlived the pointer leaving for nowhere");
        }

        /// @return The words list itself, the scroller a wheel or a flick reaches.
        function listIn(popup) {
            return Util.findAll(popup, function (o) {
                return o.contentY !== undefined && o.count !== undefined
                       && o.contentHeight !== undefined;
            })[0];
        }

        /// @brief Import @p words into the store through the reader's own import, so the list has
        ///        rows enough to scroll. The words are removed again by the case that seeded them.
        /// @return The seeded words, or an empty list when the import did not land.
        function seed(words) {
            const at = lensQaFiles.tempDir() + "/seed-" + Date.now() + ".txt";
            if (!lensQaFiles.write(at, words.join("\n") + "\n"))
                return [];
            return Controller.importWords(at) ? words : [];
        }

        /// Scrolling the list stays inside the words: the content it reports is the rows it holds
        /// and the bounds stop past the last row, at the bottom itself and back. The rows are
        /// seeded past the viewport's 306 px first -- a list shorter than that cannot scroll at
        /// all, and a case that never scrolls verifies nothing. The position is pinned rather
        /// than wheeled: this platform's synthetic wheel does not reach the Flickable, and a
        /// gesture the case cannot make is not one it can assert (the real gesture is driven on
        /// the dev surface, where it lands).
        function test_scrollingTheListStaysInsideTheWords() {
            const words = [];
            for (let i = 0; i < 40; ++i)
                words.push("zzscrollprobe" + i);
            verify(seed(words).length === 40, "the seed words were not imported");
            let popup = null;
            let list = null;
            try {
                popup = make();
                list = listIn(popup);
                verify(list, "the words list was not found");
                verify(list.count >= 40, "the seeded rows did not arrive: " + list.count);
                verify(list.contentHeight > list.height + 100,
                        "the list still does not scroll: content " + list.contentHeight + " in " + list.height);

                // The bottom, the top, and the middle: each a place the reader leaves the list.
                for (const at of [list.contentHeight - list.height, 0, Math.round((list.contentHeight - list.height) / 2)]) {
                    list.contentY = at;
                    wait(150); // the delegates the position asks for arrive
                    compare(list.contentHeight, list.count * popup.rowHeight,
                            "the content left the rows' own measure at contentY " + at);
                    verify(list.contentY <= list.contentHeight - list.height + 1,
                           "contentY " + list.contentY + " past content " + list.contentHeight + " in " + list.height);
                    verify(list.contentY >= -1, "the list was left above its first row");
                }
                // And the viewport holds its own measure wherever the content stands.
                compare(list.height, Math.min(306, list.count * popup.rowHeight),
                        "the viewport left the row count's measure");
            } finally {
                for (const word of words)
                    Controller.removeWord(word);
            }
        }

        /// The viewport cannot outlive the content: scrolled to the bottom and left with fewer
        /// words -- the row's cross, a filter -- the list has to bring the viewport back onto the
        /// words rather than leave it standing past the last one, looking at blank.
        function test_theViewportComesBackWhenTheRowsUnderItGo() {
            const words = [];
            for (let i = 0; i < 40; ++i)
                words.push("zzshrinkprobe" + i);
            verify(seed(words).length === 40, "the seed words were not imported");
            let popup = null;
            let list = null;
            try {
                popup = make();
                list = listIn(popup);
                verify(list, "the words list was not found");
                // The bottom, arrived the way the reader arrives at it.
                list.contentY = list.contentHeight - list.height;
                wait(200);

                // The row's own removal, the way the cross performs it: the model lets it go at
                // once, and the view's remove transition closes it while the bottom margin keeps
                // that fold above the viewport's edge.
                for (let i = 0; i < 30; ++i)
                    popup.removeWordAnimated(words[i]);
                wait(900); // the remove transitions and the viewport hold
                const lowest = list.contentHeight - list.height;
                verify(list.contentY <= Math.max(0, lowest) + 1,
                       "after the removals the viewport stands past the last word: contentY "
                       + list.contentY + " against content " + list.contentHeight + " in " + list.height);
                verify(list.contentY >= -1, "the removals left the list above its first row");

                // And the filter's collapse is the same shrink by another door: Known removes
                // every seeded word, every one of them being new.
                Controller.wordsModel.filter = 1;
                wait(900);
                verify(list.contentY <= Math.max(0, list.contentHeight - list.height) + 1,
                       "after the filter the viewport stands past the last word: contentY "
                       + list.contentY + " against content " + list.contentHeight);
                // The viewport is a fact of the row count, not of the fold's progress: the model
                // inserts the rows back and the height follows its count on the next frame,
                // never the content the remove transitions are still closing -- a viewport that
                // chased that fed the view's own estimate back into itself, and the list rang
                // (measured: contentHeight swinging 279..2666 with the viewport collapsing along,
                // the reader looking at blank).
                Controller.wordsModel.filter = 0;
                tryCompare(list, "height", Math.min(306, list.count * popup.rowHeight), 1000,
                           "the viewport followed the fold instead of the row count");
                wait(900);
                verify(list.contentY <= Math.max(0, list.contentHeight - list.height) + 1,
                       "after the filter came back the viewport stands past the last word: contentY "
                       + list.contentY + " against content " + list.contentHeight);
                compare(list.height, Math.min(306, list.count * popup.rowHeight),
                        "the settled viewport left the row count's measure");
            } finally {
                Controller.wordsModel.filter = 0;
                for (const word of words)
                    Controller.removeWord(word);
            }
        }

        /// A word with nothing stored in this language gets a hint beside the word saying so, rather than
        /// looking like a hover that did not register. Nothing about it asks the model, and it is not a
        /// notice: the middle of the screen is no place for a word the reader only passed over.
        function test_aWordWithNothingStoredGetsAHintBesideIt() {
            const popup = make();
            dwellOn(popup, unstored);

            const hint = Controller.explanationDuty.wordHint;
            compare(hint.lemma, unstored, "the hint has to be about the word the row drew");
            compare(hint.body, qsTranslate("lens::app::AppController", "No explanation is stored for this word in the current language."));
            compare(hint.action, qsTranslate("lens::app::AppController", "Explain now"), "the hint has no button to ask with");
            compare(Object.keys(Controller.explanationDuty.notice).length, 0, "a notice came up in the middle of the screen");
            compare(Object.keys(Controller.explanationDuty.bubble).length, 0, "nothing stored means no bubble");
        }

        /// The hint is the pointer's to see go: it takes no input, so nothing on it holds the pointer, and
        /// leaving the word takes it down once the row's grace runs out.
        function test_theHintGoesWhenThePointerLeavesTheWord() {
            const popup = make();
            dwellOn(popup, unstored);
            compare(Controller.explanationDuty.wordHint.lemma, unstored);

            mouseMove(popup, 2, 2);
            tryVerify(function () { return Object.keys(Controller.explanationDuty.wordHint).length === 0; }, 1000,
                      "the hint outlived the pointer leaving the word");
        }

        /// Asking for a stored word by name, at the controller: the answer comes from the store, the same
        /// as a hover's does. The other half of this path is a request, and nothing in this tree asks the
        /// model anything.
        function test_askingAboutAStoredWordAnswersFromTheStore() {
            Controller.explainLemma(stored);

            compare(Controller.explanationDuty.bubble.title, stored);
            compare(Object.keys(Controller.explanationDuty.notice).length, 0, "an answered word leaves no notice behind");
        }

        /// Hovering a stored word while a hint is up takes the hint down: the newer reading is the one asked.
        function test_storedReadingClearsTheHint() {
            const popup = make();
            dwellOn(popup, unstored);
            compare(Controller.explanationDuty.wordHint.lemma, unstored);

            dwellOn(popup, stored);
            compare(Controller.explanationDuty.bubble.title, stored);
            compare(Object.keys(Controller.explanationDuty.wordHint).length, 0, "the hint stayed up behind the reading");
        }

        /// The hint shows its line and its button, and takes the pointer, since the button has to be reachable from the word.
        function test_theHintCarriesItsButtonAndTakesThePointer() {
            const hint = createTemporaryObject(hintComponent, root);
            verify(hint, "the hint window did not build");
            const line = qsTranslate("lens::app::AppController", "No explanation is stored for this word in the current language.");
            const action = qsTranslate("lens::app::AppController", "Explain now");
            hint.show({lemma: unstored, body: line, action: action, x: 40, y: 80, anchorWidth: 60, anchorHeight: 20});
            compare(hint.visible, true);
            wait(50);
            verify(Util.textWith(Util.textsUnder(hint), [line]), "the hint does not show its line");

            const button = Util.findAll(hint, function (o) { return o.objectName === "explainNowButton"; })[0];
            verify(button, "the hint has no button");
            compare(button.visible, true, "the button is not drawn");
            verify(Util.textWith(Util.textsUnder(button), [action]), "the button does not say what it does");
            verify((hint.flags & Qt.WindowTransparentForInput) === 0, "the hint lets the pointer through to the word");

            let asked = 0;
            hint.actionTriggered.connect(function () { ++asked; });
            mouseClick(button, button.width / 2, button.height / 2);
            compare(asked, 1, "pressing the button did not ask for the explanation");

            hint.dismiss();
            compare(hint.visible, false);
        }

        /// The hint as it renders, for the check the pixels need. It is written only when LENS_QA_SNAPSHOT_DIR is set.
        function test_captureTheWordHint() {
            if (!lensQaSnapshotDir) {
                skip("Set LENS_QA_SNAPSHOT_DIR to save the word hint snapshot");
                return;
            }
            const hint = createTemporaryObject(hintComponent, root);
            verify(hint, "the hint window did not build");
            hint.show({lemma: unstored,
                       body: qsTranslate("lens::app::AppController", "No explanation is stored for this word in the current language."),
                       x: 40, y: 80, anchorWidth: 60, anchorHeight: 20});
            wait(Tokens.motion.pop + 80);
            verify(Util.saveSnapshot(testCase, hint.contentItem, lensQaSnapshotDir, "word-hint"));
        }
    }
}
