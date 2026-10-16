import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The reveal: an answer written out a character at a time, and what an answer that keeps arriving
 * does to a reveal already running.
 *
 * What this can see is the pace -- the count of characters written out, read over time. What it
 * cannot see is the blur sharpening, which is a shader: the offscreen platform never rasterises a
 * MultiEffect (TEST.md section 5), so the tail is checked as the count of characters still carrying
 * one rather than as pixels.
 *
 * The clock here is real time, so every direction below is a generous one: the reveal is asserted
 * to have reached a length by a deadline, never to be at an exact one on an exact frame.
 */
Item {
    id: root
    width: 320
    height: 200

    TokenHost { id: host }

    Component {
        id: revealComponent
        RevealText { width: 260 }
    }

    TestCase {
        id: testCase
        name: "RevealText"
        when: windowShown

        /// A definition of a realistic length: fifty characters is what the model writes back for
        /// a word like this one.
        readonly property string answer: "the occurrence of events by chance in a happy way"

        function opened(properties) {
            const reveal = createTemporaryObject(revealComponent, host.contentItem, properties);
            verify(reveal);
            return reveal;
        }

        /// The length the reveal has written out grows on its own, and reaches the whole text.
        function test_theAnswerIsWrittenOutOneCharacterAtATime() {
            const reveal = opened({text: answer});

            verify(reveal.revealed < answer.length, "the whole answer was there at once");
            compare(reveal.revealing, true);

            const started = Date.now();
            tryVerify(function () { return reveal.revealed > 2; }, 500);
            tryVerify(function () { return reveal.revealed === answer.length; }, answer.length * reveal.characterMs + 1000);

            // Not done at the last character: the field is still moving until that one has cleared,
            // which is the same answer Bubble.qml's countdown reads.
            tryVerify(function () { return !reveal.revealing; }, 1000);

            // A pace, not an immediate full text: fifty characters at the field's own interval
            // cannot land inside a single character's worth of it.
            verify(Date.now() - started > reveal.characterMs * 4, "the answer did not take the time it should have");
        }

        /// A chunk arriving while the field is still being written extends the target; the cursor
        /// carries on from where it stood rather than starting the field over.
        function test_aChunkArrivingMidRevealCarriesOnFromWhereItWas() {
            const reveal = opened({text: "the occurrence of"});
            tryVerify(function () { return reveal.revealed >= 4; }, 500);

            const reached = reveal.revealed;
            reveal.text = answer;

            verify(reveal.revealed >= reached, "the reveal fell back to the start of the field");
            tryVerify(function () { return reveal.revealed > reached; }, 500);
            tryVerify(function () { return reveal.revealed === reveal.text.length; }, answer.length * reveal.characterMs + 1000);
        }

        /// The card the word list raises from the cache, and every card under reduced motion, is
        /// whole on arrival: nothing is arriving, so there is nothing to pace.
        function test_aFieldRaisedWholeIsNotWrittenOut() {
            const reveal = opened({text: answer, animated: false});

            compare(reveal.revealed, answer.length);
            compare(reveal.revealing, false);
        }

        /// A field that is sent a different answer re-reveals it: the card is reused for the next
        /// word, and the next word is not stuck with the amount the last one had reached.
        function test_aDifferentAnswerRevealsFromTheStart() {
            const reveal = opened({text: answer});
            tryVerify(function () { return reveal.revealed >= 4; }, 500);

            reveal.text = "a business that keeps and lends money";

            verify(reveal.revealed < 4, "the new answer inherited the last one's cursor");
            compare(reveal.revealing, true);
        }

        /// The pace is the field's own, not a constant: two fields of the same text, one set slow
        /// and one fast, are at different lengths after the same real time. Nothing here reads an
        /// exact count on an exact frame -- the clock is real, so the gap is what is asserted.
        function test_thePaceFollowsTheFieldRatherThanAConstant() {
            const slow = opened({text: answer, characterMs: 200});
            const fast = opened({text: answer, characterMs: 20});

            wait(300);

            verify(slow.revealed < fast.revealed,
                   "the slow field and the fast one wrote at the same pace");
        }

        /// A slower pace does not widen the blur: the span is the ratio, not the milliseconds, so
        /// the tail stays a handful of characters whatever the reader picks. This is the whole
        /// reason the blur follows the pace as a ratio rather than sitting at a fixed 180 ms.
        function test_aSlowerPaceKeepsTheBlurSpanTheSame() {
            const slow = opened({text: answer, characterMs: 200});
            const fast = opened({text: answer, characterMs: 20});

            compare(slow.softSpan, fast.softSpan, "the blur span followed the pace");
            verify(slow.softSpan <= 6, "the tail carries more shaders than the shipped six");
        }

        /// Only the characters still sharpening carry a blur: the settled ones are plain text, so
        /// what the reveal costs the renderer is the tail rather than the length of the field.
        function test_onlyTheSharpeningTailCarriesABlur() {
            const reveal = opened({text: answer});
            tryVerify(function () { return reveal.revealed >= 2; }, 500);

            const blurred = Util.findAll(reveal, function (o) {
                return String(o).indexOf("MultiEffect") >= 0 && o.parent.visible;
            });
            verify(blurred.length > 0, "no character was sharpening");
            verify(blurred.length <= reveal.softSpan, "more characters carry a blur than the span allows");
        }
    }
}
