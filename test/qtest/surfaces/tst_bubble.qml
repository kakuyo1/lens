import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The bubble's countdown and the pronunciation that rides beside the word.
 *
 * The clock is the view's own behaviour (Bubble.qml), so it is checked here, where the real
 * Controller is behind the surface: show() is the only way in, and the count it starts is the
 * one the reader sees. Placement and the drag are the subjects of the sibling cases and of the
 * real-surface tools (TEST.md section 5); what this file cannot see, it does not touch.
 */
Item {
    id: root
    width: 480
    height: 320

    Component {
        id: bubbleComponent
        Bubble {}
    }

    Component {
        id: collocationsComponent
        ChainCollocations {}
    }

    TestCase {
        id: testCase
        name: "Bubble"
        when: windowShown

        /// A bubble as the controller hands it over, with its word, its pronunciation and both
        /// explanations filled in.
        /// Every case starts from the default display time, so a case that changes it cannot leave the
        /// countdown checks below reading a bubble of some other length.
        function init() {
            Controller.setSetting("bubbleDismissSeconds", 5);
        }

        function opened() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                en: "the occurrence of events by chance in a happy way",
                status: "new",
                x: 200,
                y: 240
            });
            // Short of one tick of the countdown, so the number read back is still the full one.
            wait(50);
            return bubble;
        }

        /// @return The window's own root item, which is the whole surface a reader sees.
        ///
        /// grabImage renders the item it is handed rather than the window around it -- handed
        /// the meta line it wrote a 238x26 strip -- and a Window is not an item, so the surface
        /// is named by the contentItem that holds all of it.
        function surfaceOf(bubble) {
            return bubble.contentItem;
        }

        /// @return The meta line, found by its wording rather than by its place in the column.
        function metaOf(bubble) {
            const found = Util.findAll(bubble, function (o) {
                return typeof o.text === "string" && o.text.indexOf("Disappears in") === 0;
            });
            compare(found.length, 1);
            return found[0];
        }

        /// The shortcut draws the catalog's list, not a four-item copy: the fifth language and
        /// the narrower 11 px cell are what keep Traditional Chinese on one row.
        function test_theLanguageStripOffersEveryCatalogLanguage() {
            const bubble = opened();
            wait(200);
            const button = Util.findAll(bubble, function (o) {
                return o.Accessible !== undefined && o.Accessible.name === "Explanation language";
            })[0];
            verify(button, "the bubble has no explanation-language shortcut");
            mouseClick(button, button.width / 2, button.height / 2);
            wait(200);
            const segment = Util.findAll(bubble, function (o) {
                return o.toString().indexOf("Segment_QMLTYPE") === 0;
            })[0];
            verify(segment, "the language strip did not open");
            compare(segment.labels.length, Controller.settings.languages.length);
            verify(segment.labels.indexOf("繁體中文") >= 0,
                   "the language strip did not offer Traditional Chinese");
            compare(segment.labelSize, 11, "five labels need the strip's narrower cell text");
            bubble.visible = false;
        }

        /// @return The whole seconds the meta line is showing.
        function secondsOf(meta) {
            const matched = meta.text.match(/([0-9]+)s/);
            verify(matched);
            return Number(matched[1]);
        }

        /// The controller sends one answer language, never two (explanation_duty.cpp:486-489).
        /// The fixture has to do the same or it tests a card production cannot draw.
        function test_theFixtureCarriesOneAnswerLanguage() {
            const bubble = opened();
            const populated = [bubble.english, bubble.chinese, bubble.translation].filter(function (text) {
                return text !== "";
            });
            compare(populated.length, 1, "the fixture carries more than one answer language");
            compare(bubble.english, "the occurrence of events by chance in a happy way");
        }

        /// The explanation body is the second text step on an opaque card and the first on glass:
        /// the second step is not AA over the 40% white glass (docs/adr/0106).
        function test_glassBodyUsesTheReadableTextStep() {
            const previous = Tokens.theme;
            Tokens.theme = "light";
            let bubble = opened();
            let definition = Util.findAll(bubble, function (o) {
                return o.objectName === "definitionText";
            })[0];
            verify(definition, "the bubble has no definition line");
            compare(definition.color.toString(), bubble.tokens.muted.toString(),
                    "the solid card's body left its second text step");
            bubble.visible = false;

            Tokens.theme = "frosted";
            bubble = opened();
            compare(bubble.tokens.glassMode, "frosted");
            definition = Util.findAll(bubble, function (o) {
                return o.objectName === "definitionText";
            })[0];
            compare(definition.color.toString(), bubble.tokens.text.toString(),
                    "the glass card's body did not take the first text step");
            const ipa = Util.textWith(Util.textsUnder(bubble), ["/ˌserənˈdɪpəti/"]);
            verify(ipa, "the bubble has no pronunciation line");
            compare(ipa.color.toString(), bubble.tokens.faint.toString(),
                    "metadata moved off the faint step with the body");
            Tokens.theme = previous;
        }

        /// The verdict row says what its two choices do once, on the first opening. The sentence
        /// is latched for that opening even though the document flag flips at once, and the next
        /// bubble's row carries the two pills alone.
        function test_theVerdictRowExplainsItselfOnce() {
            Controller.setSetting("verdictHintSeen", false);
            const first = opened();
            first.hovering = true;
            const hint = Util.findAll(first, function (o) {
                return o.objectName === "verdictHint";
            })[0];
            verify(hint, "the verdict row carries no hint");
            tryCompare(hint, "visible", true);
            const known = Util.findAll(first, function (o) {
                return o.Accessible !== undefined && o.Accessible.role === Accessible.Button
                       && o.Accessible.name === qsTr("Known");
            })[0];
            verify(known, "the Known pill is missing");
            compare(known.Accessible.description, hint.text,
                    "the screen reader does not hear the one-time hint");
            compare(Controller.settings.verdictHintSeen, false,
                    "the hint was marked as seen before it had stood for a moment");
            tryCompare(Controller.settings, "verdictHintSeen", true, 2500);
            first.visible = false;
            first.destroy();

            const second = opened();
            second.hovering = true;
            wait(50);
            compare(Util.findAll(second, function (o) {
                return o.objectName === "verdictHint";
            })[0].visible, false, "the hint returned on a later bubble");
            const knownAgain = Util.findAll(second, function (o) {
                return o.Accessible !== undefined && o.Accessible.role === Accessible.Button
                       && o.Accessible.name === qsTr("Known");
            })[0];
            verify(knownAgain, "the Known pill is missing on the later bubble");
            compare(knownAgain.Accessible.description, "",
                    "the spent hint is still attached to the verdict");
        }

        /// A fresh bubble starts at its full five.
        function test_aFreshBubbleShowsTheWholeCount() {
            const bubble = opened();
            compare(metaOf(bubble).text, "Disappears in 5s");
        }

        /// The display time the reader chose is the one a fresh bubble counts down from. init() puts
        /// the default back before the next case.
        function test_aFreshBubbleCountsFromTheChosenDisplayTime() {
            Controller.setSetting("bubbleDismissSeconds", 12);
            const bubble = opened();
            compare(metaOf(bubble).text, "Disappears in 12s");
        }

        /// The number is the clock rather than the setting that seeded it: waiting a second of
        /// real time steps it down.
        ///
        /// The clock starts when the answer has stopped being written out rather than when the card
        /// appears (UI-SPEC.md 4.3), so the step down is waited for from there. What the seconds read
        /// while the reveal is still running is the subject of tst_bubble_reveal.
        function test_theSecondsStepDownWhileTheBubbleIsUp() {
            const bubble = opened();
            const meta = metaOf(bubble);
            compare(secondsOf(meta), 5);

            tryVerify(function () { return !bubble.revealing; }, 5000);
            tryVerify(function () { return secondsOf(meta) < 5; }, 2000);
            verify(secondsOf(meta) >= 1);
        }

        /// Holding the bubble stops the clock where it is, and letting go carries on from there
        /// rather than refilling it to five.
        ///
        /// The clock only runs once the answer has been written out (UI-SPEC.md 4.3), so the hold begins
        /// from the first second it takes off rather than from the card's arrival.
        function test_holdingTheBubbleStopsTheCount() {
            const bubble = opened();
            tryVerify(function () { return !bubble.revealing; }, 5000);
            tryVerify(function () { return bubble.remainingMs < 4000; }, 2000);

            bubble.hovering = true;
            const held = bubble.remainingMs;
            wait(600);
            compare(bubble.remainingMs, held);

            bubble.hovering = false;
            tryVerify(function () { return bubble.remainingMs < held; }, 1000);
        }

        /// The clock cannot see the AT review pass; an accessible press refills it, the way the
        /// pointer holds it, so a card being read aloud does not vanish mid-sentence.
        function test_aScreenReaderPressRefillsTheReadingClock() {
            const bubble = opened();
            tryVerify(function () { return !bubble.revealing; }, 5000);
            tryVerify(function () { return bubble.remainingMs < 4000; }, 2000);

            bubble.readerActed();
            compare(bubble.remainingMs, 5000, "the accessible press did not refill the reading window");
        }

        /// A collocation is a footnote, not a line the card may overflow: a long one wraps inside
        /// the card instead of painting past its right edge.
        function test_longCollocationsWrapInsideTheCard() {
            const line = createTemporaryObject(collocationsComponent, root, {
                width: 160,
                tokens: Tokens,
                words: ["a deliberately long collocation that would run past the card",
                        "another long one"]
            });
            verify(line, "the collocation line is not drawn");
            compare(line.wrapMode, Text.WordWrap);
            tryVerify(function () { return line.lineCount > 1; }, 1000,
                      "the long collocation stayed on one line; width " + line.width
                      + ", text length " + line.text.length);
        }

        /// The pronunciation is drawn, and it sits beside the word rather than behind it.
        function test_theWordCarriesItsPronunciation() {            const bubble = opened();
            const ipa = Util.textWith(Util.textsUnder(bubble), ["/ˌserənˈdɪpəti/"]);
            verify(ipa);
            compare(ipa.visible, true);
            const word = Util.textWith(Util.textsUnder(bubble), ["serendipity"]);
            verify(word.width > 0, "the title row gave the word no width");
        }

        /// A pointer arriving on the card is recorded, and the recording is the card rather than
        /// the window around it. Those two readings are what TODO.md's hover-jitter sighting
        /// needs; this case says the block that writes them runs without throwing.
        function test_hoveringTheCardRecordsIt() {
            const bubble = opened();
            const word = Util.textWith(Util.textsUnder(bubble), ["serendipity"]);
            verify(word);

            mouseMove(word, word.width / 2, word.height / 2);

            tryVerify(function () { return bubble.lastHoverCard.width > 0; }, 1000);
            compare(bubble.hovering, true);
            // The card, not the window: the two shadow margins are not part of it.
            compare(bubble.lastHoverCard.width, bubble.width - 2 * bubble.shadowMargin);
        }

        function test_captureTheBubble() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const bubble = opened();
            // The answer is written out a character at a time: the snapshot is of the card the
            // reader ends up looking at, so the reveal is waited out rather than photographed
            // part-way through.
            tryVerify(function () { return !bubble.revealing; }, 8000);
            wait(100);
            verify(Util.saveSnapshot(testCase, surfaceOf(bubble), lensQaSnapshotDir, "bubble"));
        }

        /// The bubble the word list raises carries no countdown, and the line that would have
        /// carried it goes with it: a card with a gap where the seconds were reads as a card that
        /// stopped drawing. The pair of snapshots is what shows that, since the difference is the
        /// absence of a line rather than a value a case can compare.
        function test_captureTheBubbleWithoutItsCountdown() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                zh: "机缘巧合",
                status: "new",
                countsDown: false,
                x: 200,
                y: 240
            });
            tryVerify(function () { return !bubble.revealing; }, 8000);
            wait(100);

            compare(metaOf(bubble).visible, false, "a bubble with no countdown still drew its clock");
            verify(Util.saveSnapshot(testCase, surfaceOf(bubble), lensQaSnapshotDir, "bubble-review"));
        }

        /// The bubble's window never takes focus (UI-SPEC.md 4.3), so a screen reader reaches it
        /// by review rather than by focus. This case walks the surface's item tree the way the
        /// platform bridge does -- the no-focus contract is the flag the window is shown with --
        /// and asks that every pressable thing carries a name: the two verdicts, the close cross,
        /// the chain's words.
        function test_theSurfaceNamesItsPressablesWithoutTakingFocus() {
            const bubble = opened();
            const surface = surfaceOf(bubble);
            tryVerify(function () { return !bubble.revealing; }, 8000);

            const names = [];
            Util.findAll(surface, function (o) {
                if (o.Accessible !== undefined && o.Accessible.role === Accessible.Button)
                    names.push(String(o.Accessible.name));
                return false;
            });

            verify((bubble.flags & Qt.WindowDoesNotAcceptFocus) !== 0,
                   "the bubble dropped the flag that keeps it from taking focus");
            verify(names.indexOf("Known") >= 0, "the Known verdict is unnamed: " + names);
            verify(names.indexOf("New") >= 0, "the New verdict is unnamed: " + names);
            verify(names.indexOf("Close") >= 0, "the close cross is unnamed: " + names);
            verify(names.every(function (n) { return n !== ""; }),
                   "a pressable thing on the card carries no name at all");
        }
    }
}
