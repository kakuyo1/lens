import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * What a press on the words panel's own reading does to the panel.
 *
 * A row's hover raises the review card above the word (UI-SPEC.md 4.8), as a window of its own.
 * The card is taller than the panel's header, so its title row -- the word and the pronunciation a
 * reader presses to hear it -- stands over the panel's top edge while the row it belongs to is
 * inside the panel. The press comes from the mouse hook, and no offscreen case injects one, so the
 * cases hand dismissOutside() the point the hook would report, the way tst_bar_dismiss.qml does.
 * tst_review.qml owns the hover half; this file raises the card through the controller and reads
 * the panel.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "WordsDismiss"
        when: windowShown

        /// The word the fixture stores an explanation for (settings.json), so the raised card is
        /// the stored one and no request leaves.
        readonly property string stored: "panel"

        function initTestCase() {
            Controller.setSetting("explanationLang", "zh");
            Controller.setSetting("multiSense", false);
        }

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        function surface(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        function cleanup() {
            Controller.explanationDuty.dismissBubble();
            Controller.explanationDuty.dismissNotice();
        }

        /// @return The panel's card rectangle, as dismissOutside() reads it.
        function cardOf(window) {
            const margin = window.shadowMargin;
            return Qt.rect(window.x + margin, window.y + margin,
                           window.width - 2 * margin, window.height - 2 * margin);
        }

        /// @brief Raise the stored word's review card from the row that draws it.
        function raiseStoredCard(words, bubble) {
            const word = Util.textWith(Util.textsUnder(words), [stored]);
            verify(word, "the list is not showing the stored word");
            // The row's own anchor: the word text's global rectangle, in the native pixels the
            // row sends when its dwell runs out.
            const at = word.mapToGlobal(0, 0);
            Controller.reviewWord(stored, Math.round(at.x), Math.round(at.y),
                                  Math.round(word.width), Math.round(word.height));
            tryCompare(bubble, "surfaceShown", true);
            tryCompare(bubble, "cardKind", "review");
            wait(50);
        }

        /// A press on the card the panel raised is a press inside the panel: the reader is reading
        /// that row, and the card's title row is where they press to hear the word. The press must
        /// not take the panel down -- which takes the card with it -- under their hand.
        function test_aPressOnTheReviewCardKeepsThePanelUp() {
            const main = make();
            const host = Util.surfaceHost(main);
            const words = surface(main, "WordsPopup");
            const bubble = surface(main, "Bubble");
            host.showWords(null, 0);
            tryCompare(words, "visible", true);
            wait(50);

            raiseStoredCard(words, bubble);

            // The fixture's stored answer draws a short card, so it stands wholly inside the
            // panel's card; a longer answer's title row stands over the panel's top edge. Lift the
            // card to that frame -- a card the reader dragged keeps the frame it was left at.
            bubble.dragging = true;
            bubble.y = Math.max(0, words.y - bubble.height - 10);
            bubble.dragging = false;
            wait(50);

            const ipa = Util.textWith(Util.textsUnder(bubble), [Controller.explanationDuty.bubble.ipa]);
            verify(ipa, "the review card drew no pronunciation");
            const press = ipa.mapToGlobal(ipa.width / 2, ipa.height / 2);
            verify(!host.contains(cardOf(words), press),
                   "the lifted card still overlaps the panel; the press is not the one under test");

            host.dismissOutside(press);

            compare(words.visible, true, "a press on the pronunciation closed the panel");
            compare(bubble.surfaceShown, true, "a press on the pronunciation closed the card");
        }

        /// The rule's other half: a press that lands on neither the panel nor its card still closes
        /// the panel, and the card goes with it the moment the panel does.
        function test_aPressAwayStillClosesThePanel() {
            const main = make();
            const host = Util.surfaceHost(main);
            const words = surface(main, "WordsPopup");
            const bubble = surface(main, "Bubble");
            host.showWords(null, 0);
            tryCompare(words, "visible", true);
            wait(50);

            raiseStoredCard(words, bubble);

            host.dismissOutside(Qt.point(0, 0));

            compare(words.visible, false, "a press away from the panel left it up");
            tryVerify(function () { return !bubble.surfaceShown; }, 1000,
                      "the card outlived the panel that raised it");
        }
    }
}
