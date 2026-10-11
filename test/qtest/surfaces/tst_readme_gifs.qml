import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The four frame sequences the README's GIFs are cut from: the explanation bubble, the selection
 * bar, marking a word known or new, and the year table.
 *
 * Each case steps through one interaction and grabs the surface at every step, so the frames can be
 * encoded into an animation. Input is in-scene only: QTest's mouseMove and mouseClick deliver events
 * to the window, and the real pointer, keyboard and clipboard are never touched. The only writes are
 * to the settings copy setup.cpp made for this run, and the one model-facing path (reviewWord) is a
 * cache hit from the checked-in fixture, so no request leaves the process.
 *
 * Every element a clip will act on is looked up before its first frame. A lookup between frames
 * would stall the timeline, and the stall would show in the GIF as a held frame.
 *
 * Frames are named with their index and the milliseconds since the clip started. The encoder reads
 * the time from the name and holds each frame for the time it was on screen, not for a fixed guess.
 * Skipped unless LENS_QA_SNAPSHOT_DIR is set; the directory must already exist.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    Component {
        id: bubbleComponent
        Bubble {}
    }

    Component {
        id: yearWordsComponent
        YearWordsPopup {}
    }

    TestCase {
        id: testCase
        name: "ReadmeGifs"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        function panel(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        /// @return A sink that grabs the item it is handed and names the frame by index and by
        ///         the milliseconds since this sink was made.
        function sinkFor(prefix) {
            const started = Date.now();
            let index = 0;
            return function (item) {
                const at = Date.now() - started;
                const name = prefix + "-" + String(index).padStart(4, "0") + "-" + String(at).padStart(6, "0");
                index += 1;
                verify(Util.saveSnapshot(testCase, item, lensQaSnapshotDir, name));
            };
        }

        /// @return The first item under @p item, itself included, that @p predicate accepts, walking
        ///         the visual tree. Util.findAll walks data, which does not hold Repeater delegates.
        function visualFind(item, predicate) {
            if (predicate(item))
                return item;
            for (let i = 0; i < item.children.length; ++i) {
                const found = visualFind(item.children[i], predicate);
                if (found)
                    return found;
            }
            return null;
        }

        /// Grab @p item repeatedly for about @p ms, one frame per pass through the event loop.
        function sample(sink, item, ms) {
            const end = Date.now() + ms;
            do {
                sink(item);
                wait(30);
            } while (Date.now() < end);
        }

        /// The page colour the README's stills sit on. A Window grabs over its own clear colour, which
        /// is opaque black by default and would frame every surface in black; the page colour is what
        /// a reader would see around it.
        readonly property string pageColor: "#e9ebef"

        /// Record in the light theme, as the README's stills are, and return the theme to put back.
        /// The fixture is dark, so each clip sets light and the case restores what it found.
        function recordLight() {
            const was = Tokens.theme;
            Tokens.theme = "light";
            return was;
        }

        /// @return The Text an action shows, found by its wording.
        function textNamed(texts, label) {
            const text = Util.textWith(texts, [label]);
            verify(text, "no text reading " + label);
            return text;
        }

        /// Clip (a): a word's explanation bubble arriving, with its English definition and the
        /// countdown stepping down. The bubble is handed the payload the controller sends for an
        /// English explanation, the way tst_bubble does, because the fixture's only cached entry is
        /// in Chinese and the controller path would draw the Chinese row alone.
        function test_bubbleClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            const was = recordLight();
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.color = pageColor;
            bubble.show({
                title: "panel",
                type: "word",
                ipa: "/pænl/",
                en: "a flat board forming part of a surface",
                status: "new",
                x: 200,
                y: 200
            });
            tryCompare(bubble, "visible", true);

            sample(sinkFor("bubble"), bubble.contentItem, 3200);
            Tokens.theme = was;
        }

        /// Clip (b): the selection bar arriving, then the pointer moving across its three actions.
        /// Hover only: a click would run translate, explain or copy, which is a model call or a
        /// write to the real clipboard.
        function test_selectionBarClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            const main = make();
            // After make(): Main binds Tokens.theme to the settings when it is built.
            const was = recordLight();
            const bar = panel(main, "SelectionBar");
            bar.color = pageColor;
            const sink = sinkFor("selection");

            Controller.selectionBarRequested({ x: 300, y: 300, kind: "word", text: "serendipity", letterCount: 11 });
            tryCompare(bar, "visible", true);

            const texts = Util.textsUnder(bar);
            const targets = ["Translate", "Explain", "Copy text"].map(function (label) { return textNamed(texts, label); });

            sample(sink, bar.contentItem, 900);
            for (let i = 0; i < targets.length; ++i) {
                mouseMove(targets[i], targets[i].width / 2, targets[i].height / 2);
                sample(sink, bar.contentItem, 700);
            }
            Tokens.theme = was;
        }

        /// Clip (c): the pointer reveals the Known and New row, Known is pressed, and the verdict
        /// lands on the bubble. The word is put back to new afterwards so the run leaves the store
        /// as it found it.
        function test_markingClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            const main = make();
            const was = recordLight();
            Controller.explanationDuty.dismissBubble();
            const bubble = panel(main, "Bubble");
            bubble.color = pageColor;

            Controller.reviewWord("panel", 360, 240);
            tryCompare(bubble, "visible", true);
            compare(bubble.status, "new", "the fixture should leave panel unmarked");

            const texts = Util.textsUnder(bubble);
            const card = textNamed(texts, "panel");
            const known = textNamed(texts, "Known");

            const sink = sinkFor("marking");
            sample(sink, bubble.contentItem, 800);

            mouseMove(card, card.width / 2, card.height / 2);
            sample(sink, bubble.contentItem, 1000);

            mouseClick(known, known.width / 2, known.height / 2);
            sample(sink, bubble.contentItem, 1500);
            compare(bubble.status, "known", "pressing Known did not mark the word");

            Controller.explanationDuty.mark("panel", false);
            Tokens.theme = was;
        }

        /// Clip (d): the year table, a day picked, the colour palette opened, a hue chosen, and the
        /// palette closed. Every cell is found before the first frame.
        function test_yearTableClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            const was = recordLight();
            const popup = createTemporaryObject(yearWordsComponent, root);
            verify(popup);
            wait(50);
            popup.color = pageColor;
            popup.visible = true;
            wait(200);

            // The day cell and the legend swatch are always built, so they are looked up before the
            // first frame. The palette is a Loader that exists only while it is open, so its parts
            // are looked up once the swatch has opened it, by the visual tree rather than by data.
            const day = Controller.yearDays.find(function (d) { return d.pops > 0; });
            verify(day, "the fixture has no day with lookups");
            const cell = Util.findAll(popup, function (item) {
                return item.day && item.day.date === day.date;
            })[0];
            const swatch = Util.findAll(popup, function (item) {
                return typeof item.index === "number" && item.index === 3 && item.width === 9 && item.height === 9;
            })[0];
            verify(cell, "no day cell for " + day.date);
            verify(swatch, "no level-3 legend swatch");

            const sink = sinkFor("year");
            sample(sink, popup.contentItem, 900);

            mouseClick(cell, cell.width / 2, cell.height / 2);
            sample(sink, popup.contentItem, 800);

            mouseClick(swatch, swatch.width / 2, swatch.height / 2);
            sample(sink, popup.contentItem, 1000);

            const wheel = visualFind(popup.contentItem, function (item) { return item.objectName === "yearHueWheel"; });
            const hue = visualFind(wheel, function (item) {
                return typeof item.angle === "number" && typeof item.hue === "number" && item.index === 7;
            });
            const close = visualFind(popup.contentItem, function (item) { return item.objectName === "yearPaletteClose"; });
            verify(wheel, "the palette did not open");
            verify(hue, "no blue hue cell");
            verify(close, "no palette close button");

            mouseClick(hue, hue.width / 2, hue.height / 2);
            sample(sink, popup.contentItem, 900);

            mouseClick(close, close.width / 2, close.height / 2);
            sample(sink, popup.contentItem, 900);

            compare(popup.selectedDate, day.date);
            compare(popup.hueIndex, 7);
            compare(popup.paletteOpen, false);
            Tokens.theme = was;
        }
    }
}
