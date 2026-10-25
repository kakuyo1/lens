import QtQuick
import QtTest
import Lens
import "../"

/**
 * The definition chain's traffic: what a step's answer does to the trail, and what a language
 * replay of the drawn step does to it instead.
 *
 * The panel is driven through its own functions, not through a window: what a tap on a word
 * raises belongs to the duty and is covered there. What is here is the trail the card draws.
 */
Item {
    id: root
    width: 320
    height: 300

    TokenHost { id: host }

    /// The card the panel stands on; only the fields the panel reads.
    QtObject {
        id: card
        property var tokens: host.tokens
        property string title: "bank"
        property string englishFont: "Test"
        property real fontScale: 1
        property bool revealAnimation: false
        property int revealCharacterMs: 0
    }

    Component {
        id: panelComponent
        BubbleChainPanel { width: 260 }
    }

    TestCase {
        id: testCase
        name: "BubbleChainPanel"
        when: windowShown

        function opened() {
            const panel = createTemporaryObject(panelComponent, host.contentItem, {bubble: card});
            verify(panel);
            return panel;
        }

        function test_aStepJoinsTheTrail() {
            const panel = opened();
            panel.pending = "knack";
            panel.receive({title: "knack", ipa: "", text: "a clever trick"});

            compare(panel.chain.length, 1);
            compare(panel.chain[0].title, "knack");
            compare(panel.trail.length, 2, "the trail lost the card's own word or the step");
            compare(panel.atRoot, false);
            compare(panel.pending, "");
        }

        /// An answer for a word that is not the one waited for is dropped: the reader went back
        /// or asked for something else meanwhile.
        function test_anAnswerForAnotherWordIsDropped() {
            const panel = opened();
            panel.pending = "knack";
            panel.receive({title: "knack", ipa: "", text: "a clever trick"});
            panel.pending = "grace";
            panel.receive({title: "knack", ipa: "", text: "窍门"});

            compare(panel.chain.length, 1);
            compare(panel.chain[0].text, "a clever trick");
            compare(panel.pending, "grace");
        }
    }
}
