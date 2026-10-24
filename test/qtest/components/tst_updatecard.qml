import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The card a new version raises.
 *
 * What is under test here is the card's own contract: it carries the version, the page and the
 * notes it was shown with, its cross takes it down, and it builds the three handlers those
 * three actions hang on. What each action *writes* is asserted where the document can be read
 * back afterwards -- update_pure_test.cpp drives the duty and reloads the settings file.
 */
Item {
    id: root
    width: 520
    height: 420

    Component {
        id: cardComponent
        UpdateCard {}
    }

    TestCase {
        id: testCase
        name: "UpdateCard"
        when: windowShown

        function opened() {
            const card = createTemporaryObject(cardComponent, root);
            verify(!!card, "Component exists");
            let viewed = 0;
            let skipped = 0;
            card.viewRequested.connect(function () { ++viewed; });
            card.skipRequested.connect(function () { ++skipped; });
            card.show({version: "1.2.0",
                       pageUrl: "https://github.com/kakuyo1/lens/releases/tag/v1.2.0",
                       notes: "Fixed two things."});
            wait(50);
            return {card: card,
                    viewed: function () { return viewed; },
                    skipped: function () { return skipped; }};
        }

        function test_showCarriesTheVersionThePageAndTheNotes() {
            const shown = opened();
            compare(shown.card.version, "1.2.0");
            compare(shown.card.pageUrl, "https://github.com/kakuyo1/lens/releases/tag/v1.2.0");
            compare(shown.card.notes, "Fixed two things.");
            compare(shown.card.visible, true);
            shown.card.visible = false;
        }

        function test_theCrossTakesTheCardDownWithoutAskingForAnything() {
            const shown = opened();
            shown.card.closeCard();
            tryCompare(shown.card, "visible", false);
            compare(shown.viewed(), 0, "the cross opened the release page");
            compare(shown.skipped(), 0, "the cross wrote a skip");
        }

        /// Escape is the cross: the card goes down and nothing is asked for. The card's window
        /// takes no focus, so the shortcut answers application-wide, the way the removal
        /// question's does.
        function test_escapeTakesTheCardDownWithoutAskingForAnything() {
            const shown = opened();
            keyClick(Qt.Key_Escape);
            tryCompare(shown.card, "visible", false);
            compare(shown.viewed(), 0, "Escape opened the release page");
            compare(shown.skipped(), 0, "Escape wrote a skip");
        }

        /// The pill carrying one of the card's action labels, pressed where a reader would press it.
        function pressLabel(card, label) {
            const text = Util.textWith(Util.textsUnder(card), [label]);
            verify(text, "no '" + label + "' label was found");
            const pill = text.parent;
            verify(pill, "the label has no button behind it");
            mouseClick(pill, pill.width / 2, pill.height / 2);
            wait(50);
        }

        function test_viewOpensThePageAndTakesTheCardDown() {
            const shown = opened();
            pressLabel(shown.card, "View");
            compare(shown.viewed(), 1);
            compare(shown.skipped(), 0);
            // The action is reported at once; the window waits for the fade (ShadowCard.qml).
            tryCompare(shown.card, "visible", false);
        }

        function test_skipSignalsWithoutOpeningThePage() {
            const shown = opened();
            pressLabel(shown.card, "Skip this version");
            compare(shown.skipped(), 1);
            compare(shown.viewed(), 0, "Skip opened the release page");
            tryCompare(shown.card, "visible", false);
        }

        /// What the line under the title says for a download state. The states are the duty's,
        /// and the card draws them as words rather than showing what a transfer said.
        function test_theDownloadLineMapsEachStateToItsOwnText() {
            const card = opened().card;
            compare(card.downloadText("downloading", ""), "Downloading...");
            compare(card.downloadText("verifying", ""), "Checking the download...");
            compare(card.downloadText("ready", ""), "Checksum verified. The installer is ready to run.");
            compare(card.downloadText("failed", "generic"), "Download failed. Check your connection.");
            compare(card.downloadText("failed", "checksum"),
                    "The download did not match the published checksum. It was deleted.");
            compare(card.downloadText("failed", "proxy"),
                    "Downloads from GitHub usually need a proxy in mainland China. Turn on your proxy and try again.");
            // Nothing to say, and a state nobody wrote yet.
            compare(card.downloadText("idle", ""), "");
            compare(card.downloadText("something-new", ""), "");
            card.visible = false;
        }

        /// The one primary button: Download while the file can be fetched, Cancel while it is,
        /// a disabled Checking… while the digest is verified, Install once it has been checked,
        /// and nothing when this release cannot be downloaded.
        function test_thePrimaryButtonFollowsTheDownloadState() {
            const card = opened().card;
            compare(card.primaryAction("idle", true), "Download");
            compare(card.primaryAction("idle", false), "", "a release with no checksum is not downloadable");
            compare(card.primaryAction("downloading", true), "Cancel");
            compare(card.primaryAction("ready", true), "Install");
            compare(card.primaryAction("failed", true), "Download", "a failed download can be tried again");
            compare(card.primaryAction("verifying", true), "Checking...",
                    "the checked state has no pill to hold its place");
            card.visible = false;
        }

        /// Only Download and Install wear ink. View is a way out, not the card's main act, so it
        /// takes the secondary recipe: panel2 fill with the ordinary text colour.
        function test_viewIsSecondaryRatherThanInk() {
            const card = opened().card;
            const view = Util.findAll(card, function (o) {
                return o.objectName === "viewButton";
            })[0];
            verify(view, "the card has no View button");
            compare(view.color.toString(), card.tokens.panel2.toString(),
                    "View is still wearing the primary fill");
            const label = Util.textWith(Util.textsUnder(card), ["View"]);
            verify(label, "the View pill has no label");
            compare(label.color.toString(), card.tokens.text.toString(),
                    "View's label is not the ordinary text colour");
            card.visible = false;
        }

        /// The card shows nothing about a download until one is asked for: the controller is
        /// idle in this case, so the line is empty and there is no primary button at all.
        function test_anIdleCardShowsNoDownloadLine() {
            const card = opened().card;
            compare(Controller.updateDuty.download.state, "idle");
            const line = Util.findAll(card, function (object) {
                return object.objectName === "downloadLine";
            })[0];
            verify(line, "the card built no download line");
            compare(line.text, "");
            compare(line.visible, false);
            card.visible = false;
        }

        function test_captureTheCard() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            // A card that fades and lifts into place (ShadowCard.qml), so a grab taken before
            // the token is out photographs a half-transparent surface.
            const settled = Tokens.motion.pop + 80;

            Tokens.theme = "light";
            const light = opened();
            wait(settled);
            verify(Util.saveSnapshot(testCase, light.card.contentItem, lensQaSnapshotDir, "update-card-light"));
            light.card.visible = false;

            Tokens.theme = "dark";
            const dark = opened();
            wait(settled);
            verify(Util.saveSnapshot(testCase, dark.card.contentItem, lensQaSnapshotDir, "update-card-dark"));
            dark.card.visible = false;
        }
    }
}
