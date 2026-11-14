import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The notice surface for failed requests.
 *
 * The controller owns the payload; this case exercises the surface contract with the failure
 * families that can actually reach it. Every current path is an error: network, key, and schema
 * failures. A failed selection grab is dropped silently -- a popup per miss was a nag -- and the
 * three channels always give a selection somewhere to go, so a "no available channel" info notice
 * does not exist. The daily budget event is a phase-two placeholder and has no
 * production payload or fixture here.
 */
Item {
    id: root
    width: 520
    height: 420

    Component {
        id: noticeComponent
        Notice {}
    }

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        id: testCase
        name: "Notice"
        when: windowShown

        function opened(title, body, kind) {
            const notice = createTemporaryObject(noticeComponent, root);
            verify(!!notice, "Component exists");
            notice.show({title: title, body: body, kind: kind});
            wait(50);
            return notice;
        }

        function test_reachableFailureFamiliesRenderTheNoticeContract() {
            const failures = [
                { title: "Network", body: "The request could not reach the service.", kind: "error" },
                { title: "Schema", body: "The explanation response was invalid.", kind: "error" },
                { title: "API key", body: "The API key is missing.", kind: "error" }
            ];

            for (let i = 0; i < failures.length; ++i) {
                const notice = opened(failures[i].title, failures[i].body, failures[i].kind);
                compare(notice.noticeTitle, failures[i].title);
                compare(notice.noticeBody, failures[i].body);
                compare(notice.noticeKind, failures[i].kind);
                compare(notice.visible, true);
                notice.visible = false;
                notice.destroy();
            }
        }

        function test_closeDoesNotDismissAnotherSurface() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(!!bubble, "Component exists");
            bubble.show({title: "profile", type: "word", ipa: "", en: "profile", zh: "", status: "new", x: 220, y: 260});

            const notice = opened("Network", "The request failed.", "error");
            notice.closeNotice();
            // The close starts the fade; the window goes when it has run (ShadowCard.qml).
            tryCompare(notice, "visible", false);
            compare(bubble.visible, true);

            bubble.visible = false;
        }

        function test_theClosePathHidesTheWindow() {
            const notice = opened("Network", "The request could not reach the service.", "error");
            notice.closeNotice();
            tryCompare(notice, "visible", false);
        }

        function test_aLongTitleLeavesTheCloseControlInsideTheCard() {
            const notice = opened(
                "The first command of a session auto-starts the browser daemon, and that daemon inherits the command's stdout while living up to an hour.",
                "The request could not reach the service.",
                "error");
            const title = Util.textWith(Util.textsUnder(notice), [notice.noticeTitle]);
            const close = Util.findAll(notice, function (object) {
                return object.source === "qrc:/icons/ui-close.svg";
            })[0];

            verify(title);
            verify(close);
            verify(title.width <= close.x - 12);
            verify(close.x + close.width <= notice.cardWidth - 20);
            compare(close.visible, true);

            notice.visible = false;
        }

        /// A press on the action pill raises actionTriggered for Main to route: the action explains
        /// the notice's word, and there is nothing else a card's pill can do.
        function test_theActionPillPressesThrough() {
            const notice = createTemporaryObject(noticeComponent, root);
            verify(!!notice, "Component exists");
            notice.show({title: "Word", body: "No explanation is stored.", kind: "info",
                         action: "Explain now", lemma: "panel"});
            wait(50);

            let pressed = 0;
            notice.actionTriggered.connect(function () { ++pressed; });
            const label = Util.textWith(Util.textsUnder(notice), ["Explain now"]);
            verify(label, "the action pill was not drawn");
            mouseClick(label.parent, label.parent.width / 2, label.parent.height / 2);
            wait(50);
            compare(pressed, 1, "pressing the action did not raise actionTriggered");
        }

        /// The crash notice is the one with two pills: the dumps folder and the issue prefill
        /// (UI-SPEC.md 4.5). Each pill raises its own signal for Main to route, and the two act on
        /// artifacts rather than a word.
        function test_theCrashNoticePressesThroughBothPills() {
            const notice = createTemporaryObject(noticeComponent, root);
            verify(!!notice, "Component exists");
            notice.show({title: "Crash", body: "The previous run crashed.", kind: "crash",
                         action: "Open dump folder", secondaryAction: "Report issue"});
            wait(50);

            let opened = 0;
            let reported = 0;
            notice.actionTriggered.connect(function () { ++opened; });
            notice.secondaryActionTriggered.connect(function () { ++reported; });
            const openPill = Util.textWith(Util.textsUnder(notice), ["Open dump folder"]);
            const reportPill = Util.textWith(Util.textsUnder(notice), ["Report issue"]);
            verify(openPill, "the open-folder pill was not drawn");
            verify(reportPill, "the report pill was not drawn");
            mouseClick(openPill.parent, openPill.parent.width / 2, openPill.parent.height / 2);
            wait(50);
            compare(opened, 1, "pressing the first pill did not raise actionTriggered");
            mouseClick(reportPill.parent, reportPill.parent.width / 2, reportPill.parent.height / 2);
            wait(50);
            compare(reported, 1, "pressing the second pill did not raise secondaryActionTriggered");

            notice.visible = false;
        }

        /// Every other notice has at most one action, so a payload without a second draws one pill
        /// and no second press exists to press.
        function test_aNoticeWithoutASecondActionDrawsOnePill() {
            const notice = createTemporaryObject(noticeComponent, root);
            verify(!!notice, "Component exists");
            notice.show({title: "Word", body: "No explanation is stored.", kind: "info",
                         action: "Explain now"});
            wait(50);

            const openPill = Util.textWith(Util.textsUnder(notice), ["Explain now"]);
            verify(openPill, "the action pill was not drawn");
            const extras = Util.findAll(notice, function (object) {
                return object.visible !== undefined && object.visible
                    && object.label !== undefined && object.label !== "";
            });
            compare(extras.length, 1, "a second pill was drawn without a second action");

            notice.visible = false;
        }

        function test_captureTheNotice() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            // Every surface is a card that fades and lifts into place (ShadowCard.qml), so a
            // grab taken before that has finished photographs a half-transparent card. Waiting
            // out the token is what makes the still show what the reader ends up looking at.
            const settled = Tokens.motion.pop + 80;

            Tokens.theme = "light";
            const notice = opened("API key", "The API key is missing.", "error");
            wait(settled);
            verify(Util.saveSnapshot(testCase, notice.contentItem, lensQaSnapshotDir, "notice-light"));
            notice.visible = false;

            Tokens.theme = "dark";
            notice.show({title: "Network", body: "The request could not reach the service.", kind: "error"});
            wait(settled);
            verify(Util.saveSnapshot(testCase, notice.contentItem, lensQaSnapshotDir, "notice-dark"));
            notice.visible = false;
        }
    }
}
