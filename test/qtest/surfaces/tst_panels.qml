import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The three panels that are read rather than operated: today's figures, the cost breakdown and
 * the word list.
 *
 * Each is built by one expression per figure, off a QVariantMap the controller builds, so the
 * failure to catch is a key that does not exist -- it draws as "undefined" or as a zero, a quiet
 * figure rather than an error. The rows are reached through StatRow's own label and value rather
 * than by matching rendered strings, so the cases do not become a second copy of the panels.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: statsComponent
        StatsPopup {}
    }

    Component {
        id: costComponent
        CostPopup {}
    }

    Component {
        id: wordsComponent
        WordsPopup {}
    }
    TestCase {
        id: testCase
        name: "Panels"
        when: windowShown
        property int titleRequests: 0
        readonly property var costDuty: Controller.costDuty

        function make(component) {
            const panel = createTemporaryObject(component, root);
            verify(panel);
            wait(50);
            return panel;
        }

        /// @return The panel's rows, in the order the panel declares them.
        function rowsOf(panel) {
            return Util.findAll(panel, function (o) {
                return o.toString().indexOf("StatRow_QMLTYPE") === 0;
            });
        }

        /// @return The rows' main figures, keyed by label.
        function figuresOf(panel) {
            const rows = rowsOf(panel);
            const byLabel = {};
            for (let i = 0; i < rows.length; ++i)
                byLabel[rows[i].label] = rows[i].value;
            return byLabel;
        }

        /// @return The rows' secondary figures, keyed by label; empty where a row carries none.
        function notesOf(panel) {
            const rows = rowsOf(panel);
            const byLabel = {};
            for (let i = 0; i < rows.length; ++i)
                byLabel[rows[i].label] = rows[i].note;
            return byLabel;
        }

        /// @return The one figure drawn at the hero size.
        function heroOf(panel) {
            const heroes = Util.findAll(panel, function (o) {
                return o.font !== undefined && o.font.pixelSize === 32;
            });
            compare(heroes.length, 1, "the panel should draw exactly one hero figure");
            return heroes[0].text;
        }

        function test_theStatisticsPanelShowsTodaysFigures() {
            const stats = make(statsComponent);
            const figures = figuresOf(stats);

            compare(heroOf(stats), String(costDuty.stats.todayPops));
            compare(figures["Known"], String(costDuty.stats.todayLearned));
            compare(figures["New"], String(costDuty.stats.todayFresh));
            compare(figures["Cost"], costDuty.stats.currency + costDuty.stats.todayCost.toFixed(2));
            compare(figures["All time"], qsTr("%1 words").arg(Controller.wordsModel.allRows().length));
        }

        /// The cost panel's other dimension: every row draws the amount, and the three priced
        /// buckets draw their tokens in the secondary slot. The daily average has no bucket.
        function test_theCostPanelShowsTheSameFiguresInTheOtherDimension() {
            const cost = make(costComponent);
            const figures = figuresOf(cost);
            const notes = notesOf(cost);

            compare(heroOf(cost), costDuty.cost.currency + costDuty.cost.month.toFixed(2));
            compare(figures["Today"], costDuty.cost.currency + costDuty.cost.today.toFixed(2));
            compare(figures["Yesterday"], costDuty.cost.currency + costDuty.cost.yesterday.toFixed(2));
            compare(figures["This week"], costDuty.cost.currency + costDuty.cost.week.toFixed(2));
            compare(figures["Daily average"], costDuty.cost.currency + costDuty.cost.dailyAverage.toFixed(2));

            compare(notes["Today"], qsTr("%1 tokens").arg(String(costDuty.cost.todayTokens)));
            compare(notes["Yesterday"], qsTr("%1 tokens").arg(String(costDuty.cost.yesterdayTokens)));
            compare(notes["This week"], qsTr("%1 tokens").arg(String(costDuty.cost.weekTokens)));
            compare(notes["Daily average"], "", "the daily average has no token bucket to draw");
        }

        /// @return The word each row is showing, for the rows the filter lets through.
        function wordsShown(popup) {
            const words = Controller.wordsModel.allRows().map(function (entry) { return entry.word; });
            return Util.findAll(popup, function (o) {
                return typeof o.text === "string" && o.visible && words.indexOf(o.text) >= 0;
            }).map(function (o) { return o.text; });
        }

        function test_theWordsPanelListsEveryDistinctWordTheControllerHolds() {
            const popup = make(wordsComponent);
            const words = Controller.wordsModel.allRows().map(function (entry) { return entry.word; });
            verify(words.length > 1, "the fixture needs more than one word for this to mean anything");

            compare(wordsShown(popup).sort(), words.slice().sort());
        }

        /// The labels are formatted here from the day and raw time the controller hands over;
        /// the fixture is all January, so today and yesterday rows are built by hand.
        function test_theWordsPanelFormatsEachDaysTimeLabel() {
            const popup = make(wordsComponent);
            compare(popup.whenLabel("today", "10-11", "09:15"), "Today 09:15");
            compare(popup.whenLabel("yesterday", "10-10", "23:40"), "Yesterday 23:40");
            compare(popup.whenLabel("earlier", "03-07", "08:05"), "03-07 08:05");
            compare(popup.whenLabel("", "", ""), "");
        }

        /// Known and New are the verdict pills' own labels, so each row draws one of each. The
        /// filter segment repeats both words once more, which is the one extra in each count.
        function test_theWordsPanelLabelsTheVerdictsKnownAndNew() {
            const popup = make(wordsComponent);
            const texts = Util.textsUnder(popup).map(function (o) { return o.text; });
            compare(texts.filter(function (t) { return t === "Known"; }).length, Controller.wordsModel.allRows().length + 1);
            compare(texts.filter(function (t) { return t === "New"; }).length, Controller.wordsModel.allRows().length + 1);
        }

        function test_theWordsTitleRequestsTheYearPanel() {
            const popup = make(wordsComponent);
            titleRequests = 0;
            popup.yearRequested.connect(function () { titleRequests += 1; });
            const title = Util.textWith(Util.textsUnder(popup), [qsTr("Words")]);
            verify(title, "the Words title is not drawn");

            mouseClick(title, title.width / 2, title.height / 2);
            tryCompare(testCase, "titleRequests", 1);
        }

        function test_calendarOpensYear() {
            const popup = make(wordsComponent);
            titleRequests = 0;
            popup.yearRequested.connect(function () { titleRequests += 1; });
            const button = Util.findAll(popup, function (o) {
                return o.objectName === "yearButton";
            })[0];
            verify(button, "the words header has no calendar button");
            compare(button.accessibleName, qsTranslate("WordsPopup", "Year in words"));

            mouseClick(button, button.width / 2, button.height / 2);
            tryCompare(testCase, "titleRequests", 1);
        }
        /// The card is sized to its widest row: a narrower word would be seen cut short.
        function test_theWidestWordIsRenderedWhole() {
            const popup = make(wordsComponent);
            popup.visible = true;

            // The fixture carries a word long enough to outgrow the panel's base width, so a
            // card that stayed at the base would fail the comparison below rather than pass it.
            const widest = Controller.wordsModel.allRows().slice().sort(function (a, b) {
                return b.word.length - a.word.length;
            })[0].word;
            verify(popup.cardWidth > popup.minCardWidth,
                   "the widest word did not widen the card past its base");

            const row = rowFor(popup, widest);
            verify(row, "the row for " + widest + " is not on screen");

            const word = Util.findAll(row, function (o) { return o.text === widest; })[0];
            verify(word, "the row does not draw the word");
            compare(word.implicitWidth <= word.width, true,
                    "the word column is narrower than the word it draws");
            compare(word.elide, Text.ElideRight);
        }

        /// @return Whether the list is showing exactly @p expected, once the fold has settled.
        function settleOn(popup, expected) {
            const wanted = expected.slice().sort().join();
            return tryVerify(function () { return wordsShown(popup).sort().join() === wanted; },
                             1000, "the list is not showing " + wanted);
        }

        /// A delegate nested inside a Repeater inside a ListView, read through the model's own
        /// verdict rather than through the translated label the row draws.
        function test_theVerdictFilterNarrowsTheList() {
            const popup = make(wordsComponent);
            // The rows leave through the view's remove transition, which needs a visible view
            // to run; only the window's rendering owns that, not the model's count.
            popup.visible = true;

            Controller.wordsModel.filter = 1;
            const known = Controller.wordsModel.allRows().filter(function (e) { return e.verdict === "known"; }).map(function (e) { return e.word; });
            settleOn(popup, known);

            Controller.wordsModel.filter = 2;
            const fresh = Controller.wordsModel.allRows().filter(function (e) { return e.verdict === "new"; }).map(function (e) { return e.word; });
            settleOn(popup, fresh);
        }

        /// A filter switch moves in two phases: the rows it hides close, and the rows it wants
        /// arrive one fold later. Two words on one line is what the reader would call a ghost, so
        /// this samples across the whole move and asks, at every frame, whether an arriving word
        /// is drawn while a leaving one still is.
        function test_aFilterSwitchNeverDrawsTwoWordsOnOneLine() {
            const popup = make(wordsComponent);
            popup.visible = true;
            wait(300);
            const all = Controller.wordsModel.allRows();
            const known = all.filter(function (e) { return e.verdict === "known"; }).map(function (e) { return e.word; });
            const fresh = all.filter(function (e) { return e.verdict === "new"; }).map(function (e) { return e.word; });
            verify(known.length > 0 && fresh.length > 0, "the fixture needs both verdicts");

            Controller.wordsModel.filter = 1;
            settleOn(popup, known);
            Controller.wordsModel.filter = 2;
            for (let sample = 0; sample < 14; ++sample) {
                wait(25);
                const shown = wordsShown(popup);
                const leaving = shown.filter(function (w) { return known.indexOf(w) >= 0; });
                const arriving = shown.filter(function (w) { return fresh.indexOf(w) >= 0; });
                verify(leaving.length === 0 || arriving.length === 0,
                       "at " + (sample * 25) + " ms the switch drew " + JSON.stringify(arriving)
                       + " over " + JSON.stringify(leaving));
            }
            settleOn(popup, fresh);
            Controller.wordsModel.filter = 0;
        }

        /// @return The row drawing @p word, or null when no row shows it.
        function rowFor(popup, word) {
            const text = Util.textWith(Util.textsUnder(popup), [word]);
            return text ? text.parent : null;
        }

        /// @return The pill @p row draws its @p label in.
        function pillFor(row, label) {
            const labels = Util.findAll(row, function (o) { return o.text === label; });
            compare(labels.length, 1, "a row should draw exactly one " + label + " pill");
            return labels[0].parent;
        }

        /// Marking from the list settles a word the reader never marked while the bubble was up.
        function test_tappingAVerdictMarksTheWordAndTheRowFollows() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const unmarked = Controller.wordsModel.allRows().filter(function (e) { return e.verdict === ""; });
            verify(unmarked.length > 0, "the fixture needs a word that was never marked");
            const word = unmarked[0].word;

            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            verify(!Qt.colorEqual(pillFor(row, "Known").color, Tokens.ink),
                   "an unmarked word should not already show a filled Known pill");

            const known = pillFor(row, "Known");
            mouseClick(known, known.width / 2, known.height / 2);

            tryVerify(function () {
                const follow = rowFor(popup, word);
                return follow !== null && Qt.colorEqual(pillFor(follow, "Known").color, Tokens.ink);
            }, 1000, "the Known pill did not fill after the tap");

            const marked = Controller.wordsModel.allRows().filter(function (e) { return e.word === word; })[0];
            compare(marked.verdict, "known", "the tap did not reach the controller");
        }

        /// The other half of the same control: a word already settled has to take a new verdict.
        /// A pill that could only fill an unmarked entry left the second press looking dead.
        function test_reMarkingAWordMovesTheRowToTheOtherVerdict() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const settled = Controller.wordsModel.allRows().filter(function (e) { return e.verdict === "known"; });
            verify(settled.length > 0, "the fixture needs a word already marked known");
            const word = settled[0].word;

            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            verify(Qt.colorEqual(pillFor(row, "Known").color, Tokens.ink),
                   "a known word should show a filled Known pill");

            const fresh = pillFor(row, "New");
            mouseClick(fresh, fresh.width / 2, fresh.height / 2);

            tryVerify(function () {
                const follow = rowFor(popup, word);
                return follow !== null
                    && Qt.colorEqual(pillFor(follow, "New").color, Tokens.ink)
                    && !Qt.colorEqual(pillFor(follow, "Known").color, Tokens.ink);
            }, 1000, "the row did not follow the word to its new verdict");

            // The Controller is shared by every case in the run, so put the word back where the
            // case found it rather than leaving the fixture settled the other way.
            Controller.explanationDuty.mark(word, true);
        }

        /// @return The window the question is drawn in. It is a window of its own rather than a
        ///         card inside the panel, so a case reaches it through an item that lands in it.
        function questionWindow(popup) {
            const heading = Util.textWith(Util.textsUnder(popup), [qsTr("Remove word")]);
            verify(heading, "the question is not drawn");
            return heading.Window.window;
        }

        /// The question, for the eye: a word long enough to outgrow the card's floor shows
        /// whether the sentence still fits the card it is asked on.
        function test_captureTheRemovalQuestion() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            // Every surface is a card that fades and lifts into place, so a grab taken before
            // that has finished photographs a half-transparent card.
            const settled = Tokens.motion.pop + 80;
            const was = Tokens.theme;

            const popup = make(wordsComponent);
            popup.visible = true;
            wait(settled);

            Tokens.theme = "light";
            wait(settled);
            verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "words-list-light"));

            popup.requestRemoval("counterrevolutionary");
            wait(settled);
            verify(Util.saveSnapshot(testCase, questionWindow(popup).contentItem,
                                     lensQaSnapshotDir, "words-removal-light"));

            Tokens.theme = "dark";
            wait(settled);
            verify(Util.saveSnapshot(testCase, questionWindow(popup).contentItem,
                                     lensQaSnapshotDir, "words-removal-dark"));

            popup.closeChild();
            popup.visible = false;
            Tokens.theme = was;
        }

        /// The cross sits on the row's own line, beside the two clusters that share it. Left to
        /// the Row it is laid in, it takes the row's first pixel and reads high.
        function test_theRowCentresItsCrossOnItsLine() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const word = "run";
            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");

            const cross = crossFor(row);
            verify(cross, "the row draws no delete glyph");
            const pill = pillFor(row, "Known");

            const crossMid = cross.mapToItem(row, 0, cross.height / 2).y;
            const pillMid = pill.mapToItem(row, 0, pill.height / 2).y;
            verify(Math.abs(crossMid - pillMid) <= 0.5,
                   "the cross is " + (crossMid - pillMid).toFixed(1)
                   + " px off the line its verdict pills are on");
        }

        /// @return The delete glyph at the end of @p row, or null when the row draws none.
        function crossFor(row) {
            return Util.findAll(row, function (o) {
                return typeof o.source === "string" && o.source.indexOf("ui-close") >= 0;
            })[0];
        }

        /// @return The pill carrying @p label on the confirmation face.
        function pillWith(popup, label) {
            const text = Util.textWith(Util.textsUnder(popup), [label]);
            verify(text, "the confirmation draws no " + label + " pill");
            return text.parent;
        }

        /// @brief Press the delete glyph on @p word's row, whichever answer that turns out to be.
        function pressCross(popup, word) {
            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            const cross = crossFor(row);
            verify(cross, "the row draws no delete glyph");
            mouseClick(cross, cross.width / 2, cross.height / 2);
        }

        /// @brief Open the question on @p word through the row's own cross.
        function askAbout(popup, word) {
            pressCross(popup, word);
            compare(popup.pendingRemoval, word, "the cross did not name the word it belongs to");
        }

        /// The confirmation the cross opens, and the answer that carries it out.
        ///
        /// What is tested is the path through the card, not the controller call: the cross names
        /// the word, the question draws it, the answer reaches Controller.removeWord, and the
        /// list follows.
        function test_theDeleteCrossAsksBeforeTheWordGoes() {
            const popup = make(wordsComponent);
            popup.visible = true;

            // A word no other case in this file needs: this one is really removed, and the
            // Controller is shared by every case in the run.
            const word = "ubiquitous";
            verify(Controller.wordsModel.allRows().some(function (e) { return e.word === word; }),
                   "the fixture no longer carries " + word);

            askAbout(popup, word);

            const question = Util.textWith(Util.textsUnder(popup),
                                           [qsTr("Remove %1 from your word list and history?").arg(word)]);
            verify(question, "the card is not asking about " + word);
            verify(question.parent.visible, "the question is on a face that is not drawn");

            mouseClick(pillWith(popup, "Remove"), 5, 5);

            compare(popup.pendingRemoval, "", "the card stayed on the question after its answer");
            // The model removes the row at once; the view's remove transition is what closes it,
            // so the delegate stays drawn for the fold and is gone when the fold lands.
            tryVerify(function () { return !Controller.wordsModel.allRows().some(function (e) { return e.word === word; }); },
                      1000, "the answer did not reach Controller.removeWord");
            tryVerify(function () { return rowFor(popup, word) === null; },
                      1000, "the list still draws a word that is gone");
        }

        /// The question starts with "do not ask again" off, so answering Remove settles nothing
        /// about the next removal: it is asked about again. The old default silenced the run on
        /// its first deletion, which is the wrong way round for an irreversible act.
        function test_theQuestionKeepsAskingByDefault() {
            const popup = make(wordsComponent);
            popup.visible = true;
            verify(popup.askBeforeRemoving, "the panel should start out asking");

            // Words no other case in this file needs: both are really removed.
            const answered = "ostensibly";
            const askedAgain = "juxtapose";

            askAbout(popup, answered);
            const question = questionWindow(popup);
            compare(question.visible, true, "the question did not open");

            const switcher = Util.ofType(popup, "Switch");
            verify(switcher, "the question carries no switch");
            compare(switcher.checked, false, "the question should be put with the switch off");

            mouseClick(pillWith(popup, "Remove"), 5, 5);

            compare(popup.askBeforeRemoving, true, "the answer silenced the asking without being told to");
            tryVerify(function () { return !Controller.wordsModel.allRows().some(function (e) { return e.word === answered; }); },
                      1000, "the answer did not reach Controller.removeWord");
            // Answered at once; the window waits for the fade (ShadowCard.qml).
            tryCompare(question, "visible", false, 1000, "the question stayed up");

            askAbout(popup, askedAgain);
            compare(questionWindow(popup).visible, true, "the next removal skipped its question");
            compare(popup.pendingRemoval, askedAgain, "the question is not about the next word");

            mouseClick(pillWith(popup, "Cancel"), 5, 5);
            tryCompare(question, "visible", false, 1000, "Cancel did not put the question down");
            verify(Controller.wordsModel.allRows().some(function (e) { return e.word === askedAgain; }),
                   "putting the question down removed the word");
        }

        /// The other answer the reader can give there: turn the switch on and the answer settles
        /// the asking, so the next removal is a deletion. The reader's choice persists for the run.
        function test_aQuestionSilencedByTheReaderStaysSilent() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const answered = "ephemeral";
            const straightThrough = "quintessential";

            askAbout(popup, answered);
            const switcher = Util.ofType(popup, "Switch");
            verify(switcher, "the question carries no switch");
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            mouseClick(pillWith(popup, "Remove"), 5, 5);

            compare(popup.askBeforeRemoving, false, "the switch was on, so asking should stand");
            tryVerify(function () { return !Controller.wordsModel.allRows().some(function (e) { return e.word === answered; }); },
                      1000, "the answer did not reach Controller.removeWord");

            // The window stays modal for the fade, so the list takes presses again only once it
            // has gone -- asking while the old question is still fading would press a dead card.
            tryCompare(questionWindow(popup), "visible", false, 1000,
                       "the answered question stayed up");

            pressCross(popup, straightThrough);

            compare(popup.pendingRemoval, "", "a cross asked the question it was told not to ask");
            compare(questionWindow(popup).visible, false, "the question opened with the asking switched off");
            tryVerify(function () { return !Controller.wordsModel.allRows().some(function (e) { return e.word === straightThrough; }); },
                      1000, "the silenced cross did not delete the word");
        }

        /// The list behind the question is not an answer to it, so a press that reaches it must
        /// not mark or delete anything. Measured because the suite's platform does not enforce
        /// modality -- this guard does.
        function test_theListBehindTheQuestionTakesNoPress() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const word = "run";
            const before = Controller.wordsModel.allRows().filter(function (e) { return e.word === word; })[0].verdict;

            askAbout(popup, word);
            const question = questionWindow(popup);

            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            const pill = pillFor(row, "Known");
            mouseClick(pill, pill.width / 2, pill.height / 2);

            compare(question.visible, true, "the press behind the question dismissed it");
            compare(popup.pendingRemoval, word, "the press behind the question changed its subject");
            compare(Controller.wordsModel.allRows().filter(function (e) { return e.word === word; })[0].verdict,
                    before, "the list behind the question took a press");

            popup.closeChild();
        }

        /// The other two ways out, both of which have to leave the word alone: the reader who
        /// opened the question by mistake has to be able to put it down.
        function test_theConfirmationCanBePutDownWithoutRemovingAnything() {
            const popup = make(wordsComponent);
            popup.visible = true;
            const word = "counterrevolutionary";

            askAbout(popup, word);
            mouseClick(pillWith(popup, "Cancel"), 5, 5);

            compare(popup.pendingRemoval, "", "the card stayed on the question");
            verify(Controller.wordsModel.allRows().some(function (e) { return e.word === word; }),
                   "putting the question down removed the word");

            // Keep the modality fixed while visible: Qt updates its modal list only on show and
            // hide, so a change in between strands an entry QApplication treats as app-modal.
            const question = questionWindow(popup);
            compare(question.modality, Qt.WindowModal,
                    "the question dropped its modality while it was still on screen");
            tryCompare(question, "visible", false, 1000, "Cancel did not put the question down");

            askAbout(popup, word);
            // keyClick takes no item; it lands on whatever window has focus, which offscreen is
            // never the panel -- the reason the shortcut is application-wide.
            keyClick(Qt.Key_Escape);

            compare(popup.pendingRemoval, "", "Escape did not put the question down");
            verify(Controller.wordsModel.allRows().some(function (e) { return e.word === word; }),
                   "Escape removed the word");
        }

        /// The export's text half is the controller's, and lens_gtest_unit pins its shape. What
        /// is left for this case is the seam the file dialog hands its answer to: the scope the
        /// row filter maps to, and that an unknown one is refused rather than written.
        function test_theExportFollowsTheFilterAndRefusesAnyOtherScope() {
            const popup = make(wordsComponent);

            compare(popup.scope, "all", "the panel opens on the whole list");
            Controller.wordsModel.filter = 1;
            compare(popup.scope, "known");
            Controller.wordsModel.filter = 2;
            compare(popup.scope, "new");

            // A path under a directory that does not exist, so a write could not land even if
            // the scope check let it through.
            compare(Controller.saveWords("file:///no-such-directory/words.txt", "everything"), false);
        }
    }
}
