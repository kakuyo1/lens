import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The action bar's press, which is the case the components target had to leave behind.
 *
 * Pressing one of the items also arms the card's DragHandler, whose grab reads
 * Controller.cursorPos(); with no Controller behind it that read trips an assert in the
 * singleton's factory and takes the process down, so only this target can check the press. The
 * bug pinned here: the item read `pressed` off a HoverHandler, which has no such property, so
 * the press never shrank it and nothing but an eye could tell.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: barComponent
        SelectionBar { selectionText: "ubiquitous" }
    }

    TestCase {
        id: testCase
        name: "SelectionBar"
        when: windowShown

        function opened() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            // Through open(), not `visible = true`: the content is disabled while the bar is not
            // meant to be up, so a direct show would draw it without taking the press.
            bar.open();
            wait(50);
            return bar;
        }

        /// @return The bar's three items, in the order they are drawn.
        function itemsOf(bar) {
            return Util.textsUnder(bar).map(function (t) { return t.parent.parent; });
        }

        /// A tag placed with its drawer out flips its string side while the bar is reading the room the drawer needs.
/// The re-measure that flip asks for must land after the move, not inside the read (a queued tick, not a direct
/// one: the direct write read back as the bar's `reach` binding depending on itself and Qt warned on every
/// arrival). This case walks that path and reads the room the window kept.
        function test_aTagWithADrawerKeepsRoomForItWhenItFlips() {
            Controller.setSetting("selectionBarStyle", "tag");
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionText = "a sentence long enough to be a breakdownable English sentence with several words";
            bar.selectionKind = "sentence";
            bar.breakdownable = true;
            bar.grammarable = true;
            bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);

            const drawer = drawerOf(bar);
            verify(drawer, "the tag drew no copy drawer");
            verify(drawer.applies, "the drawer does not apply to the sentence");
            verify(bar.drawerSide !== "", "the drawer was given no side to open from");
            verify(bar.reach.top + bar.reach.right + bar.reach.bottom + bar.reach.left > 0,
                   "the window kept no room for the drawer");

            bar.open();
            wait(100);
            bar.dismiss();
            wait(100);
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_holdingAnItemShrinksIt() {
            const bar = opened();
            const item = itemsOf(bar)[0];

            compare(item.scale, 1.0, "an item should sit at its full size until it is pressed");

            mousePress(item, item.width / 2, item.height / 2, Qt.LeftButton);
            // The scale animates over 150 ms, so the press is a state to wait for rather than
            // a value to read.
            tryVerify(function () { return item.scale < 0.98; }, 1000);
            mouseRelease(item, item.width / 2, item.height / 2, Qt.LeftButton);

            tryVerify(function () { return item.scale > 0.99; }, 1000);
        }

        /// With nothing selected there is no action to take, so the controller declines and the
        /// clipboard is untouched -- which is why this case can tap an item at all.
        function test_tappingAnItemTakesTheBarDown() {
            const bar = opened();

            const item = itemsOf(bar)[2];
            mouseClick(item, item.width / 2, item.height / 2);

            // The tap starts the fade; the window goes when the fade has run (SurfaceFade.qml).
            tryCompare(bar, "visible", false, 1000, "the bar stayed up after one of its items was tapped");
        }

        /// @return The bar, up in @p style. The stored style is left as the test found it only
        ///         when the test runs to its end, so these cases stay last in the file.
        function openedWithStyle(style) {
            Controller.setSetting("selectionBarStyle", style);
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            bar.open();
            wait(50);
            return bar;
        }

        /// The first arrival of a shape teaches it; the next one in the same run is half the time
        /// (ADR 0109). The factor is set before the body starts, so the animation reads one number.
        function test_aShapeEntersInFullOnceThenInHalf() {
            const bar = openedWithStyle("tag");
            compare(bar.entranceFactor, 1, "the first tag entrance was already shortened");

            bar.dismiss();
            bar.open();
            compare(bar.entranceFactor, 0.5, "the second tag entrance was not halved");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_theTabItemShiftsAwayFromItsSpine() {
            const bar = openedWithStyle("tab");
            const body = bar.bodyItem;
            verify(body);
            // The staggered entrance slides the items along the same x; it has to have settled
            // before the shift's direction can be read on its own.
            wait(500);

            const item = itemsOf(bar)[0];
            bar.sideLeft = false;
            body.playPress("translate");
            tryVerify(function () { return item.transform[0].x > 5; }, 1000,
                      "a press on the right-side tab should send the item outward, to the right");
            wait(250);

            bar.sideLeft = true;
            body.playPress("translate");
            tryVerify(function () { return item.transform[0].x < -5; }, 1000,
                      "with the tab flipped to the left the same press should lift the other way");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// The window takes no focus, so the actions cannot be Tabbed to; the screen reader still
        /// has to find all three in every form. The names are the action labels themselves.
        function test_everyFormNamesItsThreeActions() {
            for (const style of ["bar", "tab", "hand", "tag"]) {
                const bar = openedWithStyle(style);
                wait(500);
                const names = Util.findAll(bar, function (o) {
                    // Only Items carry the attached property; asking a ShapePath or a Timer for
                    // it prints a warning rather than answering.
                    return o.hasOwnProperty("activeFocusOnTab")
                           && o.Accessible !== undefined && o.Accessible.name !== "";
                }).map(function (o) { return o.Accessible.name; });
                for (const label of ["Translate", "Explain", "Copy text"]) {
                    verify(names.indexOf(label) >= 0, style + " has no accessible '" + label + "' action");
                }
                bar.visible = false;
            }
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// Bar, tab and tag draw each drawer item with its icon and label on one line, the icon to the left of the
        /// label and centred on it; the hand keeps its labels under the icon.
        function test_theDrawerItemsKeepTheirIconAndLabelOnOneLine() {
            for (const style of ["bar", "tab", "tag"]) {
                Controller.setSetting("selectionBarStyle", style);
                const bar = createTemporaryObject(barComponent, root);
                verify(bar);
                bar.selectionKind = "sentence";
                bar.breakdownable = true;
                bar.grammarable = true;
                bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);
                bar.open();
                wait(500);
                const items = Util.findAll(bar, function (o) {
                    return o.Accessible !== undefined
                           && ["Fix syntax", "Breakdown"].indexOf(o.Accessible.name) >= 0;
                });
                verify(items.length === 2, style + " drew " + items.length + " drawer items, not two");
                for (const item of items) {
                    const icon = Util.findAll(item, function (o) { return o.objectName === "drawerIcon"; })[0];
                    const text = Util.findAll(item, function (o) { return o.text === item.Accessible.name; })[0];
                    verify(icon && text, style + " '" + item.Accessible.name + "' has no icon or no label");
                    const iconCentre = icon.mapToItem(item, icon.width / 2, icon.height / 2);
                    const textCentre = text.mapToItem(item, 0, text.height / 2);
                    verify(Math.abs(iconCentre.y - textCentre.y) <= 1,
                           style + " '" + item.Accessible.name + "' puts its icon and label on different lines");
                    verify(iconCentre.x < textCentre.x, style + " '" + item.Accessible.name + "' has its icon right of its label");
                }
                bar.visible = false;
            }
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// The copy drawer anywhere in the bar. The hand's drawer stands beside the copy item, not inside it.
        function drawerIn(bar) {
            return Util.findAll(bar, function (o) {
                return o.expand !== undefined && o.form !== undefined;
            })[0];
        }

        /// A sentence's bar in one form, placed so its drawer opens to the side the cases below read.
        function sentenceBarIn(style) {
            Controller.setSetting("selectionBarStyle", style);
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "sentence";
            bar.breakdownable = true;
            bar.grammarable = true;
            bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);
            bar.open();
            wait(50);
            return bar;
        }

        /// Under a glass theme the hand's cards are glass, as the bar's own are: each card carries a
        /// GlassSurface (UI-SPEC.md 4.10).
        function test_theHandDrawerIsGlassUnderAGlassTheme() {
            const saved = Tokens.theme;
            Tokens.theme = "frosted";

            const hand = sentenceBarIn("hand");
            const handDrawer = drawerIn(hand);
            handDrawer.open = true;
            wait(300);
            const cardGlass = Util.findAll(handDrawer, function (o) { return o.objectName === "drawerCardGlass"; });
            compare(cardGlass.length, 2, "the hand's drawer cards are not glass");
            for (const glass of cardGlass)
                verify(glass.visible, "a hand's drawer card draws no glass");
            hand.visible = false;

            Tokens.theme = saved;
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// The hand's drawer cards fold toward the copy card when the drawer is shut, fan out when it opens, and the card
        /// under the pointer straightens and lifts, as the bar's own hand does (UI-SPEC.md 4.10).
        function test_theHandDrawerCardsFanOutAndLiftUnderThePointer() {
            const bar = sentenceBarIn("hand");
            const drawer = drawerIn(bar);
            const card = function (name) {
                return Util.findAll(bar, function (o) {
                    return o.Accessible !== undefined && o.Accessible.name === name && o.turn !== undefined;
                })[0];
            };
            const grammar = card("Fix syntax");
            const breakdown = card("Breakdown");
            verify(grammar && breakdown, "the hand drew no drawer cards");

            compare(grammar.drop, 12, "a shut card does not sit low");
            compare(grammar.grow, 0.9, "a shut card is not folded small");
            drawer.open = true;
            tryCompare(grammar, "turn", -6, 1000, "the first card did not fan out to -6 degrees");
            tryCompare(breakdown, "turn", 6, 1000, "the second card did not fan out to 6 degrees");

            mouseMove(breakdown, breakdown.width / 2, breakdown.height / 2);
            tryCompare(breakdown, "drop", -6, 1000, "the card under the pointer did not lift");
            tryCompare(breakdown, "grow", 1.035, 1000, "the card under the pointer did not grow");
            compare(breakdown.turn, 0, "the card under the pointer did not straighten");
            bar.visible = false;
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// The tag's press is the swing of the whole card, not a per-item shift and fade; the
        /// state that names the pressed action clears with the swing instead of waiting for the
        /// next show (UI-SPEC 4.10).
        function test_theTagAnswersAPressWithItsSwingAlone() {
            const bar = openedWithStyle("tag");
            const body = bar.bodyItem;
            verify(body);
            wait(800);
            const item = itemsOf(bar)[0];

            compare(item.opacity, 1.0, "the tag item should not fade on press");
            compare(item.transform[0].x, 0.0, "the tag item should not shift on press");

            body.playPress("translate");
            compare(body.flashing, "translate", "the pressed action was not recorded");
            tryVerify(function () { return body.flashing === ""; }, 1000,
                      "the tag's press state outlived its swing");
            compare(item.opacity, 1.0, "the tag item faded as well as swinging");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// §4.10's "2 px" is how much higher the middle card sits, not its offset from the
        /// hand's top edge; the two sides are equal and the difference is exactly 2.
        function test_theHandLiftsItsMiddleCardTwoPixels() {
            const bar = openedWithStyle("hand");
            const body = bar.bodyItem;
            verify(body);
            compare(body.cardY[0], body.cardY[2], "the two outer cards should sit at one height");
            compare(body.cardY[0] - body.cardY[1], 2,
                    "the middle card should sit 2 px above its neighbours");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_theTabSpineAndFoldReadAtRest() {
            const bar = openedWithStyle("tab");
            wait(500);
            const tokens = bar.tokens;

            const spine = Util.findAll(bar, function (o) {
                return o.width === 1 && o.height > 40 && o.color !== undefined;
            })[0];
            verify(spine, "the tab has no spine");
            compare(spine.color, tokens.muted, "the spine should read before hover");

            const fold = Util.findAll(bar, function (o) {
                return o.strokeColor !== undefined && o.strokeWidth === 1 && o.strokeColor.a > 0;
            })[0];
            verify(fold, "the tab has no fold");
            compare(fold.strokeColor, tokens.line, "the fold should carry its own edge");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        function test_thePressedCardTossesUpAndFades() {
            const bar = openedWithStyle("hand");
            // The hand's label sits a level deeper than the rows do -- inside the card's face --
            // so the tossed card is reached by the property only its slot carries.
            const card = Util.findAll(bar, function (o) { return o.pressFade !== undefined; })[0];
            verify(card, "the hand dealt no card");
            // The dealing entrance ends in the card at rest, fully opaque.
            wait(600);

            compare(card.opacity, 1.0);
            bar.bodyItem.playPress("translate");
            tryVerify(function () { return card.opacity < 0.9 && card.transform[0].y < 0; }, 1000,
                      "the pressed card should lift off the hand and fade as it is tossed");
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// @return The copy text's item, the one the screen reader names by its label.
        function copyOf(bar) {
            return Util.findAll(bar, function (o) {
                return o.Accessible !== undefined && o.Accessible.name === "Copy text";
            })[0];
        }

        /// @return The copy drawer, which every form draws inside the copy text's own item.
        function drawerOf(bar) {
            return Util.findAll(copyOf(bar), function (o) {
                return o.expand !== undefined;
            })[0];
        }

        /// The drawer offers the breakdown only to a sentence the breakdown can take (UI-SPEC.md 4.10).
        function test_theCopyDrawerOffersTheBreakdownOnlyForABreakdownableSentence() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            bar.breakdownable = true;
            compare(bar.drawerItems.length, 0, "a word offered the copy drawer");

            bar.selectionKind = "sentence";
            bar.breakdownable = false;
            compare(bar.drawerItems.length, 0, "a sentence the breakdown cannot take offered it");

            bar.breakdownable = true;
            compare(bar.drawerItems.length, 1);
            compare(bar.drawerItems[0].action, "breakdown");
        }

        /// The drawer opens after the pointer rests on the copy text and closes once the pointer has left it
        /// (UI-SPEC.md 4.10). The placement is the real one, so the drawer's side is the one a reader gets.
        function test_theCopyDrawerOpensOnHoverAndClosesWhenThePointerLeaves() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "sentence";
            bar.breakdownable = true;
            bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);
            bar.open();
            wait(50);

            const drawer = drawerOf(bar);
            verify(drawer, "the bar draws no copy drawer");
            verify(drawer.applies, "the drawer does not apply to the sentence");
            compare(drawer.open, false, "the drawer was out before any hover");

            const copy = copyOf(bar);
            verify(copy, "the bar draws no copy text");
            mouseMove(copy, copy.width / 2, copy.height / 2);
            tryCompare(drawer, "open", true, 1000, "the drawer did not open under the pointer");

            mouseMove(bar.contentItem, 1, 1);
            tryCompare(drawer, "open", false, 1000, "the drawer stayed out after the pointer left");
            bar.visible = false;
        }

        /// The drawer's item is a named button for the screen reader, and a word has no drawer to speak of.
        function test_theCopyDrawerItemIsANamedButtonAndAWordHasNoDrawer() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "sentence";
            bar.breakdownable = true;
            bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);
            bar.open();
            wait(50);

            const named = Util.findAll(bar, function (o) {
                return o.Accessible !== undefined && o.Accessible.name === "Breakdown";
            });
            compare(named.length, 1, "the drawer item has no name for the screen reader");
            compare(named[0].Accessible.role, Accessible.Button);

            bar.selectionKind = "word";
            bar.breakdownable = false;
            wait(50);
            compare(drawerOf(bar).applies, false, "the drawer still applies to a word");
            compare(drawerOf(bar).visible, false, "a word drew the copy drawer");
            bar.visible = false;
        }

        /// The spelling correction is offered for one token and the grammar correction for a sentence, and
        /// neither is offered for the other's selection (UI-SPEC.md 4.10, docs/adr/0129).
        function test_theCopyDrawerOffersSpellingForATokenAndGrammarForASentence() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            bar.spellable = true;
            compare(bar.drawerItems.map(item => item.action), ["spell"], "a single token did not offer the spelling correction");
            bar.spellable = false;
            compare(bar.drawerItems.length, 0, "a token the spelling correction cannot take still offered a drawer");

            bar.selectionKind = "sentence";
            bar.grammarable = true;
            compare(bar.drawerItems.map(item => item.action), ["grammar"], "a sentence did not offer the grammar correction");
            bar.grammarable = false;
            compare(bar.drawerItems.length, 0, "a sentence the grammar correction cannot take still offered a drawer");
        }

        /// The corrections come ahead of the breakdown, in the order the spec lists them.
        function test_theCopyDrawerListsTheCorrectionsBeforeTheBreakdown() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "sentence";
            bar.grammarable = true;
            bar.breakdownable = true;
            compare(bar.drawerItems.map(item => item.action), ["grammar", "breakdown"]);
        }

        /// The spelling correction's item is a named button for the screen reader, drawn once the drawer applies.
        function test_theSpellingCorrectionIsANamedButton() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "word";
            bar.spellable = true;
            bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);
            bar.open();
            wait(50);

            const named = Util.findAll(bar, function (o) {
                return o.Accessible !== undefined && o.Accessible.name === "Fix spelling";
            });
            compare(named.length, 1, "the spelling correction has no name for the screen reader");
            compare(named[0].Accessible.role, Accessible.Button);
            bar.visible = false;
        }
    }
}
