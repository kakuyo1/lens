import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The frame sequences the README's GIFs are cut from, and the still of the pet settings page: a word's
 * explanation with its origin and example, marking that word known, the year table, and the four desktop
 * pets each doing one action.
 *
 * Each case steps through one interaction and grabs the surface at every step, so the frames can be
 * encoded into an animation. Input is in-scene only: QTest's mouseMove and mouseClick deliver events
 * to the window, and the real pointer, keyboard and clipboard are never touched. The only writes are
 * to the settings copy setup.cpp made for this run, and the one model-facing path (reviewWord) is a
 * cache hit from that copy's entry, so no request leaves the process.
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
        id: yearWordsComponent
        YearWordsPopup {}
    }

    // The window that owns the colour table the extensions page reads through Window.window (docs/adr/0041),
    // so the switches and the slider in the still are drawn rather than failing on a null window.
    TokenHost { id: host }

    Component {
        id: extensionsCardComponent

        Rectangle {
            width: 440
            height: page.implicitHeight
            color: Tokens.panel

            SettingsExtensionsPage {
                id: page
                width: 440
            }
        }
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

        /// Clip: a word's explanation arriving with its origin and example, the pointer over the card so
        /// the Known and New row opens, Known pressed, and the badge changing. The word is cached in the
        /// run's settings copy with both fields, so the card draws them through the controller's own path,
        /// and the word is put back to unmarked afterwards so the run leaves the store as it found it.
        function test_wordBubbleClip() {
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
            verify(bubble.etymology !== "", "the cached entry carries no origin to draw");
            verify(bubble.example !== "", "the cached entry carries no example to draw");

            const texts = Util.textsUnder(bubble);
            const card = textNamed(texts, "panel");
            // The label the bubble draws in the running language, looked up in the bubble's own context.
            const known = textNamed(texts, qsTranslate("Bubble", "Known"));

            const sink = sinkFor("bubble");
            sample(sink, bubble.contentItem, 900);

            mouseMove(card, card.width / 2, card.height / 2);
            sample(sink, bubble.contentItem, 900);

            mouseClick(known, known.width / 2, known.height / 2);
            sample(sink, bubble.contentItem, 1200);
            compare(bubble.status, "known", "pressing Known did not mark the word");

            Controller.explanationDuty.mark("panel", false);
            Tokens.theme = was;
        }

        /// One pet doing one action, for the clip of that pet. The pet is chosen in this run's settings copy,
        /// since Pet.setPet writes it there, and the action is shown through Pet.preview(), the call the
        /// settings page's action pill makes. Each play starts from the first frame and runs for @p msPerPlay,
        /// so a one-shot action is played @p plays times to fill the clip, and a looping one needs only one.
        /// The machine's own reactions are not driven here: the surfaces harness does not connect the pet to
        /// the app's events (main.cpp does), so the preview is the only route a clip can take.
        function petClip(id, action, plays, msPerPlay) {
            Pet.setPet(id);
            const main = make();
            Pet.setEnabled(true);
            const petWindow = panel(main, "PetWindow");
            tryCompare(petWindow, "visible", true);
            // The pet window is transparent on the desktop; a grab of it is opaque black unless the
            // backdrop is set. The page colour is what the GIF sits on, so the frame gets it too.
            petWindow.color = pageColor;

            const sink = sinkFor("pet");
            for (let i = 0; i < plays; ++i) {
                Pet.preview(action);
                sample(sink, petWindow.contentItem, msPerPlay);
            }

            Pet.setPreviewing(false);
            Pet.setEnabled(false);
        }

        /// Clip: Mochi celebrating, the celebrate action played four times.
        function test_petMochiCelebrateClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            petClip("mochi", "celebrate", 4, 1000);
        }

        /// Clip: Cat kneading, the knead action played five times.
        function test_petCatKneadClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            petClip("cat", "knead", 5, 900);
        }

        /// Clip: Dog poked. The action is named click_react in the pet's data and reads "Poked" in the settings.
        function test_petDogPokedClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            petClip("dog", "click_react", 6, 700);
        }

        /// Clip: Owl reading. The action is named study in the pet's data and reads "Reading" in the settings;
        /// it loops, so one play covers a few cycles.
        function test_petOwlReadingClip() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the frame sequence");

            petClip("owl", "study", 1, 4500);
        }

        /// Clip: the year table, a day picked, the colour palette opened, a hue chosen, and the
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
            verify(wheel, "the palette did not open");
            verify(hue, "no blue hue cell");

            mouseClick(hue, hue.width / 2, hue.height / 2);
            sample(sink, popup.contentItem, 900);

            // The open level's own legend cell shuts the palette; the palette has no close button.
            mouseClick(swatch, swatch.width / 2, swatch.height / 2);
            sample(sink, popup.contentItem, 900);

            compare(popup.selectedDate, day.date);
            compare(popup.hueIndex, 7);
            compare(popup.paletteOpen, false);
            Tokens.theme = was;
        }

        /// Still: the extensions page as the app draws it with the desktop pet on, so the pet type, the
        /// preview stage and the actions are all in the frame. The pet type list is a window of its own
        /// and a grab of the page does not take it in, so the list stays shut. Written once per language.
        function test_petSettingsStill() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the still");

            const was = recordLight();
            Pet.setEnabled(true);
            // The window is what a grab is cut to, so it is taller than the whole page it holds.
            host.width = 440;
            host.height = 1200;
            const card = createTemporaryObject(extensionsCardComponent, host.contentItem);
            verify(card);
            waitForRendering(card);

            verify(Util.saveSnapshot(testCase, card, lensQaSnapshotDir, "pet-ui"));
            Pet.setEnabled(false);
            Tokens.theme = was;
        }
    }
}
