import QtQuick
import QtTest
import Lens

/**
 * The bubble's appearance as one word (Bubble.phase): the seven states it names, and the dismissal
 * that cancels a show which has not reached its placement turn yet.
 *
 * A file of its own because the bubble's main case sits at the QML size cap (docs/QML.md section 1),
 * the way the waiting and reveal cases already do.
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
        name: "BubblePhase"
        when: windowShown

        function test_theCardNamesItsOwnPhase() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            compare(bubble.phase, "hidden");

            bubble.show({
                title: "bank", type: "word", waiting: true, hold: true, waitingLines: 4,
                countsDown: false, reveal: false, x: 200, y: 240
            });
            compare(bubble.phase, "pending", "a show that has not reached its placement turn is not up yet");
            tryCompare(bubble, "phase", "waiting");

            bubble.show({
                title: "bank", type: "word", sameCard: true, waiting: false, hold: true,
                en: "an institution for keeping money", countsDown: false, reveal: false,
                x: 200, y: 240
            });
            compare(bubble.phase, "holding", "an answer still on its way is not a settled card");

            bubble.show({
                title: "bank", type: "word", sameCard: true, waiting: false, hold: false,
                en: "an institution for keeping money", countsDown: false, reveal: false,
                x: 200, y: 240
            });
            compare(bubble.phase, "shown");
            // Let the arrival fade finish: a card dismissed while it is still fully transparent
            // has nothing left to fade out and goes straight to hidden, which is correct but not
            // the phase under test here.
            wait(Tokens.motion.pop + 80);

            bubble.dismiss();
            if (Tokens.motion.reduced) {
                compare(bubble.phase, "hidden", "reduced motion takes the card down without a fade");
            } else {
                compare(bubble.phase, "fading", "the window is still on screen while the card fades out");
                tryCompare(bubble, "phase", "hidden", Tokens.motion.pop + 400);
            }

            // The duty can show and clear a card in one turn: the placement timer must not put the
            // cleared card up after the dismissal.
            bubble.show({title: "bank", type: "word", reveal: false, x: 200, y: 240});
            compare(bubble.phase, "pending");
            bubble.dismiss();
            wait(Tokens.motion.pop + 100);
            compare(bubble.phase, "hidden");
            compare(bubble.visible, false, "a card cleared before its first frame was shown anyway");
        }
    }
}
