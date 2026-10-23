import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util
import "../../../src/app/qml/util/Format.js" as Format

/**
 * The update block on the About page: the button, the line that answers it, and the version it
 * names. The switch that says whether the check happens without being asked for stays on the
 * General page, and is checked here only by the case that reads it.
 *
 * The line is under test as the mapping it is: five states, five texts, nothing else. That is
 * reachable offline because the page reads the text out of a function of the state, so the case
 * can hand that function every state the controller has. What is not asserted here is that a
 * press on the button starts a check -- that would put a real request to the release source into
 * a suite that deliberately keeps itself off the network (setup.cpp's OfflineManager), so the
 * wiring is read rather than driven; see the case that says so by name.
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: pageComponent
        SettingsAboutPage { width: 340 }
    }

    Component {
        id: generalComponent
        SettingsGeneralPage { width: 340 }
    }

    TokenHost { id: host }

    TestCase {
        id: testCase
        name: "SettingsUpdate"
        when: windowShown

        function make(component) {
            const page = createTemporaryObject(component ? component : pageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            verify(!!page, "Component exists");
            wait(50);
            return page;
        }

        function textByName(page, name) {
            return Util.findAll(page, function (object) {
                return object.objectName === name;
            })[0];
        }

        /// The line reads five states and nothing else: no notes, no page, no error text.
        function test_theStatusLineMapsEveryStateToItsOwnText() {
            const page = make();
            compare(page.updateStatusText("checking"), "Checking...");
            compare(page.updateStatusText("offline"), "Cannot connect");
            // The two that name a version take it off the controller's own answer.
            compare(page.updateStatusText("upToDate"),
                    "Up to date (version " + Controller.updateDuty.update.current + ")");
            compare(page.updateStatusText("available"),
                    "Version " + Controller.updateDuty.update.latest + " is available");
            // Idle has nothing to say, and so has a state nobody has written yet.
            compare(page.updateStatusText("idle"), "");
            compare(page.updateStatusText("something-new"), "");
            page.destroy();
        }

        /// Nothing is checked while idle, so the line is empty and the button is live.
        function test_idleSaysNothingAndLeavesTheButtonPressable() {
            const page = make();
            compare(Controller.updateDuty.update.state, "idle", "this case assumes no check has run");
            compare(textByName(page, "updateStatusLine").text, "");
            compare(textByName(page, "updateStatusLine").visible, false);
            compare(textByName(page, "checkForUpdatesButton").opacity, 1);

            const handler = Util.findAll(textByName(page, "checkForUpdatesButton"),
                                         function (object) {
                                             return object.toString().indexOf("QQuickTapHandler") === 0;
                                         })[0];
            verify(handler, "the button built no press handler");
            compare(handler.enabled, true, "the button was dead before anything was asked of it");
            page.destroy();
        }

        /// The lock and the line agree on the same function, so the button cannot be disabled
        /// in a state the line would still call finished.
        function test_onlyCheckingLocksTheButton() {
            const page = make();
            compare(page.updateBusy("checking"), true);
            for (const state of ["idle", "upToDate", "available", "offline"])
                compare(page.updateBusy(state), false, state + " should not read as busy");
            compare(page.updatesBusy, false, "the controller is idle in this case");
            page.destroy();
        }

        function test_theBlockSaysWhatItIs() {
            const page = make();
            const labels = Util.textsUnder(page).map(function (t) { return t.text; });
            verify(labels.indexOf("Check for updates") >= 0, "the button carries no label");
            verify(labels.indexOf("GitHub repository") >= 0, "the repository row carries no label");
            page.destroy();
        }

        /// The version the check compares against is on the page, and the footer carries the build
        /// day in the form the controller stamps: yyyy-MM-dd, on every machine.
        function test_theVersionAndBuildDateAreShown() {
            const page = make();
            const labels = Util.textsUnder(page).map(function (t) { return t.text; });
            verify(labels.indexOf("Current version " + Controller.updateDuty.update.current) >= 0,
                   "the version line is missing");
            verify(/^\d{4}-\d{2}-\d{2}$/.test(Controller.buildDate),
                   "the build date is not yyyy-MM-dd: " + Controller.buildDate);
            verify(labels.indexOf("Built " + Controller.buildDate) >= 0, "the footer carries no build date");
            page.destroy();
        }

        /// The picture cache line names the one cache the reader may drop, shows what it holds, and
        /// carries the action. The press is read rather than driven: the controller's cache sits in
        /// the reader's own profile, and this suite must not empty it.
        function test_thePictureCacheRowReportsItsSizeAndCarriesAClearAction() {
            const page = make();
            compare(Format.formatBytes(0), "0 B");
            compare(Format.formatBytes(512), "512 B");
            compare(Format.formatBytes(1536), "1.5 KB");

            const row = Util.findAll(page, function (object) {
                return object.objectName === "pictureCacheRow";
            })[0];
            verify(row, "the About page has no picture cache row");

            const labels = Util.textsUnder(row).map(function (text) { return text.text; });
            verify(labels.indexOf("Picture cache") >= 0, "the row does not name the cache");

            const size = textByName(page, "pictureCacheSize");
            verify(size, "the row carries no size");
            compare(size.text, Format.formatBytes(Controller.pictureDuty.cacheBytes));

            const button = textByName(page, "clearPictureCacheButton");
            verify(button, "the row carries no clear action");
            const handler = Util.findAll(button, function (object) {
                return object.toString().indexOf("QQuickTapHandler") === 0;
            })[0];
            verify(handler, "the clear action built no press handler");
            page.destroy();
        }

        /// The switch reads and writes the controller's own value, so what the reader toggles
        /// is the setting the startup check reads.
        function test_theSwitchIsBoundToTheStartupCheck() {
            const page = make(generalComponent);
            const rows = Util.findAll(page, function (object) {
                return typeof object.toggled === "function" && object.label !== undefined;
            });
            const row = rows.filter(function (candidate) {
                return candidate.label === "Check for updates at startup";
            })[0];
            verify(row, "no startup switch was found");
            compare(row.checked, Controller.updateDuty.autoCheck);
            compare(row.interactive, true);
            page.destroy();
        }

        function test_captureTheUpdateBlock() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            const page = make();

            // The page has no background of its own: in the panel it sits on the card's surface, so
            // the snapshot has to stand on the same colour rather than the host window's white.
            Tokens.theme = "light";
            host.color = host.tokens.panel;
            verify(Util.saveSnapshot(testCase, page, lensQaSnapshotDir, "settings-update-light"));

            Tokens.theme = "dark";
            host.color = host.tokens.panel;
            verify(Util.saveSnapshot(testCase, page, lensQaSnapshotDir, "settings-update-dark"));
        }
    }
}
