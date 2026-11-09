import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The action bar's and its copy drawer's appearance as one word: hidden/opening/shown/fading on
 * the bar, closed/opening/open/closing on the drawer, and the dwells that move between them
 * (UI-SPEC.md 4.10).
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: barComponent
        SelectionBar { selectionText: "a whole sentence" }
    }

    TestCase {
        id: testCase
        name: "SelectionBarPhase"
        when: windowShown

        function init() {
            Controller.setSetting("selectionBarStyle", "bar");
        }

        /// A sentence's bar, placed and up, with the copy drawer its actions ask for.
        function drawnBar() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            bar.selectionKind = "sentence";
            bar.breakdownable = true;
            bar.grammarable = true;
            bar.placeAt(Qt.point(300, 300), {left: 0, top: 0, right: 800, bottom: 600}, 8);
            bar.open();
            wait(50);
            return bar;
        }

        function test_theBarNamesItsOwnPhase() {
            const bar = createTemporaryObject(barComponent, root);
            verify(bar);
            compare(bar.phase, "hidden");

            bar.open();
            if (!Tokens.motion.reduced)
                compare(bar.phase, "opening", "the bar is on its way up before the fade ends");
            tryCompare(bar, "phase", "shown", Tokens.motion.pop + 400);

            bar.dismiss();
            if (!Tokens.motion.reduced)
                compare(bar.phase, "fading", "the window is still on screen while the bar fades out");
            tryCompare(bar, "phase", "hidden", Tokens.motion.press + 400);
        }

        function test_theDrawerNamesItsOpenAndClose() {
            const bar = drawnBar();
            // The classic body carries one drawer per action, and only the copy item's has items.
            const drawer = Util.findAll(bar, function (o) {
                return o.expand !== undefined && o.form !== undefined
                       && o.items !== undefined && o.items.length > 0;
            })[0];
            verify(drawer && drawer.applies, "the bar drew no copy drawer");
            compare(drawer.phase, "closed");

            drawer.copyHovered = true;
            compare(drawer.phase, "opening", "the dwell has not opened the drawer yet");
            tryCompare(drawer, "phase", "open", 600);

            drawer.copyHovered = false;
            compare(drawer.phase, "closing", "a drawer waiting out its close delay is not shut");
            tryCompare(drawer, "phase", "closed", 900);

            drawer.expand();
            compare(drawer.phase, "open", "a screen reader's press opens at once");
            drawer.shown = false;
            compare(drawer.phase, "closed", "the drawer outlived the bar it belongs to");
        }
    }
}
