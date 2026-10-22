import QtQuick
import QtTest
import Lens

/**
 * The controller's own state, driven straight from the surface target.
 *
 * setup.cpp builds the Controller the way main() does -- minus the mouse hook and the tray icon
 * -- so this target is the only one that can call it at all: it is a QML singleton, and its
 * constructor wants a store, a client and a hook that no unit test has. What was checked here
 * before was how the surfaces are laid out from that state; these cases check the state itself,
 * which is the half a photograph cannot see.
 *
 * The entry point that turns a gesture into a selection is not reachable this way -- it is not
 * Q_INVOKABLE and the hook is not exposed to QML -- so beginSelection() and everything below it
 * stay with the integration suite and the real desktop.
 */
Item {
    id: root
    width: 400
    height: 400

    TestCase {
        name: "ControllerState"
        when: windowShown

        /// The export's three scopes are the words popup's own filter, and anything else has to
        /// write nothing rather than a whole list by accident.
        function test_theExportCoversEveryScopeAndRefusesAnyOther() {
            const all = Controller.exportWords("all");
            verify(all.length > 0, "the fixture needs at least one word to export");
            compare(Controller.exportWords("nonsense"), "", "an unknown scope should export nothing");

            const byLemma = {};
            Controller.words.forEach(function (row) { byLemma[row.word] = row.verdict; });

            // Every line is one of the words the list holds, and the known / new scopes narrow it
            // to the rows carrying that verdict. Empty lines are the text's trailing break.
            all.split("\n").forEach(function (line) {
                if (line.length === 0) return;
                verify(byLemma[line] !== undefined, "the export carries a word the list does not: " + line);
            });
            Controller.exportWords("known").split("\n").forEach(function (line) {
                if (line.length > 0) compare(byLemma[line], "known");
            });
            Controller.exportWords("new").split("\n").forEach(function (line) {
                if (line.length > 0) compare(byLemma[line], "new");
            });
        }

        function test_theYearProjectionHas365OrderedZeroFilledDays() {
            const days = Controller.yearDays;
            compare(days.length, 365);
            compare(days[0].date.length, 10);
            compare(days[364].date.length, 10);
            verify(days[0].date < days[364].date);
            verify(days.every(function (day) {
                return typeof day.date === "string" && typeof day.pops === "number"
                    && typeof day.learned === "number" && typeof day.fresh === "number";
            }));
            verify(days.some(function (day) { return day.pops === 0; }));
        }

        /// The controller hands the words over as raw values: the day's relation and the date and
        /// time apart, with the label drawn by WordsPopup. The fixture is all January, so every
        /// dated row is "earlier" here.
        function test_theWordRowsCarryRawDayAndTimeNotALabel() {
            const rows = Controller.words;
            verify(rows.length > 0, "the fixture needs at least one word");
            verify(rows.every(function (row) {
                return ["today", "yesterday", "earlier", ""].indexOf(row.day) >= 0
                    && !("when" in row) && !("status" in row);
            }), "a row carries a day outside today / yesterday / earlier, or a formatted label");
            verify(rows.some(function (row) {
                return row.day === "earlier" && /^[0-9]{2}-[0-9]{2}$/.test(row.date) && /^[0-9]{2}:[0-9]{2}$/.test(row.time);
            }), "no dated row carries its date and time apart");
        }

        /// An action arrives from the bar the reader is looking at; with no selection behind it
        /// there is nothing to act on, and answering would paint a card about nothing.
        function test_anActionWithNoSelectionPendingIsIgnored() {
            let bars = 0;
            Controller.selectionBarRequested.connect(function () { bars += 1; });

            Controller.explanationDuty.runSelectionAction("translate", "ubiquitous");
            Controller.explanationDuty.runSelectionAction("copy", "ubiquitous");
            Controller.explanationDuty.runSelectionAction("explain", "ubiquitous");

            compare(bars, 0, "an action with nothing pending put an action bar up");
            compare(Object.keys(Controller.explanationDuty.bubble).length, 0, "an action with nothing pending opened a bubble");
        }

        /// Dismissing a surface that is not up must not tell the surface anything -- the signal
        /// is what closes a window, and emitting it with nothing up is a change nobody made.
        function test_dismissingWhatIsNotUpSaysNothing() {
            let bubbles = 0;
            let notices = 0;
            Controller.explanationDuty.bubbleChanged.connect(function () { bubbles += 1; });
            Controller.explanationDuty.noticeChanged.connect(function () { notices += 1; });

            verify(Object.keys(Controller.explanationDuty.bubble).length === 0, "the fixture should start with no bubble");
            verify(Object.keys(Controller.explanationDuty.notice).length === 0, "the fixture should start with no notice");
            Controller.explanationDuty.dismissBubble();
            Controller.explanationDuty.dismissNotice();

            compare(bubbles, 0, "dismissing an empty bubble told the surface");
            compare(notices, 0, "dismissing an empty notice told the surface");
        }

        /// The policy is a two-way switch at the QML boundary; the stored key vocabulary stays in
        /// C++, so a third value cannot be written by a surface that only knows the switch.
        function test_theClipboardPolicyIsATwoWaySwitch() {
            Controller.setSetting("clipboardPolicy", "silent");
            compare(Controller.settings.clipboardPolicy === "silent", true);

            Controller.setSetting("clipboardPolicy", "topmost");
            compare(Controller.settings.clipboardPolicy === "silent", false);
        }

        /// The display time is stored as whole seconds and kept between one and thirty; an out-of-range
        /// value lands on the nearer edge. A placement that is neither side is refused, as the policy is.
        function test_theBubbleSettingsClampAndRefuseWhatTheyCannotUse() {
            Controller.setSetting("bubbleDismissSeconds", 12);
            compare(Controller.settings.bubbleDismissSeconds, 12);

            Controller.setSetting("bubbleDismissSeconds", 0);
            compare(Controller.settings.bubbleDismissSeconds, 1, "a zero display time was stored");

            Controller.setSetting("bubbleDismissSeconds", 99);
            compare(Controller.settings.bubbleDismissSeconds, 30, "an oversized display time was stored");

            Controller.setSetting("bubblePlacement", "below");
            compare(Controller.settings.bubblePlacement, "below");

            Controller.setSetting("bubblePlacement", "nonsense");
            compare(Controller.settings.bubblePlacement, "below", "an unknown placement was written anyway");

            Controller.setSetting("bubblePlacement", "above");
            Controller.setSetting("bubbleDismissSeconds", 5);
            compare(Controller.settings.bubblePlacement, "above");
            compare(Controller.settings.bubbleDismissSeconds, 5);
        }

        /// The typing pace is a level between one and five; an out-of-range value lands on the nearer
        /// edge, and the pace the reveal reads follows the level. The exact milliseconds are the
        /// header's own contract (lens_gtest_unit); this reads the wiring to the facade.
        function test_theRevealSpeedClampsAndDrivesThePace() {
            Controller.setSetting("revealSpeed", 2);
            compare(Controller.settings.revealSpeed, 2);
            const paced = Controller.settings.revealCharacterMs;

            Controller.setSetting("revealSpeed", 4);
            verify(Controller.settings.revealCharacterMs < paced, "a faster speed did not shorten the pace");

            Controller.setSetting("revealSpeed", 0);
            compare(Controller.settings.revealSpeed, 1, "a speed below the range was stored");
            Controller.setSetting("revealSpeed", 99);
            compare(Controller.settings.revealSpeed, 5, "a speed above the range was stored");

            Controller.setSetting("revealSpeed", 3);
            compare(Controller.settings.revealSpeed, 3);
        }

        /// Automatic scanning is a phase-1 placeholder, but the label the tray menu and tooltip
        /// both read still has to follow it rather than disagree with itself.
        function test_theModeLabelFollowsAutoScan() {
            Controller.captureDuty.setAutoScan(false);
            compare(Controller.modeLabel, qsTr("Manual"));

            Controller.captureDuty.setAutoScan(true);
            compare(Controller.modeLabel, qsTr("Auto"));

            Controller.captureDuty.setAutoScan(false);
        }

        /// The key is write-only: the document carries it, nothing reads it back to a surface.
        function test_theApiKeyIsWriteOnly() {
            compare(Controller.settings.hasApiKey, false, "the fixture should hold no key");

            Controller.setApiKey("sk-not-a-real-key");
            compare(Controller.settings.hasApiKey, true);

            // Nothing echoes it: the settings map carries only whether a key exists, never a
            // value under a name that spells the key itself.
            const names = Object.keys(Controller.settings);
            verify(names.indexOf("hasApiKey") >= 0, "the settings map should say whether a key exists");
            compare(names.indexOf("API-KEY"), -1);
            compare(names.indexOf("API_KEY"), -1);
            compare(names.indexOf("apiKey"), -1);

            Controller.setApiKey("");
            compare(Controller.settings.hasApiKey, false, "clearing the key left it behind");
        }

        function test_theCursorPositionIsARealPoint() {
            const at = Controller.cursorPos();
            compare(typeof at.x, "number");
            compare(typeof at.y, "number");
        }
    }
}
