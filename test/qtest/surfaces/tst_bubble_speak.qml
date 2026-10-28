import QtQuick
import QtTest
import Lens

/**
 * The speaker glyph that rides beside a word card's pronunciation (docs/adr/0134, UI-SPEC.md 4.3).
 *
 * The speaker is the one control that voices the headword, so it is the part of speech and the
 * pronunciation that it must sit beside, and the only card kind it may appear on. What this reads
 * is the glyph's container: a hidden parent leaves the child's own `visible` true, which is the
 * failure a structural assertion cannot see on its own.
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
        name: "BubbleSpeak"
        when: windowShown

        /// A word bubble as the controller hands one over, with a pronunciation to sit beside.
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

        /// The speaker rides beside the pronunciation and nowhere else (UI-SPEC.md 4.3, docs/adr/0134):
        /// a word card has one, a sentence and a card with no pronunciation do not, and the settings
        /// preview -- a demonstration of a setting rather than a word the reader looked up -- does not
        /// either. What is asserted is the glyph's container, because a hidden parent leaves the
        /// child's own `visible` true.
        function test_theSpeakerRidesBesideThePronunciationOnly() {
            const bubble = opened();
            const speaker = speakerOf(bubble);
            verify(speaker, "the word card drew no speaker");
            compare(speaker.visible, true, "a word card with a pronunciation drew no speaker");
            // The art itself: a path that does not resolve draws an empty box, which is the one
            // failure a structural assertion cannot see.
            const glyph = speakerGlyphOf(bubble);
            verify(glyph, "the speaker is not drawn from a file");
            compare(glyph.status, Image.Ready, "the speaker's glyph did not load: " + glyph.source);

            // The pronunciation and the glyph are one control to a screen reader, so the row is
            // named once for the pair and the press reads the word.
            const named = Util.findAll(bubble, function (o) {
                return o.Accessible !== undefined && String(o.Accessible.name).indexOf("Pronounce serendipity") === 0;
            });
            compare(named.length, 1, "the pronunciation and the speaker do not share one name");

            // A sentence card has no headword to read.
            const sentence = createTemporaryObject(bubbleComponent, root);
            verify(sentence);
            sentence.show({title: "the quick brown fox", type: "sentence", en: "A pangram.", x: 200, y: 240});
            wait(50);
            const sentenceSpeaker = speakerOf(sentence);
            verify(sentenceSpeaker, "the sentence card drew no title row at all");
            compare(sentenceSpeaker.visible, false, "a sentence card drew a speaker");

            // A card whose pronunciation is switched off arrives with no IPA, and the glyph goes with
            // it: there is nothing for it to stand beside.
            const bare = createTemporaryObject(bubbleComponent, root);
            verify(bare);
            bare.show({title: "spring", type: "word", ipa: "", en: "The season between winter and summer.", x: 200, y: 240});
            wait(50);
            compare(speakerOf(bare).visible, false, "a card with no pronunciation drew a speaker");

            // The settings preview wears the same card, and must not invite a press that would
            // read a demonstration word out loud.
            const preview = createTemporaryObject(bubbleComponent, root);
            verify(preview);
            preview.preview = true;
            preview.show({title: "serendipity", type: "word", ipa: "/ˌserənˈdɪpəti/", en: "A happy accident.", x: 200, y: 240});
            wait(50);
            compare(speakerOf(preview).visible, false, "the settings preview drew a speaker");
        }

        /// @return The item the speaker glyph is drawn in, or null. Its container is what carries
        ///         `visible`, so that is what a case reads.
        ///
        /// The card carries two title rows: its own and the chain panel's, which is empty and hidden
        /// until the reader steps into a sense. The one a case means is the one wearing this card's
        /// word, so the word is what picks it out.
        function speakerOf(bubble) {
            const rows = Util.findAll(bubble, function (o) {
                return o.toString().indexOf("BubbleTitleRow_QMLTYPE") === 0 && o.title === bubble.title;
            });
            if (rows.length === 0)
                return null;
            const glyph = Util.findAll(rows[0], function (o) {
                return typeof o.source === "string" && o.source.indexOf("ui-speaker") >= 0;
            })[0];
            return glyph ? glyph.parent : null;
        }

        /// @return The Image the speaker glyph is loaded into, or null. The Icon around it is an
        ///         item with a `source` and no `status`; the image is what knows whether the file
        ///         was found.
        function speakerGlyphOf(bubble) {
            return Util.findAll(bubble, function (o) {
                // The image's source is a QUrl, not the string the Icon holds, so it is compared
                // through String() -- and `status` is what tells the two apart.
                return o.status !== undefined && String(o.source).indexOf("ui-speaker") >= 0;
            })[0];
        }
    }
}
