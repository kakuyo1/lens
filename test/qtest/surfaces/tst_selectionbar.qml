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

    TextMetrics {
        id: metrics
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

            Controller.setSetting("selectionBarStyle", "fan");
            bar.open();
            compare(bar.entranceFactor, 1, "a different shape's first entrance was halved");

            bar.dismiss();
            bar.open();
            compare(bar.entranceFactor, 0.5, "the second fan entrance was not halved");
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
            for (const style of ["bar", "tab", "hand", "tag", "fan"]) {
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

        function test_theFanRibsReadAtRest() {
            const bar = openedWithStyle("fan");
            wait(800);
            const tokens = bar.tokens;
            // The fill paths carry a transparent stroke at the default width; only the drawn
            // outline and the two ribs have ink in the stroke.
            const paths = Util.findAll(bar, function (o) {
                return o.strokeColor !== undefined && o.strokeWidth === 1 && o.strokeColor.a > 0;
            });
            const ribs = paths.filter(function (path) {
                return String(path.strokeColor) === String(tokens.muted);
            });

            compare(paths.length, 3, "the fan should draw an outline and two ribs");
            compare(ribs.length, 2, "the two ribs should read before hover");
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
                return o.Accessible !== undefined && o.Accessible.name === "Sentence breakdown";
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
                return o.Accessible !== undefined && o.Accessible.name === "Spell correction";
            });
            compare(named.length, 1, "the spelling correction has no name for the screen reader");
            compare(named[0].Accessible.role, Accessible.Button);
            bar.visible = false;
        }

        /// The labels each interface language gives the drawer's and the bar's items, as the catalogs translate them
        /// (i18n/lens_*.ts). The measurement reads them so the layout is the one a reader in that language gets.
        function labelsOf(language) {
            const catalogs = {
                en: {translate: "Translate", explain: "Explain", copy: "Copy text",
                     spell: "Spell correction", grammar: "Grammar correction", breakdown: "Sentence breakdown"},
                zh: {translate: "翻译", explain: "解释", copy: "复制文本",
                     spell: "纠正拼写", grammar: "纠正语法", breakdown: "句子拆解"},
                ja: {translate: "翻訳", explain: "解説", copy: "テキストをコピー",
                     spell: "スペルを直す", grammar: "文法を直す", breakdown: "文の分解"},
                es: {translate: "Traducir", explain: "Explicar", copy: "Copiar texto",
                     spell: "Corregir ortografía", grammar: "Corregir gramática", breakdown: "Análisis sintáctico"}
            };
            return catalogs[language];
        }

        /// @return The box @p item covers in @p frame's coordinates, over its four corners, so a rotated item is
        ///         measured by what it covers and not by its unrotated frame.
        function boxIn(item, frame) {
            const points = [item.mapToItem(frame, 0, 0), item.mapToItem(frame, item.width, 0),
                            item.mapToItem(frame, 0, item.height), item.mapToItem(frame, item.width, item.height)];
            const xs = points.map(function (p) { return p.x; });
            const ys = points.map(function (p) { return p.y; });
            const x = Math.min.apply(null, xs);
            const y = Math.min.apply(null, ys);
            return {x: x, y: y, w: Math.max.apply(null, xs) - x, h: Math.max.apply(null, ys) - y};
        }

        /// @return How far @p inner pokes out of @p outer, on the worst side, in px; zero when it lies inside.
        function spillOf(inner, outer) {
            return Math.max(0, outer.x - inner.x, inner.x + inner.w - (outer.x + outer.w),
                            outer.y - inner.y, inner.y + inner.h - (outer.y + outer.h));
        }

        /// @return Whether @p item is drawn: it and every ancestor up to @p frame are visible.
        function shownIn(item, frame) {
            for (let o = item; o && o !== frame; o = o.parent)
                if (!o.visible)
                    return false;
            return true;
        }

        /// @return Whether the first line of a wrapped label ends where Qt may break it: after a space, or beside a
        ///         character of a script with no spaces (Japanese, Chinese, Korean). A line cut inside a Latin word
        ///         is a split word, the defect this checks for. Japanese may break between any two characters, so
        ///         a Japanese split is not caught here; it is judged on the PNG.
        function breaksAreLegal(label, width) {
            const cjk = /[　-鿿가-힯豈-﫿＀-￯]/;
            const text = label.text;
            // Measured with the font the label draws with. Each line is the longest run that fits, backed up to
            // the last point Qt may break at; every line after the first is checked the same way.
            metrics.font = label.font;
            let start = 0;
            while (start < text.length) {
                let fit = start;
                for (let n = start + 1; n <= text.length; n++) {
                    metrics.text = text.slice(start, n);
                    if (metrics.advanceWidth > width)
                        break;
                    fit = n;
                }
                if (fit >= text.length)
                    return true;
                let p = fit;
                while (p > start && !(text.charAt(p - 1) === " " || text.charAt(p) === " "
                                      || cjk.test(text.charAt(p - 1)) || cjk.test(text.charAt(p))))
                    p -= 1;
                if (p <= start)
                    return false;
                start = p;
            }
            return true;
        }

        /// Opens the bar in @p style on @p scenario, with the labels of @p language, and measures what it draws.
        /// The labels are swapped on the live body, so the layout is the one a reader in that language gets.
        /// @return The measurement, and `problems`: what does not fit, empty when all of it does.
        function measureCombo(style, scenario, language, dir) {
            Controller.setSetting("selectionBarStyle", style);
            const bar = barComponent.createObject(root);
            try {
                verify(bar, "the bar did not open");
                bar.selectionText = scenario.text;
                bar.selectionKind = scenario.kind;
                bar.spellable = scenario.spellable;
                bar.grammarable = scenario.grammarable;
                bar.breakdownable = scenario.breakdownable;

                const labels = labelsOf(language);
                const body = bar.bodyItem;
                body.actions = [
                    {action: "translate", label: labels.translate, source: "qrc:/icons/ui-translate.svg"},
                    {action: "explain", label: labels.explain, source: "qrc:/icons/ui-explain.svg"},
                    {action: "copy", label: labels.copy, source: "qrc:/icons/ui-copy.svg"}
                ];
                body.drawerItems = bar.drawerItems.map(function (item) {
                    return {action: item.action, label: labels[item.action], source: item.source};
                });
                // The order a reader gets: placed, then opened, and only then the entrance runs (ADR 0109). The window
                // is sized for the drawer where the placement finds it, so measuring after a wait that skipped the
                // placement would test a state no reader reaches.
                bar.placeAt(Qt.point(400, 300), {left: 0, top: 0, right: 1000, bottom: 800}, 8);
                bar.open();
                wait(900);

                const drawers = Util.findAll(bar, function (o) { return o.expand !== undefined && o.applies; });
                verify(drawers.length === 1, "the bar drew " + drawers.length + " active drawers");
                const drawer = drawers[0];
                drawer.open = true;
                tryCompare(drawer, "opacity", 1, 1000);
                wait(50);

                const frame = bar.contentItem;
                const window = {x: 0, y: 0, w: bar.width, h: bar.height};
                const drawerBox = boxIn(drawer, frame);
                const acts = Util.findAll(drawer, function (o) {
                    return o.modelData !== undefined && o.Accessible !== undefined && o.Accessible.name !== "";
                });

                // The copy item holds its own drawer, so an item's labels are read only when they are not in one.
                const outsideDrawer = function (o) {
                    for (let p = o; p && p !== frame; p = p.parent)
                        if (p.expand !== undefined)
                            return false;
                    return true;
                };
                const bodyActs = Util.findAll(body, function (o) {
                    return o.modelData !== undefined && o.Accessible !== undefined && o.Accessible.name !== ""
                           && acts.indexOf(o) < 0 && outsideDrawer(o);
                });

                let labelSpill = 0;
                let actSpill = 0;
                let splitWords = 0;
                let lines = 1;
                const check = function (actBox, container, text) {
                    labelSpill = Math.max(labelSpill, spillOf(boxIn(text, frame), actBox));
                    if (text.lineCount > 1 && !breaksAreLegal(text, text.width))
                        splitWords += 1;
                    lines = Math.max(lines, text.lineCount);
                };
                for (const act of acts) {
                    const actBox = boxIn(act, frame);
                    actSpill = Math.max(actSpill, spillOf(actBox, drawerBox));
                    for (const text of Util.textsUnder(act))
                        if (shownIn(text, frame))
                            check(actBox, act, text);
                }
                // A hand's cards are tilted by design (UI-SPEC.md 4.10), so they overhang the body's own frame: the
                // body is judged by what it draws, against the window.
                let bodyDrawn = 0;
                for (const act of bodyActs) {
                    const actBox = boxIn(act, frame);
                    bodyDrawn = Math.max(bodyDrawn, spillOf(actBox, window));
                    for (const text of Util.textsUnder(act))
                        if (shownIn(text, frame) && outsideDrawer(text))
                            check(actBox, act, text);
                }
                const drawerClip = spillOf(drawerBox, window);
                const bodyClip = bodyDrawn;

                const problems = [];
                if (acts.length !== body.drawerItems.length)
                    problems.push("drawer shows " + acts.length + " of " + body.drawerItems.length + " items");
                if (labelSpill > 0.5)
                    problems.push("a label spills " + labelSpill.toFixed(1) + " px out of its item");
                if (actSpill > 0.5)
                    problems.push("an item spills " + actSpill.toFixed(1) + " px out of its drawer");
                if (drawerClip > 0.5)
                    problems.push("the drawer spills " + drawerClip.toFixed(1) + " px out of the window");
                if (bodyClip > 0.5)
                    problems.push("the bar spills " + bodyClip.toFixed(1) + " px out of the window");
                if (splitWords > 0)
                    problems.push(splitWords + " wrapped label(s) break inside a word");

                if (dir)
                    Util.saveSnapshot(testCase, frame, dir, style + "-" + scenario.name + "-" + language);
                const row = [style, scenario.name, language, "window=" + window.w + "x" + window.h,
                             "items=" + acts.length + "/" + body.drawerItems.length, "labelSpill=" + labelSpill.toFixed(1),
                             "actSpill=" + actSpill.toFixed(1), "lines=" + lines, "splitWords=" + splitWords,
                             "drawerClip=" + drawerClip.toFixed(1), "bodyClip=" + bodyClip.toFixed(1)].join(" ");
                console.log("TABLE " + row);
                return {problems: problems, row: row};
            } finally {
                bar.destroy();
            }
        }

        /// Every form draws each drawer item, with each label inside its item, each item inside the drawer, and the
        /// drawer and the bar inside the window, for a single-token word and a 15-word sentence in every interface
        /// language (UI-SPEC.md 4.10). The table is printed per combination; a failure names what spills.
        function test_theDrawersFitTheirLabelsAndTheWindowInEveryForm() {
            const dir = lensQaSnapshotDir;
            const scenarios = [
                {name: "word", text: "recieve", kind: "word", spellable: true, grammarable: false, breakdownable: false},
                {name: "sentence", text: "She said that the meeting had been moved to next Thursday because of the storm.",
                 kind: "sentence", spellable: false, grammarable: true, breakdownable: true}
            ];
            const failing = [];
            let total = 0;
            try {
                for (const style of ["bar", "tab", "hand", "tag", "fan"]) {
                    for (const scenario of scenarios) {
                        for (const language of ["en", "zh", "ja", "es"]) {
                            const measured = measureCombo(style, scenario, language, dir);
                            total += 1;
                            if (measured.problems.length > 0)
                                failing.push(measured.row + " :: " + measured.problems.join("; "));
                        }
                    }
                }
            } finally {
                Controller.setSetting("selectionBarStyle", "bar");
            }
            verify(failing.length === 0, failing.length + " of " + total + " combinations do not fit:\n" + failing.join("\n"));
        }
    }
}
