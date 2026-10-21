import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The vocabulary-level dropdown's preview (UI.md 4.4): resting the pointer on a level row raises
 * a word-cloud card for that level beside the list, and taking the list down puts it down.
 *
 * The words are content, so the cases read the expected list out of Controller.settingPreview
 * rather than writing it here; what is asserted is that the hovered row picks its own level.
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "VocabPreview"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// The snapshot case changes the theme; every case leaves the fixture's dark in force.
        function cleanup() {
            Controller.setTheme("dark");
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        /// @return The vocabulary level field, with its list open.
        function openedLevelField(main) {
            const panel = settingsPanel(main);
            Util.surfaceHost(main).showSettings();
            panel.openCategory("learning");
            wait(25);
            const field = Util.findAll(panel, function (o) {
                return o.objectName === "levelField";
            })[0];
            verify(field, "the learning page has no vocabulary level field");
            field.openList();
            wait(50);
            return field;
        }

        function hover(field, label) {
            const row = Util.textWith(Util.textsInNestedWindow(field), [label]);
            verify(row, "the level list has no '" + label + "' row");
            mouseMove(row, row.width / 2, row.height / 2);
        }

        function test_hoveringALevelRowRaisesItsWordCloudBesideTheList() {
            const main = make();
            const field = openedLevelField(main);
            compare(field.optionPreviewWindow, null, "a preview was built before anything was hovered");

            hover(field, "CET-6");
            tryCompare(field, "hoveredIndex", 3);
            const preview = field.optionPreviewWindow;
            verify(preview, "hovering a level built no preview window");
            tryCompare(preview, "visible", true);
            compare(preview.levelLabel, "CET-6");

            const sample = Controller.settingPreview.vocabulary;
            verify(sample && sample["3"] && sample["3"].length > 0,
                   "data/preview.json carries no CET-6 words");
            compare(preview.words.length, sample["3"].length);
            compare(preview.words[0], sample["3"][0]);
            const drawn = Util.textsUnder(preview).map(function (t) { return t.text; });
            verify(drawn.indexOf(sample["3"][0]) >= 0, "the card draws none of its sample words");

            // The next row takes the same window over rather than a second one being built.
            hover(field, "TOEFL");
            tryCompare(field, "hoveredIndex", 7);
            compare(field.optionPreviewWindow, preview, "a second preview window was built");
            tryCompare(preview, "levelLabel", "TOEFL");
            compare(preview.words[0], sample["7"][0], "the words did not follow the hovered level");
            compare(preview.visible, true, "moving to the next row took the card down");

            // The card takes the side with room, clear of the list's card. The offscreen screen is
            // narrow, so which side that is is read from the geometry rather than assumed.
            const list = field.openChildRect;
            const area = Util.surfaceHost(main).workArea(Util.surfaceHost(main).screenFor(Qt.point(list.x, list.y)));
            const gap = 8;
            if (list.x + list.width + gap + preview.width - preview.shadowMargin <= area.right - gap) {
                compare(preview.x + preview.shadowMargin, list.x + list.width + gap,
                        "the card did not take the room on the list's right");
            } else if (list.x - gap - preview.width + preview.shadowMargin >= area.left + gap) {
                compare(preview.x + preview.width - preview.shadowMargin, list.x - gap,
                        "the card did not take the room on the list's left");
            }

            field.closeList();
            tryCompare(preview, "visible", false);
        }

        /// The card as it renders, one shot per theme: TEST.md section 5's snapshot path, which
        /// writes nothing unless LENS_QA_SNAPSHOT_DIR is set.
        function test_captureVocabPreviewCard() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const main = make();
            const field = openedLevelField(main);
            Controller.setTheme("light");
            wait(50);
            hover(field, "CET-6");
            const preview = field.optionPreviewWindow;
            tryCompare(preview, "visible", true);
            // The card fades in through ShadowCard; a shot taken mid-fade is a translucent card.
            wait(260);
            verify(Util.saveSnapshot(testCase, preview.contentItem, lensQaSnapshotDir,
                                     "vocab-preview-light"));
            Controller.setTheme("dark");
            wait(80);
            verify(Util.saveSnapshot(testCase, preview.contentItem, lensQaSnapshotDir,
                                     "vocab-preview-dark"));
            field.closeList();
        }
    }
}
