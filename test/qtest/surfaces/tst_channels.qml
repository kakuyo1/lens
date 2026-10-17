import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 800
    height: 600

    Component {
        id: barComponent
        SelectionBar {}
    }

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        name: "Channels"
        when: windowShown

        function actionLabels(bar) {
            return Util.textsUnder(bar).map(function (item) { return item.text; });
        }

        function test_theActionBarKeepsAllActionsForEachChannel() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            const kinds = ["word", "entity", "sentence", "translation"];
            for (let i = 0; i < kinds.length; ++i) {
                bar.selectionText = "selection";
                bar.selectionKind = kinds[i];
                bar.open();
                tryCompare(bar, "selectionKind", kinds[i]);
                const labels = actionLabels(bar);
                verify(labels.indexOf("Translate") >= 0);
                verify(labels.indexOf("Explain") >= 0);
                verify(labels.indexOf("Copy text") >= 0);
                bar.dismiss();
            }
        }

        function test_entityBubbleHasNoWordOnlyFields() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "New York",
                type: "entity",
                en: "A city in the United States.",
                zh: "美国的一座城市。",
                x: 200,
                y: 240
            });
            tryCompare(bubble, "title", "New York");
            compare(bubble.type, "entity");
            compare(bubble.ipa, "");
            compare(bubble.status, "");
            // A named thing is not a learning item: no type badge, unlike the word bubble
            // (Known / New) and the sentence bubble (Sentence). The empty status keeps the
            // verdict row from ever showing on an entity.
            verify(!Util.textWith(Util.textsUnder(bubble), ["Entity"]));
            compare(Util.textWith(Util.textsUnder(bubble), ["Sentence"]), null);
        }

        function test_sentenceBubbleDrawsNoTitleButKeepsTheSentence() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "The quiet room felt different.",
                type: "sentence",
                en: "The room seemed changed.",
                zh: "这个房间似乎变了。",
                x: 200,
                y: 240
            });
            tryCompare(bubble, "title", "The quiet room felt different.");
            compare(bubble.type, "sentence");
            compare(bubble.ipa, "");
            compare(bubble.status, "");
            verify(Util.textWith(Util.textsUnder(bubble), ["Sentence"]));
            // The sentence is carried but not drawn: a long selection would overflow the card.
            const title = Util.textWith(Util.textsUnder(bubble), ["The quiet room felt different."]);
            verify(title);
            verify(!title.visible);
        }

        function test_translationBubbleDrawsTheEnglishAndNothingElse() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "今天天气不错。",
                type: "translation",
                en: "The weather is nice today.",
                zh: "",
                x: 200,
                y: 240
            });
            tryCompare(bubble, "title", "今天天气不错。");
            compare(bubble.type, "translation");
            wait(50);

            // What the reader asked for is the English of it, so the Chinese they selected is
            // carried (the app stamps the selection on) rather than drawn. No badge heads the card:
            // a translation is not a sentence, and the empty status is what keeps the word verdict
            // row shut on it, the way it does on an entity.
            const title = Util.textWith(Util.textsUnder(bubble), ["今天天气不错。"]);
            verify(title);
            verify(!title.visible);
            compare(bubble.status, "");
            compare(Util.textWith(Util.textsUnder(bubble), ["Sentence"]), null);

            const line = Util.textWith(Util.textsUnder(bubble), ["The weather is nice today."]);
            verify(line, "the translation was not drawn");
            compare(line.visible, true);
        }
    }
}
