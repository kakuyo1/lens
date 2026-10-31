import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The card the reader's click raises, before the model has written a word of its answer.
 *
 * The click is the ask, and the card answers it in the click's own frame (docs/adr/0146): what is on
 * screen while the request is away is the word the reader pointed at and the lines the answer will be
 * written on. This file reads that state and the moment it ends -- the waiting rows, the clock that
 * stands still under it, and the two things the card withholds until there is something to judge or
 * to hold for. The card's own body is the subject of the sibling files.
 */
Item {
    id: root
    width: 480
    height: 320

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        id: testCase
        name: "BubbleWaiting"
        when: windowShown

        function init() {
            Controller.setSetting("bubbleDismissSeconds", 5);
        }

        /// @return The card as the duty sends it at the moment of the click: the ask's title, and
        ///         nothing where the answer will go.
        function waiting(extra) {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const payload = {
                title: "bank", type: "word", cardKind: "selection", waiting: true, sameCard: false,
                en: "", zh: "", translation: "", senses: [], status: "new",
                countsDown: false, verdictButtons: false, reveal: false, x: 200, y: 240
            };
            for (const key in extra)
                payload[key] = extra[key];
            bubble.show(payload);
            tryCompare(bubble, "visible", true);
            return bubble;
        }

        /// @return The answer's payload for the same card, as the duty hands it over once the model has
        ///         written the whole of it.
        function answered() {
            return {
                title: "bank", type: "word", cardKind: "selection", waiting: false, sameCard: true,
                en: "an institution for keeping money", zh: "", translation: "", senses: [], status: "new",
                countsDown: true, verdictButtons: true, reveal: false, x: 200, y: 240
            };
        }

        function rowsOf(bubble) {
            return Util.findAll(bubble, function (o) {
                return o.objectName === "waitingRows";
            })[0];
        }

        /// @return The window's own root item, which is the whole surface a reader sees.
        function surfaceOf(bubble) {
            return bubble.contentItem;
        }

        /// The meta line is drawn with its text set whatever the card counts down from, so the reading
        /// is its visibility and not its existence (BubbleCard.qml).
        function metaVisible(bubble) {
            const meta = Util.findAll(bubble, function (o) {
                return typeof o.text === "string" && o.text.indexOf("Disappears in") === 0;
            })[0];
            return meta !== undefined && meta.visible;
        }

        /// The verdict row stands on the card at zero height until it opens, so a card that offers no
        /// verdict is one whose row stays shut under the pointer.
        function verdictHeight(bubble) {
            const row = Util.ofType(bubble, "BubbleVerdictRow");
            verify(row, "the card has no verdict row");
            return row.height;
        }

        /// The click puts a card up with the word on it and the answer's lines unwritten, and says what
        /// it is waiting for (UI-SPEC.md 4.3).
        function test_theClickPutsTheCardUpWithItsLinesUnwritten() {
            const bubble = waiting();
            compare(bubble.waiting, true);
            const rows = rowsOf(bubble);
            verify(rows, "the waiting card draws no rows");
            compare(rows.visible, true);
            compare(Util.findAll(bubble, function (o) {
                return o.objectName === "waitingBar";
            }).length, 3, "the waiting card did not reserve its lines");
            verify(Util.textWith(Util.textsUnder(bubble), ["Explaining…"]), "the card does not say it is waiting");
            compare(metaVisible(bubble), false, "a card nothing has answered is already counting down");
            bubble.visible = false;
        }

        /// A word nothing has explained yet is not a word the reader marks, and there is no standing
        /// sense for the globe to hold for.
        function test_theWaitingCardWithholdsVerdictAndGlobe() {
            const previous = Controller.settings.explanationLang;
            Controller.setSetting("explanationLang", "en");
            const bubble = waiting();
            bubble.hovering = true;
            wait(300);
            compare(verdictHeight(bubble), 0, "the waiting card opened its verdicts to the pointer");
            const globe = Util.findAll(bubble, function (o) {
                return o.Accessible !== undefined && o.Accessible.name === "Mother tongue translation";
            })[0];
            verify(!globe || !globe.visible, "the waiting card offers the peek globe");
            bubble.visible = false;
            Controller.setSetting("explanationLang", previous);
        }

        /// The wait is not the reader's reading time: no clock runs under the waiting card, so a slow
        /// provider cannot take the card down before it has anything to show.
        function test_theWaitingCardKeepsItsClockStill() {
            const bubble = waiting();
            const held = bubble.remainingMs;
            wait(400);
            compare(bubble.remainingMs, held, "the clock ran while nothing had been answered");
            bubble.visible = false;
        }

        /// The answer arrives into the card the click raised: the rows go, the countdown comes, and the
        /// verdict the reader can now earn opens to the pointer.
        function test_theAnswerTakesTheWaitingRowsPlace() {
            const bubble = waiting();
            const rows = rowsOf(bubble);
            bubble.show(answered());
            wait(50);
            compare(bubble.waiting, false);
            compare(rows.visible, false, "the answer left its lines standing");
            tryVerify(function () { return metaVisible(bubble); }, 2000, "the clock did not come back with the answer");
            bubble.hovering = true;
            tryVerify(function () { return verdictHeight(bubble) > 0; }, 2000,
                      "the answered card still keeps its verdicts shut");
            bubble.visible = false;
        }

        /// The waiting card's own look, in both solid themes: the bars are the answer's lines in the
        /// answer's place, and the card is short by the blocks nothing has written yet. What the cases
        /// above cannot say is whether the bars read as a card that is waiting rather than one that
        /// broke -- and `line2` is a hairline, so its step against the card's own fill is the thing to
        /// look at, not a value a case can compare.
        function test_captureTheWaitingCard() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            // The card fades and lifts into place (SurfaceFade.qml): a grab taken before that has
            // finished photographs a half-transparent card.
            const settled = Tokens.motion.pop + 80;

            Tokens.theme = "light";
            const light = waiting();
            wait(settled);
            verify(Util.saveSnapshot(testCase, surfaceOf(light), lensQaSnapshotDir, "bubble-waiting-light"));
            light.visible = false;

            Tokens.theme = "dark";
            const dark = waiting();
            wait(settled);
            verify(Util.saveSnapshot(testCase, surfaceOf(dark), lensQaSnapshotDir, "bubble-waiting-dark"));
            dark.visible = false;
        }
    }
}
