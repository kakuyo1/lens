import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Every bar form's drawer and labels fit their items and the window, in every interface language.
 *
 * Split out of tst_selectionbar.qml, which would pass the 32768-character cap on a .qml file (QML.md section 1). The
 * measurement opens each form on a single-token word and a sentence and reads what it draws, so the layout checked is
 * the one a reader gets.
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
        name: "SelectionBarFit"
        when: windowShown

        /// The labels each interface language gives the drawer's and the bar's items, as the catalogs translate them
        /// (i18n/lens_*.ts). The measurement reads them so the layout is the one a reader in that language gets.
        function labelsOf(language) {
            const catalogs = {
                en: {translate: "Translate", explain: "Explain", copy: "Copy text",
                     spell: "Fix spelling", grammar: "Fix syntax", breakdown: "Breakdown"},
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

        /// @return An item's box as its layout places it in @p frame, leaving out the transforms an animation puts on it: a
        ///         card fanned out or lifted is still laid out at its slot.
        function layoutBoxIn(item, frame) {
            let x = 0;
            let y = 0;
            for (let o = item; o && o !== frame; o = o.parent) {
                x += o.x;
                y += o.y;
            }
            return {x: x, y: y, w: item.width, h: item.height};
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
            // CJK Symbols and Kana, CJK ideographs, Hangul syllables, CJK compatibility ideographs, fullwidth forms.
            const cjk = /[\u3000-\u9fff\uac00-\ud7af\uf900-\ufaff\uff00-\uffef]/;
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
                // The fan-out and the unfold run for the bubble's 220 ms after the fade: measure the settled pose.
                wait(300);

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
                let poseSpill = 0;
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
                    actSpill = Math.max(actSpill, spillOf(layoutBoxIn(act, frame), drawerBox));
                    poseSpill = Math.max(poseSpill, spillOf(actBox, window));
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
                if (poseSpill > 0.5)
                    problems.push("an item's drawn pose spills " + poseSpill.toFixed(1) + " px out of the window");
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
                             "actSpill=" + actSpill.toFixed(1), "poseSpill=" + poseSpill.toFixed(1), "lines=" + lines, "splitWords=" + splitWords,
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
