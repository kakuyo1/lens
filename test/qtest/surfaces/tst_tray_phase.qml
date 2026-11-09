import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The tray menu's own state and the language list it overlays: hidden/leaving/shown from the
 * window and the handover flag, and the list's unfold/fold going through one pair of functions
 * (UI-SPEC.md 4.2).
 */
Item {
    id: root
    width: 480
    height: 320

    Component {
        id: menuComponent
        TrayMenu {}
    }

    TestCase {
        id: testCase
        name: "TrayPhase"
        when: windowShown

        function test_theMenuNamesItsOwnPhase() {
            const menu = createTemporaryObject(menuComponent, root);
            verify(menu);
            compare(menu.phase, "hidden");

            menu.visible = true;
            compare(menu.phase, "shown");

            menu.leaving = true;
            compare(menu.phase, "leaving");

            menu.leaving = false;
            compare(menu.phase, "shown");
            menu.visible = false;
            compare(menu.phase, "hidden");
        }

        function test_theLanguageListFoldsAfterItsGrace() {
            const menu = createTemporaryObject(menuComponent, root);
            verify(menu);
            menu.contentActive = true;
            menu.visible = true;
            const row = Util.findAll(menu, function (o) { return o.label === "Language"; })[0];
            verify(row, "the menu drew no language row");

            mouseMove(row, row.width / 2, row.height / 2);
            tryCompare(menu, "listVisible", true, 1000);

            // Leaving the row starts the grace that lets the pointer cross the gap to the list;
            // the fold is only the timer running out.
            mouseMove(menu.contentItem, 4, 4);
            wait(60);
            compare(menu.listVisible, true, "the list folded before its grace ran out");
            tryCompare(menu, "listVisible", false, 1000, "the list outlived its grace");
        }
    }
}
