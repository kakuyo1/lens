import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The Main-into-bubble placement handover, split out of tst_main.qml to keep that file under
 * the .qml size cap (docs/QML.md section 1): the cases here raise a bubble through Main's own
 * wiring and pin what SurfaceHost does with the frame afterwards.
 *
 * The root reads Controller.settings on its first binding, and make() builds Main the way
 * tst_main.qml does. See TEST.md section 5 for what a person was doing instead.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "MainBubble"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// The root is a Window rather than an Item, so it is the one place a panel cannot be
        /// reached through an id: Util.ofType is what gets at one from outside.
        function panel(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        /// The reader's side reaches the geometry: with room on both sides at mid-screen, above puts
        /// the card over the anchor and below under it. The rule itself is surface_geometry_test's;
        /// this case pins that placeTransient() hands the setting over.
        function test_aBubbleHangsOnTheSideTheReaderChose() {
            const main = make();
            const bubble = panel(main, "Bubble");
            const anchor = Qt.point(400, 300);
            const previous = Controller.settings.bubblePlacement;

            try {
                Controller.setSetting("bubblePlacement", "above");
                Util.surfaceHost(main).placeTransient(bubble, anchor, bubble.gap);
                const above = bubble.y;
                verify(above + bubble.height - bubble.shadowMargin <= anchor.y,
                       "the card did not hang above the anchor");

                Controller.setSetting("bubblePlacement", "below");
                Util.surfaceHost(main).placeTransient(bubble, anchor, bubble.gap);
                verify(bubble.y + bubble.shadowMargin >= anchor.y,
                       "the card did not hang below the anchor");
                verify(bubble.y !== above, "the reader's side did not move the card");
            } finally {
                Controller.setSetting("bubblePlacement", previous);
            }
        }

        /// The verdict pills write their word and re-present the standing card, and the re-place
        /// that follows must not undo where the reader put the card. The drag is driven the way
        /// the other bubble cases drive it: the flag transitions the DragHandler produces, with
        /// the window moved by hand (TEST.md section 5 owns the real-pointer pass).
        function test_aVerdictPressLeavesADraggedCardWhereItWasPut() {
            const main = make();
            const bubble = panel(main, "Bubble");
            const previousLang = Controller.settings.explanationLang;
            const previousMultiSense = Controller.settings.multiSense;
            try {
                Controller.setSetting("explanationLang", "zh");
                Controller.setSetting("multiSense", false);

                // The fixture's stored word answers out of the cache: no request, a whole card.
                Controller.explainLemma("panel");
                tryCompare(Controller.explanationDuty.bubble, "title", "panel");
                tryCompare(bubble, "surfaceShown", true);
                wait(50);

                bubble.dragging = true;
                bubble.x = bubble.x + 150;
                bubble.y = bubble.y + 60;
                bubble.dragging = false;
                const draggedTo = {x: bubble.x, y: bubble.y};

                // The press itself is the pill's own call (BubbleVerdictRow's TapHandler); the
                // wait gives the refresh both of its turns -- the placement timer and the host's
                // re-place -- either of which would pull the card back to its anchor.
                Controller.explanationDuty.mark("panel", "known");
                compare(bubble.status, "known", "the verdict never reached the card");
                wait(50);
                compare(bubble.x, draggedTo.x, "the verdict press moved the dragged card sideways");
                compare(bubble.y, draggedTo.y, "the verdict press moved the dragged card");
            } finally {
                Controller.explanationDuty.mark("panel", "new");
                Controller.explanationDuty.dismissBubble();
                Controller.setSetting("multiSense", previousMultiSense);
                Controller.setSetting("explanationLang", previousLang);
            }
        }
    }
}
