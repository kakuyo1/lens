import QtQuick
import QtTest
import Lens

/**
 * When the card's clock starts, now that the answer is written out a character at a time.
 *
 * The countdown is the view's own behaviour, so this is where it is checked: a card that counted
 * while its own answer was still arriving would take itself down mid-sentence, and the display time
 * the reader chose is meant to be time spent reading. The measure of the reveal itself is the
 * sibling components case; what this reads is the pair.
 *
 * The two ends are both here: a fresh answer, whose clock waits; and a card raised whole out of the
 * cache, which has nothing arriving and counts at once.
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
        name: "BubbleReveal"
        when: windowShown

        /// A word bubble as the controller hands one over, with an answer long enough that the
        /// reveal outlasts one tick of the clock.
        function opened(extra) {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const payload = {
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                en: "the occurrence of events by chance in a happy way",
                status: "new",
                x: 200,
                y: 240
            };
            for (const key in extra)
                payload[key] = extra[key];
            bubble.show(payload);
            tryCompare(bubble, "visible", true);
            return bubble;
        }

        /// The clock stands still while the answer is being written, and starts when it is done.
        function test_theCountdownWaitsForTheAnswerToBeWrittenOut() {
            const bubble = opened();

            // Under reduced motion the card arrives complete (UI.md's motion table), so the
            // clock starts at once instead of waiting for the reveal. Nothing here to check
            // in that case -- it is the sibling test's contract.
            if (bubble.tokens.motion.reduced) {
                tryVerify(function () { return bubble.remainingMs < bubble.dismissAfterMs; }, 1000);
                return;
            }

            compare(bubble.revealing, true, "a fresh answer was up whole");
            compare(bubble.remainingMs, bubble.dismissAfterMs, "the clock was running already");

            // Several ticks of the countdown, which is a hundred milliseconds apiece.
            wait(700);
            compare(bubble.remainingMs, bubble.dismissAfterMs, "the clock ran while the answer was still being written");

            tryVerify(function () { return !bubble.revealing; }, 5000);
            tryVerify(function () { return bubble.remainingMs < bubble.dismissAfterMs; }, 2000);
        }

        /// A card raised whole out of the cache -- the one the word list puts up for the row under
        /// the pointer -- has nothing arriving, so it is complete at once and counts at once.
        function test_aCardRaisedWholeIsCompleteAtOnce() {
            const bubble = opened({reveal: false});

            compare(bubble.reveal, false);
            compare(bubble.revealing, false, "a card out of the cache wrote itself out");
            tryVerify(function () { return bubble.remainingMs < bubble.dismissAfterMs; }, 1000);
        }
    }
}
