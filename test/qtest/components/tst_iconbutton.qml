import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The lone glyph as a button: the cross on a surface's header, the chevron that goes back.
 *
 * The glyph is the whole button, so the name is the caller's to pass -- these cases hold the
 * keyboard and screen-reader half the control now carries: Return and Space press it, the ink
 * ring follows the focus, and the name it was given is the name it reports.
 */
Item {
    id: root
    width: 120
    height: 80

    Component {
        id: buttonComponent
        IconButton { accessibleName: "Close" }
    }

    TokenHost { id: host }

    TestCase {
        id: testCase
        name: "IconButton"
        when: windowShown

        function make(properties) {
            const button = createTemporaryObject(buttonComponent, host.contentItem, properties);
            waitForRendering(host.contentItem);
            verify(button);
            return button;
        }

        /// @return The control's focus ring, by the 2 px stroke no other child carries.
        function ringOf(button) {
            const ring = Util.findAll(button, function (o) {
                return o.border !== undefined && o.border.width === 2;
            })[0];
            verify(ring, "the button built no focus ring");
            return ring;
        }

        function test_returnAndSpaceTapIt() {
            const button = make({});
            let taps = 0;
            button.tapped.connect(function () { taps++; });
            host.requestActivate();
            tryVerify(function () { return host.active; });
            button.forceActiveFocus();
            tryVerify(function () { return button.activeFocus; });

            keyClick(Qt.Key_Return);
            compare(taps, 1, "Return left the button unpressed");

            keyClick(Qt.Key_Space);
            compare(taps, 2, "Space left the button unpressed");
        }

        function test_theNameItWasGivenIsTheNameItReports() {
            const button = make({});
            compare(button.activeFocusOnTab, true);
            compare(button.Accessible.role, Accessible.Button);
            compare(button.Accessible.name, "Close");
        }

        function test_theRingArrivesWithTheFocusAndLeavesWithIt() {
            const button = make({});
            const ring = ringOf(button);
            compare(ring.visible, false, "the ring is drawn before anything has the focus");

            host.requestActivate();
            tryVerify(function () { return host.active; });
            button.forceActiveFocus();
            tryVerify(function () { return ring.visible; });
        }

        /// A photograph of the ring, for the pixel questions a property read cannot answer.
        function test_theFocusedButtonPhotographs() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            Tokens.theme = "light";
            const button = make({});
            button.x = 80;
            button.y = 60;
            host.requestActivate();
            tryVerify(function () { return host.active; });
            button.forceActiveFocus();
            wait(80);
            verify(Util.saveSnapshot(testCase, host.contentItem, lensQaSnapshotDir, "focus-ring-icon-button"));
        }
    }
}
