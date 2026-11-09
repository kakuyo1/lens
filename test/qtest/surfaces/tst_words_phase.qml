import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The removal question as one word -- hidden/asking/closing -- and the panel's list staying
 * pressless until the question's window is gone, its fade included (UI-SPEC.md 4.8).
 *
 * The panel's own phase waits for the split QML.md section 1 asks of a file at this size:
 * `WordsPopup.qml` sits 136 characters under the hard cap, so a panel-level projection would
 * cross it. The question is the panel's only multi-flag machine; `visible`/`leaving` are one
 * flag each.
 */
Item {
    id: root
    width: 640
    height: 480

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    TestCase {
        id: testCase
        name: "WordsPhase"
        when: windowShown

        function test_theQuestionNamesItsPhaseAndHoldsTheList() {
            // The panel is born on whatever the model holds; put it back on the whole list first,
            // the way the panels suite does, so the case does not inherit another case's filter.
            Controller.wordsModel.filter = 0;
            tryVerify(function () { return !Controller.wordsModel.filterSwitching; }, 5000);
            const panel = createTemporaryObject(wordsComponent, root);
            verify(panel);
            wait(50);
            const question = Util.ofType(panel, "RemovalQuestion");
            verify(question, "the panel has no removal question");
            compare(question.phase, "hidden");
            // The list's card: the one in the panel's own window, not the question's card.
            const card = Util.findAll(panel, function (o) {
                return o.toString().indexOf("ShadowCard_QMLTYPE") === 0 && o.Window.window === panel;
            })[0];
            verify(card, "the panel drew no card");
            compare(card.enabled, true);

            panel.visible = true;
            panel.requestRemoval("bank");
            compare(question.phase, "asking");
            compare(card.enabled, false, "the list took presses under the question");

            panel.closeChild();
            compare(question.phase, "closing", "the question went before its fade");
            compare(card.enabled, false, "the list took presses while the question was still on screen");
            tryCompare(question, "phase", "hidden", Tokens.motion.pop + 400);
            compare(card.enabled, true);
            panel.visible = false;
        }
    }
}
