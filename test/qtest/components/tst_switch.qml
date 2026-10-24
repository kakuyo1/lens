import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The 34x20 pill switch.
 *
 * The non-interactive switch is the case with a rule behind it: the phase-1 placeholders have
 * to read as unavailable without saying why. So it is dimmed and answers nothing, and both
 * halves are asserted -- the dimming alone would leave a switch that still toggles.
 */
Item {
    id: root
    width: 200
    height: 120

    Component {
        id: switchComponent
        Switch {}
    }

    TokenHost { id: host }

    TestCase {
        id: testCase
        name: "Switch"
        when: windowShown

        function make(properties) {
            const switcher = createTemporaryObject(switchComponent, host.contentItem, properties);
            waitForRendering(host.contentItem);
            verify(switcher);
            return switcher;
        }

        function test_clickingFlipsItAndReportsTheNewState() {
            const switcher = make({});
            let reported = null;
            switcher.toggled.connect(function (on) { reported = on; });

            compare(switcher.checked, false);
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            compare(switcher.checked, true);
            compare(reported, true);
        }

        function test_aNonInteractiveSwitchIsDimmedAndAnswersNothing() {
            const switcher = make({ interactive: false });
            let reported = false;
            switcher.toggled.connect(function () { reported = true; });

            verify(switcher.opacity < 1.0, "a disabled switch should be dimmed");
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            compare(switcher.checked, false, "a disabled switch should not toggle");
            compare(reported, false);
        }

        /// The keyboard half: a focused switch answers Space, a dimmed one answers nothing.
        function test_spaceFlipsAFocusedSwitch() {
            const switcher = make({});
            host.requestActivate();
            tryVerify(function () { return host.active; });
            switcher.forceActiveFocus();
            tryVerify(function () { return switcher.activeFocus; });
            keyClick(Qt.Key_Space);
            compare(switcher.checked, true, "Space left the switch where it was");

            const inert = make({ interactive: false });
            inert.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(inert.checked, false, "Space flipped a switch that offers nothing to flip");
        }

        /// The screen-reader half: the row's label is the switch's name, and the checked state
        /// travels with it.
        function test_theNameAndTheStateReachTheScreenReader() {
            const switcher = make({ accessibleName: "Animations" });
            compare(switcher.activeFocusOnTab, true);
            compare(switcher.Accessible.role, Accessible.CheckBox);
            compare(switcher.Accessible.name, "Animations");
            compare(switcher.Accessible.checked, false);

            switcher.checked = true;
            compare(switcher.Accessible.checked, true);
        }

        /// The focus ring every button is asked for: nothing without the focus, ink beside it
        /// with the focus.
        function test_theRingArrivesWithTheFocusAndLeavesWithIt() {
            const switcher = make({});
            const ring = Util.findAll(switcher, function (o) {
                return o.border !== undefined && o.border.width === 2;
            })[0];
            verify(ring, "the switch built no focus ring");
            compare(ring.visible, false, "the ring is drawn before anything has the focus");

            host.requestActivate();
            tryVerify(function () { return host.active; });
            switcher.forceActiveFocus();
            tryVerify(function () { return ring.visible; });
        }

        /// A photograph of the ring, for the pixel questions a property read cannot answer.
        function test_theFocusedSwitchPhotographs() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            Tokens.theme = "light";
            const switcher = make({ accessibleName: "Animations" });
            switcher.x = 80;
            switcher.y = 60;
            host.requestActivate();
            tryVerify(function () { return host.active; });
            switcher.forceActiveFocus();
            wait(80);
            verify(Util.saveSnapshot(testCase, host.contentItem, lensQaSnapshotDir, "focus-ring-switch"));
        }
    }
}
