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

    TokenHost { id: host }

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
                for (const id of Pet.pets) {
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
