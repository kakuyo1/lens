import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The breakdown and the correction cards: the sentence in its role tints, the legend, the pending and
 * failed lines, the underline on a corrected word, and the tag that names the correction. They live apart
 * from the bubble's own cases so each file stays under the QML size cap (QML.md section 1).
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
        name: "BubbleBreakdown"
        when: windowShown

        /// Every case starts from the default display time, so a case that changes it cannot leave a
        /// countdown reading of another length behind.
        function init() {
            Controller.setSetting("bubbleDismissSeconds", 5);
        }

        /// @return The window's own root item, which is the whole surface a reader sees.
        function surfaceOf(bubble) {
            return bubble.contentItem;
        }

        /// @return The peek globe, found by its screen-reader name rather than by its place.
        function globeOf(bubble) {
            return Util.findAll(bubble, function (o) {
                return o.Accessible !== undefined && o.Accessible.name === "Mother tongue translation";
            })[0];
        }

        /// The breakdown's payload as the duty sends it: pending holds the sentence as one run, and
        /// ready splits it into role runs; whitespace stays in its own run either way.
        function breakdownPayload(state, roles, failure) {
            const segments = state === "ready"
                ? [{text: "The rain", role: "S"}, {text: " ", role: ""}, {text: "stopped.", role: "V"}]
                : [{text: "The rain stopped.", role: ""}];
            return {
                title: "", type: "sentence", breakdown: state, segments: segments, roles: roles, failure: failure || "",
                countsDown: true, verdictButtons: false, reveal: false, x: 200, y: 240
            };
        }

        /// The clock waits while the breakdown is pending and starts when the answer is ready
        /// (UI-SPEC.md 4.3).
        function test_theBreakdownClockStartsOnlyWhenTheAnswerIsReady() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show(breakdownPayload("pending", []));
            wait(50);
            const held = bubble.remainingMs;
            wait(400);
            compare(bubble.remainingMs, held, "the clock ran while the breakdown was pending");

            bubble.show(breakdownPayload("ready", ["S", "V"]));
            tryVerify(function () { return bubble.remainingMs < held; }, 1500, "the clock did not start at ready");
            bubble.visible = false;
        }

        /// The card is labelled Breakdown and offers no peek globe: its answer is the sentence's
        /// structure, not a meaning to ask the mother tongue for.
        function test_theBreakdownCardIsLabelledWithoutAGlobe() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show(breakdownPayload("ready", ["S", "V"]));
            wait(50);
            verify(Util.textWith(Util.textsUnder(bubble), ["Breakdown"]), "the label does not read Breakdown");
            const globe = globeOf(bubble);
            verify(!globe || !globe.visible, "the breakdown card offers the peek globe");
            bubble.visible = false;
        }

        /// The legend names the roles the sentence has and no others; pending and failed each write
        /// their line where the legend stands.
        function test_theLegendNamesTheRolesAndThePendingAndFailedStatesWriteALine() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show(breakdownPayload("ready", ["S", "V"]));
            wait(50);
            verify(Util.textWith(Util.textsUnder(bubble), ["Subject"]), "the legend has no subject");
            verify(Util.textWith(Util.textsUnder(bubble), ["Verb"]), "the legend has no verb");
            compare(Util.textWith(Util.textsUnder(bubble), ["Adverbial"]), null,
                    "the legend named a role the sentence does not have");

            bubble.show(breakdownPayload("pending", []));
            wait(50);
            verify(Util.textWith(Util.textsUnder(bubble), ["Breaking down…"]), "pending drew no line");

            bubble.show(breakdownPayload("failed", [], "No connection or timeout."));
            wait(50);
            verify(Util.textWith(Util.textsUnder(bubble), ["Failed: No connection or timeout."]),
                   "failed drew no line");
            bubble.visible = false;
        }

        /// A legend wider than the card wraps onto a second line, and no role name runs past the card's edge.
        function test_theLegendWrapsInsideTheCardWhenItIsWiderThanIt() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({title: "", type: "sentence", breakdown: "ready", roles: ["S", "V", "C", "A"], countsDown: true,
                         verdictButtons: false, reveal: false, x: 200, y: 240,
                         segments: [{text: "The dog", role: "S"}, {text: " chased ", role: "V"},
                                    {text: "the cat", role: "C"}, {text: " quickly.", role: "A"}]});
            wait(50);
            const block = breakdownOf(bubble);
            const names = Util.textsUnder(bubble).filter(function (t) {
                return ["Subject", "Verb", "Complement", "Adverbial"].indexOf(t.text) >= 0;
            });
            compare(names.length, 4, "the legend did not name all four roles");
            const lines = {};
            names.forEach(function (t) { lines[Math.round(t.mapToItem(block, 0, 0).y)] = true; });
            verify(Object.keys(lines).length >= 2, "the legend did not wrap");
            names.forEach(function (t) {
                verify(t.mapToItem(block, t.width, 0).x <= block.width, t.text + " runs past the card");
            });
            bubble.visible = false;
        }

        /// @return The breakdown's body, the item that holds the sentence, its tint layer and its legend.
        function breakdownOf(bubble) {
            return Util.findAll(bubble, function (o) {
                return o.roleOrder !== undefined && o.bubble !== undefined;
            })[0];
        }

        /// The sentence keeps its place from pending to ready: one tint box per role run, faded in over the
        /// plain line.
        function test_theRoleTintFadesInOverTheSentenceWithoutMovingIt() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show(breakdownPayload("pending", []));
            wait(50);
            const block = breakdownOf(bubble);
            verify(block, "the breakdown drew no body");
            const pendingHeight = block.height;
            const layer = Util.findAll(block, function (o) { return o.objectName === "tintLayer"; })[0];
            verify(layer, "the breakdown drew no tint layer");
            compare(layer.opacity, 0, "the tint showed before the answer");
            compare(Util.findAll(block, function (o) { return o.objectName === "tintBox"; }).length, 0,
                    "pending drew a tint box");

            bubble.show(breakdownPayload("ready", ["S", "V"]));
            tryCompare(layer, "opacity", 1, 1000, "the tint did not fade in at ready");
            compare(Util.findAll(block, function (o) { return o.objectName === "tintBox"; }).length, 2,
                    "the sentence's two role runs did not each get a box");
            compare(block.height, pendingHeight, "the sentence moved when the answer landed");
            bubble.visible = false;
        }

        /// @return Whether two colours match to within the rounding a grab leaves, opaque or not.
        function sameColour(actual, expected) {
            const a = Qt.color(actual);
            const e = Qt.color(expected);
            return Math.abs(a.r - e.r) < 0.02 && Math.abs(a.g - e.g) < 0.02 && Math.abs(a.b - e.b) < 0.02
                   && Math.abs(a.a - e.a) < 0.02;
        }

        /// A role run is painted across its words, the space between them included, and not on the text
        /// that carries no role. The probe points are picked from the glyph positions the sentence lays out.
        function test_theRoleRunsArePaintedOverTheirWordsAndNotTheRest() {
            const segments = [
                {text: "The rain", role: "S"}, {text: " ", role: ""}, {text: "stopped.", role: "V"},
                {text: " Nobody came.", role: ""}
            ];
            const sentenceText = segments.map(function (segment) { return segment.text; }).join("");
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            // Pending over the same sentence, so the glyph positions read below are the ready card's too.
            bubble.show({title: "", type: "sentence", breakdown: "pending", roles: [], countsDown: true,
                         verdictButtons: false, reveal: false,
                         segments: [{text: sentenceText, role: ""}], x: 200, y: 240});
            wait(400);
            const block = breakdownOf(bubble);
            verify(block, "the breakdown drew no body");
            const sentence = Util.findAll(block, function (o) { return o.objectName === "breakdownSentence"; })[0];
            verify(sentence, "the breakdown drew no sentence");

            // The surface is grabbed whole and the points are mapped into it: a grab of the block alone is not
            // laid out on the block's own origin.
            const surface = surfaceOf(bubble);
            const space = sentence.measureWidth(" ");
            const lineY = sentence.topPadding + sentence.lineBaseline - 4;
            const inS = sentence.mapToItem(surface, sentence.positions[4].x - space / 2, lineY);
            const inV = sentence.mapToItem(surface, sentence.positions[9].x - 1, lineY);
            const unroled = sentence.mapToItem(surface, sentence.positions[25].x - space / 2, lineY);

            // The grab's background is opaque, so an untinted point is judged against the tints, not against alpha.
            const tintS = block.tintOf(block.bubble.tokens.roleS);
            const tintV = block.tintOf(block.bubble.tokens.roleV);
            const at = function (point) {
                return testCase.grabImage(surface).pixel(Math.round(point.x), Math.round(point.y));
            };
            verify(!sameColour(at(inS), tintS), "the pending sentence was tinted");

            bubble.show({title: "", type: "sentence", breakdown: "ready", roles: ["S", "V"], countsDown: true,
                         verdictButtons: false, reveal: false, segments: segments, x: 200, y: 240});
            tryVerify(function () {
                return sameColour(at(inS), tintS);
            }, 1500, "the space inside the subject run is not the subject's tint");
            verify(sameColour(at(inV), tintV), "the verb run is not the verb's tint");
            verify(!sameColour(at(unroled), tintS) && !sameColour(at(unroled), tintV),
                   "a run with no role was tinted");
            bubble.visible = false;
        }

        /// A correction's payload as the duty sends it: the breakdown's state field, the changed words marked by
        /// role, and no legend.
        function correctionPayload(state, kind, segments, type, failure) {
            return {
                title: "", type: type || "sentence", breakdown: state, correction: kind, segments: segments, roles: [],
                failure: failure || "", countsDown: true, verdictButtons: false, reveal: false, x: 200, y: 240
            };
        }

        /// The changed word gets one rule under it and the unchanged words get none. The rule is checked by its
        /// geometry and by the pixels under it, since a rule that is laid out and never painted passes the first alone.
        function test_aCorrectionUnderlinesTheChangedWordAndNothingElse() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show(correctionPayload("ready", "grammar", [
                {text: "She ", role: ""}, {text: "recieve", role: "changed"}, {text: " home.", role: ""}
            ]));
            wait(50);
            const block = breakdownOf(bubble);
            verify(block, "the correction drew no body");
            const sentence = Util.findAll(block, function (o) { return o.objectName === "breakdownSentence"; })[0];
            verify(sentence, "the correction drew no sentence");

            const marks = Util.findAll(block, function (o) { return o.objectName === "underlineBox"; });
            compare(marks.length, 1, "the changed word did not get exactly one underline");
            compare(Util.findAll(block, function (o) { return o.objectName === "tintBox"; }).length, 0,
                    "a correction tinted a run");
            compare(marks[0].x, Math.round(sentence.positions[4].x), "the rule does not start at the changed word");
            compare(marks[0].width, Math.round(sentence.measureWidth("recieve")), "the rule is not as wide as the word");

            const surface = surfaceOf(bubble);
            const at = function (point) {
                return testCase.grabImage(surface).pixel(Math.round(point.x), Math.round(point.y));
            };
            // The rule is one pixel tall, so its top row is the row it paints. Read again on every try: the card
            // settles its height after the first frame, and the rule moves with it.
            const rowY = function () {
                return marks[0].mapToItem(surface, 0, 0).y;
            };
            const underChangedWord = function () {
                return {x: marks[0].mapToItem(surface, marks[0].width / 2, 0).x, y: rowY()};
            };
            // "o" in "home." is the 13th character: a letter with no descender, so the rule's row is clear of its ink.
            const underUnchangedWord = function () {
                return {x: sentence.mapToItem(surface, sentence.positions[13].x, 0).x, y: rowY()};
            };
            tryVerify(function () {
                return sameColour(at(underChangedWord()), bubble.tokens.warn);
            }, 1500, "the changed word has no warn-coloured rule under it");
            verify(!sameColour(at(underUnchangedWord()), bubble.tokens.warn), "an unchanged word has a rule under it");
            bubble.visible = false;
        }

        /// A correction's pending and failed lines say what the correction is doing.
        function test_aCorrectionSaysWhatItIsDoing() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const plain = [{text: "She recieve home.", role: ""}];
            bubble.show(correctionPayload("pending", "spell", plain, "word"));
            wait(50);
            verify(Util.textWith(Util.textsUnder(bubble), ["Correcting…"]), "pending did not say Correcting…");
            compare(Util.findAll(breakdownOf(bubble), function (o) { return o.objectName === "underlineBox"; }).length, 0,
                    "a pending correction drew a rule");

            bubble.show(correctionPayload("failed", "spell", plain, "word", "The answer could not be read."));
            wait(50);
            verify(Util.textWith(Util.textsUnder(bubble), ["Failed: The answer could not be read."]), "failed did not say so");
            bubble.visible = false;
        }

        /// A correction that has landed draws the sentence alone: its legend row and the gap above it go, while the
        /// pending card still holds them.
        function test_aCorrectionReadyTakesNoLegendRow() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const segments = [{text: "She ", role: ""}, {text: "goes", role: "changed"}, {text: " home.", role: ""}];
            bubble.show(correctionPayload("pending", "grammar", segments));
            wait(50);
            const block = breakdownOf(bubble);
            const pendingHeight = block.height;
            const legend = Util.findAll(block, function (o) { return o.objectName === "legendRow"; })[0];
            verify(legend, "the breakdown drew no legend row");
            verify(legend.visible, "the pending correction drew no legend row");

            bubble.show(correctionPayload("ready", "grammar", segments));
            tryCompare(legend, "visible", false, 1000, "the ready correction kept its legend row");
            tryCompare(block, "height", pendingHeight - 26, 1000, "the ready correction kept the legend row's space");
            bubble.visible = false;
        }

        /// The tag names the correction on the card: Spelling for a word, Grammar for a sentence, and never the
        /// word's New or Known.
        function test_theCorrectionTagNamesItsKind() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show(correctionPayload("ready", "spell", [{text: "recieve", role: "changed"}], "word"));
            wait(50);
            // The verdict pills are in the scene with their text even when hidden, so the tag is read by visible text.
            const shown = function (words) {
                return Util.textsUnder(bubble).filter(function (t) { return t.visible && words.indexOf(t.text) >= 0; })
                                              .map(function (t) { return t.text; });
            };
            compare(shown(["Spelling", "New", "Known"]), ["Spelling"], "the spelling correction was not tagged Spelling alone");

            bubble.show(correctionPayload("ready", "grammar", [{text: "She goes home.", role: ""}]));
            wait(50);
            compare(shown(["Grammar", "Spelling", "New", "Known"]), ["Grammar"], "the grammar correction was not tagged Grammar alone");
            bubble.visible = false;
        }

    }
}
