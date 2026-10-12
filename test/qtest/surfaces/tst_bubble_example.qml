import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The example sentence, under the origin: what the card draws when there is one, and what it leaves
 * out when there is not.
 *
 * The subject is the same as the origin's -- a reader asked for two different things, and the card
 * has to read as two things (UI.md 4.3). This checks the block's presence, its text, and its place
 * under the origin. Whether a sentence is asked for at all is the setting's business and the duty's,
 * and that pair is covered by the offline suite.
 */
Item {
    id: root
    width: 480
    height: 460

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        id: testCase
        name: "BubbleExample"
        when: windowShown

        /// A word bubble showing @p example, or none at all when it is left out.
        ///
        /// The drag flag holds the countdown, the way the other bubble cases do: the card is compared
        /// over more than one frame here, and a clock that takes it down mid-case would report a
        /// missing label rather than a missing block.
        function opened(example, etymology) {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const payload = {
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                zh: "机缘巧合",
                status: "new",
                x: 200,
                y: 360
            };
            if (etymology !== undefined)
                payload.etymology = etymology;
            if (example !== undefined)
                payload.example = example;
            bubble.show(payload);
            tryCompare(bubble, "visible", true);
            bubble.dragging = true;
            return bubble;
        }

        function named(bubble, name) {
            return Util.findAll(bubble, function (item) { return item.objectName === name; });
        }

        function blockOf(bubble) {
            const block = named(bubble, "exampleBlock");
            compare(block.length, 1);
            return block[0];
        }

        function test_theExampleIsSetOffUnderTheMeaningWithARuleAndALabel() {
            const bubble = opened("The bank raised its interest rate.");
            const block = blockOf(bubble);
            compare(block.visible, true);

            const text = named(bubble, "exampleText");
            compare(text.length, 1);
            compare(text[0].text, "The bank raised its interest rate.");
            compare(text[0].textFormat, Text.PlainText, "a model's sentence is text, not markup");
            compare(text[0].wrapMode, Text.Wrap, "a long sentence has to wrap rather than run off the card");

            const label = named(bubble, "exampleLabel");
            compare(label.length, 1);
            compare(label[0].text, "Example");

            const separator = named(bubble, "exampleSeparator");
            compare(separator.length, 1);
            compare(separator[0].height, 1);
            compare(separator[0].color, Tokens.line2);
            compare(separator[0].width, block.width, "the rule does not span the card");

            // The example comes after the meaning, and after the origin when there is one.
            const meaning = Util.textWith(Util.textsUnder(bubble), ["机缘巧合"]);
            verify(meaning);
            verify(block.y > meaning.y, "the example is not drawn under the meaning");
            compare(block.x, meaning.x, "the rule starts where the meaning does, or the two columns disagree");

            compare(meaning.color, Tokens.text);
            compare(text[0].color, Tokens.muted);
        }

        /// No example, no block: an empty slot would say the model failed rather than that it had none.
        function test_aWordWithoutAnExampleDrawsNoBlockAtAll() {
            const bubble = opened(undefined);
            compare(bubble.example, "");
            compare(blockOf(bubble).visible, false);
        }

        /// The example follows the origin in the card's column: both are set off, neither hides the other.
        function test_theExampleSitsBelowTheOriginWhenBothAreThere() {
            const bubble = opened("The bank raised its interest rate.", "Old Norse banki, a bench or ridge.");
            const origin = named(bubble, "etymologyBlock");
            compare(origin.length, 1);
            compare(origin[0].visible, true);
            verify(blockOf(bubble).y > origin[0].y, "the example is not drawn after the origin");
        }

        /// A refreshed card is the same card: the next word must not inherit the last one's example.
        function test_refreshingTheCardTakesTheExampleWithIt() {
            const bubble = opened("The bank raised its interest rate.");
            compare(blockOf(bubble).visible, true);
            bubble.show({title: "bank", type: "word", zh: "河岸", status: "new", x: 200, y: 360});
            compare(bubble.example, "");
            compare(blockOf(bubble).visible, false);
        }

        function test_snapshotBothThemes() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const bubble = opened("The bank raised its interest rate.", "Old Norse banki, a bench or ridge.");
                    waitForRendering(bubble.contentItem);
                    verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "bubble-example-" + mode));
                    compare(blockOf(bubble).visible, true);
                }
            } finally {
                Tokens.theme = theme;
            }
        }
    }
}
