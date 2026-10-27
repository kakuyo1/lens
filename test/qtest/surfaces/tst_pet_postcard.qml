import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The pet picker's option preview (UI-SPEC.md 4.4): resting the pointer on a row raises a postcard of that pet
 * beside the list, and taking the list down puts it down.
 *
 * The card is PetPostcardPreview.qml, a window of its own created on the first hover. It is not declared under the
 * field, so its labels never join the list's -- the preview is reached through the field's own `optionPreviewWindow`
 * handle rather than by walking the window tree.
 *
 * The page is built here rather than through Main, as the pet surface cases build it: the extensions page is the
 * only surface with a pet picker, and a whole Main would put a second copy of the settings panel in the way.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: pageComponent

        SettingsExtensionsPage {
            width: 480
        }
    }

    Component {
        id: fieldComponent

        DropdownField {
            width: 200
        }
    }

    Component {
        id: postcardComponent

        PetPostcardPreview {}
    }

    TokenHost { id: host; width: root.width; height: root.height }

    TestCase {
        id: testCase
        name: "PetPostcard"
        when: windowShown

        function init() {
            Pet.setEnabled(true);
        }

        function cleanup() {
            Pet.setEnabled(false);
        }

        /// @return The extensions page, with the pet section on and its picker in it.
        function makePage() {
            const page = createTemporaryObject(pageComponent, host.contentItem);
            verify(page);
            waitForRendering(host.contentItem);
            return page;
        }

        function fieldOf(page) {
            const field = Util.findAll(page, function (o) {
                return o.objectName === "petTypeField";
            })[0];
            verify(field, "the extensions page has no pet type field");
            return field;
        }

        /// @return The label the picker shows for @p id, which is what the row and the card both print.
        function labelOf(field, id) {
            for (let i = 0; i < field.options.length; ++i) {
                if (field.options[i].value === id)
                    return field.options[i].label;
            }
            return "";
        }

        /// @return The row's index in the list, which is what the field reports while it is hovered. Read rather
        ///         than assumed: the catalogue's order is the folder's, not this case's.
        function indexOf(field, id) {
            for (let i = 0; i < field.shown.length; ++i) {
                if (field.shown[i].value === id)
                    return i;
            }
            return -1;
        }

        /// @return The row showing @p label in the list's own window.
        function rowOf(field, label) {
            const row = Util.textWith(Util.textsInNestedWindow(field), [label]);
            verify(row, "the pet list has no '" + label + "' row");
            return row;
        }

        function hover(field, id) {
            const row = rowOf(field, labelOf(field, id));
            mouseMove(row, row.width / 2, row.height / 2);
        }

        function test_hoveringAPetRowRaisesItsPostcard() {
            const page = makePage();
            const field = fieldOf(page);
            verify(field.options.length >= 7, "the picker does not list the shipped pets");
            field.openList();
            wait(50);
            compare(field.optionPreviewWindow, null, "a card was built before anything was hovered");

            hover(field, "cat");
            tryCompare(field, "hoveredIndex", indexOf(field, "cat"));
            const card = field.optionPreviewWindow;
            verify(card, "hovering a pet built no card");
            tryCompare(card, "visible", true);
            compare(card.hasContent, true, "the cat's card carries nothing");
            compare(card.petName, labelOf(field, "cat"), "the card does not print the row's own label");
            verify(card.bodySheet.endsWith("/cat/sprites/body/idle.png"), "the card does not draw the cat's idle sheet");
            verify(card.faceSheet.endsWith("/sprites/expressions/eyes.png"), "the card draws no eyes");

            // The next pet takes the same window over rather than a second one being built.
            hover(field, "owl");
            tryCompare(field, "hoveredIndex", indexOf(field, "owl"));
            compare(field.optionPreviewWindow, card, "a second card was built");
            tryCompare(card, "petName", labelOf(field, "owl"));
            verify(card.bodySheet.endsWith("/owl/sprites/body/idle.png"), "the card kept the cat's sheet");

            field.closeList();
            tryCompare(card, "visible", false);
        }

        /// A package is the reader's own art, so it has no card: the window stays down rather than standing empty.
        /// The option carries a shipped pet's id on purpose -- the `external` flag is what must keep it down, not an
        /// id the catalogue cannot resolve.
        function test_anInstalledPetHasNoCard() {
            const field = createTemporaryObject(fieldComponent, host.contentItem, {
                options: [
                    {value: "cat", label: "Cat", group: "", note: "", external: false},
                    {value: "lounge", label: "Lounge", group: "", note: "External", external: true}
                ],
                currentValue: "cat",
                optionPreview: postcardComponent
            });
            verify(field);
            field.openList();
            wait(50);

            hover(field, "cat");
            tryCompare(field, "hoveredIndex", indexOf(field, "cat"));
            const card = field.optionPreviewWindow;
            verify(card, "the shipped pet raised no card");
            tryCompare(card, "visible", true);

            // The package row raises the same window and immediately takes it down again.
            const row = rowOf(field, "Lounge");
            mouseMove(row, row.width / 2, row.height / 2);
            tryCompare(field, "hoveredIndex", indexOf(field, "lounge"));
            compare(card.hasContent, false, "a package was given a card");
            tryCompare(card, "visible", false);

            field.closeList();
        }

        /// The card as it renders, one shot per theme: TEST.md section 5's snapshot path, which writes nothing
        /// unless LENS_QA_SNAPSHOT_DIR is set.
        function test_capturePetPostcard() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const page = makePage();
            const field = fieldOf(page);
            field.openList();
            wait(50);
            // The page is built without Main here, so nothing keeps Tokens in step with the settings document:
            // the table is set the way Main sets it.
            const previous = Tokens.theme;
            Tokens.theme = "light";
            wait(50);
            hover(field, "cat");
            const card = field.optionPreviewWindow;
            tryCompare(card, "visible", true);
            // The card fades in through ShadowCard; a shot taken mid-fade is a translucent card.
            wait(260);
            verify(Util.saveSnapshot(testCase, card.contentItem, lensQaSnapshotDir, "pet-postcard-light"));
            Tokens.theme = "dark";
            wait(120);
            verify(Util.saveSnapshot(testCase, card.contentItem, lensQaSnapshotDir, "pet-postcard-dark"));
            Tokens.theme = previous;
            field.closeList();
        }
    }
}
