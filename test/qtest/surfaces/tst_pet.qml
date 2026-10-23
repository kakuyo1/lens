import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The desktop pet's settings and sprite layer: pass-through is only reachable while the pet is on (PHASE3 3.5),
 * and a layer shows the one cell its frame names (PHASE3 3.7).
 *
 * No pet window is created: the page is built in the test and the layer is an Image that draws nothing without a sheet.
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
        id: panelComponent

        Rectangle {
            width: 480
            height: page.implicitHeight
            color: Tokens.panel

            SettingsExtensionsPage {
                id: page
                width: 480
            }
        }
    }

    Component {
        id: layerComponent

        SpriteLayer {}
    }

    TokenHost { id: host; width: root.width; height: root.height }

    TestCase {
        id: testCase
        name: "Pet"
        when: windowShown

        function init() {
            Pet.setEnabled(false);
        }

        function cleanup() {
            Pet.setEnabled(false);
        }

        function test_passthroughIsOnlyInteractiveWhilePetIsOn() {
            const page = createTemporaryObject(pageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            const passthrough = findChild(page, "passthroughSwitch");
            verify(passthrough !== null);
            compare(passthrough.interactive, false);

            Pet.setEnabled(true);
            compare(passthrough.interactive, true);
        }

        /// The landing's optional action is in the same row as the other roles, under its own label.
        function test_theDizzyRoleHasAChip() {
            Pet.setEnabled(true);
            const page = createTemporaryObject(pageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            const chips = Util.findAll(page, function (o) { return o.modelData === "dizzy"; });
            compare(chips.length, 1, "the dizzy landing is not in the actions row");
            verify(chips[0].label !== "", "the dizzy chip has no label");
        }

        /// The sign action is one of the Actions chips, and its row is captured on its own, since the panel snapshot
        /// above is cut off above the Actions group.
        function test_theSignActionIsAChipInTheActionsRow() {
            // The preview, actions and scenes sections are hidden while the pet is off, so the chip only shows with it on.
            Pet.setEnabled(true);
            const page = createTemporaryObject(pageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            const chips = Util.findAll(page, function (o) { return o.modelData === "ask"; });
            compare(chips.length, 1);
            verify(chips[0].visible, "the sign chip is shown to the reader");
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the snapshot of the actions row");
            // The page is sized to its content, or the rows below its first screen are clipped. Then it moves so the
            // row sits near the top of the host, where the grab can see it.
            page.height = page.implicitHeight;
            const row = chips[0].parent;
            page.y = 30 - row.mapToItem(page, 0, 0).y;
            waitForRendering(page);
            verify(Util.saveSnapshot(testCase, host.contentItem, lensQaSnapshotDir, "pet-actions-row"));
        }

        /// The settings panel's content column is 340 DIP wide; the stage and its sign have to fit in that.
        function signPage() {
            Pet.setEnabled(true);
            const page = createTemporaryObject(pageComponent, host.contentItem, {width: 340});
            waitForRendering(host.contentItem);
            return page;
        }

        /// Picking the sign action in the preview shows the review sign standing beside the figure on the stage, and taking
        /// the action away takes the board down again.
        function test_theSignPreviewShowsTheBoardOnTheStage() {
            const page = signPage();
            const stage = Util.findAll(page, function (o) { return o.toString().indexOf("PetStage_QMLTYPE") === 0; })[0];
            const board = Util.findAll(page, function (o) { return o.toString().indexOf("ReviewSignBoard_QMLTYPE") === 0; })[0];
            verify(stage, "the settings page has no pet stage");
            verify(board, "the settings page has no review sign board");
            verify(!board.visible, "the sign showed before the sign action was picked");

            Pet.preview("ask");
            tryVerify(() => board.visible, 2000, "the sign action did not show the sign");
            verify(board.word !== "", "the preview has no word to put on the sign");
            verify(board.x >= 0 && board.x + board.width <= stage.width,
                   "the sign runs off the stage: " + board.x + " + " + board.width + " on " + stage.width);

            Pet.preview("idle");
            tryVerify(() => !board.visible, 2000, "the sign stayed up after another action was picked");
        }

        function test_captureTheSignPreview() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the snapshot of the hold-sign preview");

            const page = signPage();
            const stage = Util.findAll(page, function (o) { return o.toString().indexOf("PetStage_QMLTYPE") === 0; })[0];
            // The page is sized to its content and moved so the stage sits near the top of the host: a grab of an item
            // that lies below the host's first screen comes back showing the wrong part of the page.
            page.height = page.implicitHeight;
            page.y = 30 - stage.mapToItem(page, 0, 0).y;
            Pet.preview("ask");
            wait(200);
            waitForRendering(page);
            verify(Util.saveSnapshot(testCase, page, lensQaSnapshotDir, "pet-preview-sign"));
        }

        /// The board is drawn at the pet's magnification: unscaled at the default ×3.01, larger for a larger pet, and smaller
        /// for a smaller one with no floor (UI.md 4.13).
        function test_signBoardScalesWithThePet() {
            const chosen = Pet.size;
            const page = signPage();
            const board = Util.findAll(page, function (o) { return o.toString().indexOf("ReviewSignBoard_QMLTYPE") === 0; })[0];
            try {
                Pet.setSize(67);
                verify(Math.abs(board.boardScale - 1) < 1e-9, "the default size scales the board: " + board.boardScale);
                Pet.setSize(100);
                verify(Math.abs(board.boardScale - Pet.scale / board.designScale) < 1e-9, "the board did not grow with the pet");
                Pet.setSize(0);
                verify(Math.abs(board.boardScale - Pet.scale / board.designScale) < 1e-9, "the board has a floor at the smallest pet: " + board.boardScale);
            } finally {
                Pet.setSize(chosen);
            }
        }

        /// The post stands on the pet's foot line at every size, so the sign never floats above or sinks below the pet.
        function test_thePostStandsOnTheFootLineAtEverySize() {
            const chosen = Pet.size;
            const page = signPage();
            const stage = Util.findAll(page, function (o) { return o.toString().indexOf("PetStage_QMLTYPE") === 0; })[0];
            const board = Util.findAll(page, function (o) { return o.toString().indexOf("ReviewSignBoard_QMLTYPE") === 0; })[0];
            Pet.preview("ask");
            tryVerify(() => board.visible, 2000, "the sign action did not show the sign");
            try {
                for (const size of [0, 10, 20, 67, 100]) {
                    Pet.setSize(size);
                    const base = board.y + board.designBase * board.boardScale;
                    const feet = stage.figureY + (Pet.inkBox.y + Pet.inkBox.height) * Pet.scale;
                    verify(Math.abs(base - feet) < 1, "the post's foot is at " + base + ", not the pet's feet at " + feet + " at size " + size
                           + " (board y " + board.y + ", scale " + board.boardScale + ", figureY " + stage.figureY + ")");
                }
            } finally {
                Pet.setSize(chosen);
            }
        }

        function test_captureTheSignPreviewAcrossSizes() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR to save the snapshots of the hold-sign preview at several sizes");

            const chosen = Pet.size;
            try {
                for (const size of [0, 10, 20, 100]) {
                    Pet.setSize(size);
                    const page = signPage();
                    const stage = Util.findAll(page, function (o) { return o.toString().indexOf("PetStage_QMLTYPE") === 0; })[0];
                    page.height = page.implicitHeight;
                    page.y = 30 - stage.mapToItem(page, 0, 0).y;
                    Pet.preview("ask");
                    wait(200);
                    waitForRendering(page);
                    verify(Util.saveSnapshot(testCase, page, lensQaSnapshotDir, "pet-preview-sign-size-" + size));
                    page.destroy();
                    Pet.preview("idle");
                }
            } finally {
                Pet.setSize(chosen);
            }
        }

        function test_aLayerShowsTheCellItsFrameNames() {
            const layer = createTemporaryObject(layerComponent, root, {sheet: "", frame: 2, at: Qt.point(0, 0)});
            compare(layer.sourceClipRect.x, 2 * Pet.canvas);
            compare(layer.sourceClipRect.y, 0);
            compare(layer.sourceClipRect.width, Pet.canvas);
            compare(layer.sourceClipRect.height, Pet.canvas);
        }

        function test_anEmptySheetDrawsNothing() {
            const layer = createTemporaryObject(layerComponent, root, {sheet: "", frame: 0, at: Qt.point(0, 0)});
            compare(layer.visible, false);
        }

        function test_snapshotSettingsPage() {
            if (!lensQaSnapshotDir) skip("Set LENS_QA_SNAPSHOT_DIR to save snapshots");
            // The pet rows sit under the enable switch, so the pet has to be on for the picker to show.
            Pet.setEnabled(true);
            const theme = Tokens.theme;
            const chosen = Pet.pet;
            try {
                for (const entry of Pet.pets) {
                    const id = entry.id;
                    Pet.setPet(id);
                    for (const mode of ["light", "dark"]) {
                        Tokens.theme = mode;
                        const panel = createTemporaryObject(panelComponent, host.contentItem);
                        waitForRendering(host.contentItem);
                        verify(panel);
                        waitForRendering(panel);
                        verify(Util.saveSnapshot(testCase, panel, lensQaSnapshotDir, "pet-settings-" + id + "-" + mode));
                        panel.destroy();
                    }
                }
            } finally {
                Tokens.theme = theme;
                Pet.setPet(chosen);
            }
        }
    }
}
