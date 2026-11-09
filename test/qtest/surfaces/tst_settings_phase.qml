import QtQuick
import QtTest
import Lens

/**
 * The settings panel's own state, read off its shell card: hidden, leaving while another panel
 * takes its place, or shown (UI-SPEC.md 4.4).
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: settingsComponent
        SettingsPopup {}
    }

    TestCase {
        id: testCase
        name: "SettingsPhase"
        when: windowShown

        function test_thePanelNamesItsOwnPhase() {
            const panel = createTemporaryObject(settingsComponent, root);
            verify(panel);
            compare(panel.phase, "hidden");

            panel.visible = true;
            compare(panel.phase, "shown");

            panel.leaving = true;
            compare(panel.phase, "leaving");

            panel.leaving = false;
            compare(panel.phase, "shown");
            panel.visible = false;
            compare(panel.phase, "hidden");
        }
    }
}
