import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The English font setting (UI-SPEC.md 4.4): the list the field offers, what the setting keeps, and
 * what it is for -- the English runs of an explanation following it while the reader's own
 * language does not.
 *
 * The setting is a document key, so every case puts it back. The families are the machine's own,
 * which is why the cases pick a family out of the list the field is handed rather than naming one:
 * a name that is only on some Windows would make this case red on the others.
 */
Item {
    id: root
    width: 480
    height: 320

    Component {
        id: mainComponent
        Main {}
    }

    Component {
        id: bubbleComponent
        Bubble {}
    }

    Component {
        id: previewComponent
        Bubble { preview: true }
    }

    /// A Text that names no family: what a field in the reader's own language is drawn as.
    Component {
        id: bareTextComponent
        Text {}
    }

    /// The window that owns the colour table the page reads through Window.window (docs/adr/0041),
    /// so the field in the snapshot is drawn rather than failing on a null window.
    TokenHost { id: host }

    Component {
        id: pageCardComponent

        Rectangle {
            // The width the page has in the panel: the 380 px card less the 20 px the body indents.
            width: 340
            height: page.implicitHeight
            color: Tokens.panel

            SettingsFontPage {
                id: page
                width: 340
            }
        }
    }

    TestCase {
        id: testCase
        name: "Font"
        when: windowShown

        function cleanup() {
            Controller.setSetting("font", "");
        }

        /// @return The families the field is offered, as plain names.
        function familyNames() {
            return Controller.latinFontFamilies().map((option) => option.value);
        }

        /// @return The Texts under `item` that draw `text`.
        ///
        /// A component with a `text` property is not a Text -- RevealText carries one -- and it has
        /// no font, so the walk is narrowed to the things that draw.
        function drawnTexts(item, text) {
            return Util.findAll(item, (o) => o.text === text && o.font !== undefined);
        }

        /// @return A family that is not the one in force, or "" when the machine has only that one.
        function anotherFamily() {
            const names = familyNames();
            for (let i = 0; i < names.length; ++i) {
                if (names[i] !== Controller.settings.font)
                    return names[i];
            }
            return "";
        }

        function opened() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                 title: "serendipity",
                 type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                en: "the occurrence of events by chance in a happy way",
                zh: "机缘巧合",
                status: "new",
                x: 200,
                y: 240
            });
            wait(50);
            return bubble;
        }

        function test_theListIsThisMachinesLatinFamilies() {
            const options = Controller.latinFontFamilies();
            verify(options.length > 0, "the field has no family to offer");

            const names = [];
            for (let i = 0; i < options.length; ++i) {
                const name = options[i].value;
                verify(name.length > 0, "an option carries no family name");
                compare(options[i].label, name, "the row shows something other than the family");
                verify(name.charAt(0) !== "@", "'" + name + "' is a vertical-writing duplicate");
                verify(names.indexOf(name) < 0, "'" + name + "' is offered twice");
                names.push(name);
            }

            // The face in force is always one the field can show: it is where the setting starts,
            // and a field showing nothing on a machine nobody has touched reads as broken.
            verify(names.indexOf(Controller.settings.font) >= 0,
                   "'" + Controller.settings.font + "' is in force and not in the list");
        }

        function test_theSettingKeepsThePick() {
            const picked = anotherFamily();
            if (picked === "")
                skip("this machine has one Latin family");

            Controller.setSetting("font", picked);
            compare(Controller.settings.font, picked);

            // A name this machine does not have is dropped rather than stored: QML would draw it
            // as whatever the font matcher picked instead, under a name that is not in the list.
            Controller.setSetting("font", "No Such Family At All");
            compare(Controller.settings.font, picked, "an unknown family was stored");

            // Empty is the application's own face, which is the first family main() installed.
            Controller.setSetting("font", "");
            compare(Controller.settings.font, Qt.application.font.family);
        }

        function test_theEnglishRunsFollowItAndTheReadersLanguageDoesNot() {
            const other = anotherFamily();
            if (other === "")
                skip("this machine has one Latin family");

            const bubble = opened();
            const english = "the occurrence of events by chance in a happy way";
            // The word: one Text, the whole title. The definition: a RevealText, so the Text
            // carrying the whole line is the invisible one it reserves its height by.
            const words = drawnTexts(bubble, "serendipity");
            const definitions = drawnTexts(bubble, english);
            compare(words.length, 1, "the bubble drew no title");
            compare(definitions.length, 1, "the bubble drew no definition line");
            const word = words[0];
            const definition = definitions[0];
            compare(word.font.family, Controller.settings.font);

            Controller.setSetting("font", other);
            wait(20);
            compare(word.font.family, other, "the word did not follow the setting");
            compare(definition.font.family, other, "the definition did not follow the setting");
        }

        function test_aFieldInTheReadersLanguageKeepsTheApplicationFace() {
            const other = anotherFamily();
            if (other === "")
                skip("this machine has one Latin family");

            const bubble = opened();
            Controller.setSetting("font", other);
            wait(20);

            // The Chinese line is drawn a character at a time (RevealText), so its face is read
            // off the characters -- the settled one and the blurred slot the tail moves through --
            // and compared with a Text that names no family at all: the two resolving to the same
            // face is what "other languages keep the application's own face" means.
            const characters = drawnTexts(bubble, "机");
            verify(characters.length > 0, "the bubble did not draw the Chinese line");
            const bare = createTemporaryObject(bareTextComponent, root, { text: "机" });
            verify(bare);
            for (let i = 0; i < characters.length; ++i) {
                compare(characters[i].fontInfo.family, bare.fontInfo.family,
                        "the reader's own language moved with the font setting");
            }
        }

        /// The page itself, offscreen: the silent snapshot of TEST.md section 5, which is how the
        /// page is looked at without driving the real desktop. Needs LENS_QA_SNAPSHOT_DIR
        /// (scripts/qa/qml-snapshot.ps1), and skips without it rather than writing a stray file.
        function test_captureTheFontPage() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const card = createTemporaryObject(pageCardComponent, host.contentItem);
            verify(card);
            wait(100);
            verify(Util.saveSnapshot(testCase, card, lensQaSnapshotDir, "font-page"));
        }

        /// The preview card the "Font" label raises (UI-SPEC.md 4.4), offscreen: the same Bubble, with
        /// the payload the panel builds for that key. Set in a serif where the machine has one,
        /// since a snapshot of a face that looks like the one beside it shows nothing.
        function test_captureTheFontPreview() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const names = familyNames();
            Controller.setSetting("font", names.indexOf("Georgia") >= 0 ? "Georgia" : Controller.settings.font);
            const payload = PreviewSample.payload("font");
            verify(payload, "the font preview has no sample to draw");

            const card = createTemporaryObject(previewComponent, root);
            verify(card);
            // Whole, not written out a character at a time: the snapshot is of the card, and the
            // reveal would catch it a few characters in (Bubble.show reads this off the payload).
            payload.reveal = false;
            card.show(payload);
            wait(200);
            verify(Util.saveSnapshot(testCase, card.contentItem, lensQaSnapshotDir, "font-preview"));
        }

        function test_theGeneralPageOpensTheFontPageOnTheFamilyInForce() {
            Controller.setSetting("font", "");
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            Util.surfaceHost(main).showSettings();
            wait(25);

            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            panel.openCategory("general");
            wait(25);

            const rows = Util.findAll(panel, (o) =>
                o.toString().indexOf("SettingsLinkRow_QMLTYPE") === 0 && o.label === "Font");
            compare(rows.length, 1, "the general page has no font row");
            mouseClick(rows[0], rows[0].width / 2, rows[0].height / 2);
            wait(25);
            compare(panel.route, "font", "the font row did not open the font page");

            const fields = Util.findAll(panel, (o) =>
                o.toString().indexOf("DropdownField_QMLTYPE") === 0 && o.objectName === "fontField");
            compare(fields.length, 1, "the font page has no field");
            compare(fields[0].currentValue, Controller.settings.font);
            compare(fields[0].options.length, Controller.latinFontFamilies().length);
        }

        /// The preview belongs to the rows of the list (UI-SPEC.md 4.4), not to the "Font" title,
        /// and each row draws the same bubble in its own family.
        function test_theFontRowsRaiseTheBubblePreviewInTheirOwnFace() {
            Controller.setSetting("font", "");
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            Util.surfaceHost(main).showSettings();
            wait(25);

            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            panel.openCategory("general");
            wait(25);
            panel.openRoute("font");
            wait(25);

            const preview = Util.findAll(main, (o) => {
                return o.toString().indexOf("Bubble_QMLTYPE") === 0 && o.preview === true;
            })[0];
            verify(preview, "Main did not build the preview bubble");
            compare(preview.visible, false);

            // A rest on the title is no longer a gesture: only a row of the list is.
            const title = Util.textWith(Util.ownWindow(panel), ["Font"]);
            verify(title, "the font page has no title to rest on");
            mouseMove(title, title.width / 2, title.height / 2);
            wait(200);
            compare(preview.visible, false, "the font title still raises the preview");

            const field = Util.findAll(panel, (o) => o.objectName === "fontField")[0];
            verify(field, "the font page has no field");
            field.openList();
            wait(50);

            // The families are the machine's own, so the row is read from the field rather than
            // named here; the list is sorted, and the first two rows are always drawn.
            const first = field.options[0].value;
            const row = Util.textWith(Util.textsInNestedWindow(field), [first]);
            verify(row, "the list draws no row for '" + first + "'");
            mouseMove(row, row.width / 2, row.height / 2);
            tryCompare(preview, "visible", true);
            compare(preview.englishFont, first, "the preview did not take the hovered family");

            const second = field.options[1].value;
            const next = Util.textWith(Util.textsInNestedWindow(field), [second]);
            verify(next, "the list draws no row for '" + second + "'");
            mouseMove(next, next.width / 2, next.height / 2);
            tryCompare(preview, "englishFont", second);
            compare(preview.visible, true, "moving to the next row took the card down");

            field.closeList();
            tryCompare(preview, "visible", false);
        }
    }
}
