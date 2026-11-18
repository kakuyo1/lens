import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util
import "../../../src/app/qml/util/Format.js" as Format

/**
 * The career statistics page (UI-SPEC.md 4.4, docs/adr/0194): the About row opens it, the
 * counters cover the whole log, and the clear action zeroes them. Split out of tst_settings.qml
 * when that file reached the QML size cap (docs/QML.md section 1).
 *
 * This target's profile is a scratch copy, so the presses touch no reader data.
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "SettingsCareer"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// Every engine in the run shares one Controller and one settings document, so a case
        /// that writes one puts it back rather than leaving the next suite reading it.
        function cleanup() {
            Controller.setSetting("theme", "dark");
        }

        function named(scope, name) {
            return Util.findAll(scope, function (o) {
                return o.objectName === name;
            })[0] || null;
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        function categoryRow(panel, title) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SettingsCategoryRow_QMLTYPE") === 0;
            });
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].title === title)
                    return rows[i];
            }
            return null;
        }

        function bringIntoView(item) {
            const body = Util.scrollerOf(item);
            if (body && body.contentHeight > body.height) {
                const bottom = item.mapToItem(body, 0, item.height).y;
                if (bottom > body.height)
                    body.contentY = Math.min(bottom - body.height, body.contentHeight - body.height);
                wait(20);
            }
        }

        function openCategory(panel, title) {
            const row = categoryRow(panel, title);
            verify(row, "the settings catalog has no " + title + " category");
            bringIntoView(row);
            mouseClick(row, row.width / 2, row.height / 2);
            wait(20);
        }

        /// The career statistics page: the About row opens it, the counters cover the whole log
        /// with every counter listed at zero included, and the clear action zeroes them.
        function test_theCareerStatisticsPageCountsAndClears() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "About");

            const row = named(panel, "eventDataRow");
            verify(row, "the About page has no career statistics row");
            const size = Util.textWith(Util.textsUnder(row),
                                       [Format.formatBytes(Controller.telemetryDuty.eventBytes)]);
            verify(size, "the career statistics row carries no size");
            mouseClick(row, row.width / 2, row.height / 2);
            wait(25);
            compare(panel.route, "career", "the row did not take the career route");

            const page = Util.ofType(panel, "SettingsCareerPage");
            verify(page, "the career route did not load its page");

            // The page flushes the queue when it opens, so the startup event is counted.
            const starts = named(page, "careerValue.starts");
            verify(starts, "the page has no launch counter");
            verify(Number(starts.text.replace(/,/g, "")) >= 1,
                   "the launch counter did not count the startup event");
            verify(named(page, "careerValue.events"), "the page has no event counter");
            // Every counter is listed even at zero: the OCR and the last model row among them.
            verify(named(page, "careerValue.ocr.captures"), "the OCR counter is not listed");
            verify(named(page, "careerValue.llm.example"), "the last model counter is not listed");

            // A verdict made now reaches the counters on refresh. "new" on an unmarked word
            // records the event without writing a mark, so the shared document keeps its state.
            for (let i = 0; i < 14; ++i)
                Controller.explanationDuty.markVia("zzprobe" + i, "new", "words");
            const refresh = named(page, "refreshCareerButton");
            verify(refresh, "the page has no refresh action");
            mouseClick(refresh, refresh.width / 2, refresh.height / 2);
            wait(25);
            const resets = named(page, "careerValue.verdict.new");
            verify(resets, "the page has no reset-to-new counter");
            verify(Number(resets.text.replace(/,/g, "")) >= 14,
                   "the recorded verdicts did not reach the counters");

            // What the table looks like with a career in it, and once cleared: the offscreen
            // snapshot path (docs/qa.md), skipped when no snapshot dir was asked for. The whole
            // card, one viewport at a time: grabImage crops in window coordinates.
            Controller.setSetting("theme", "light");
            // The palette crosses over the theme fade; photograph it settled.
            wait(25 + Tokens.motion.theme);
            Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "career-stats-light");
            const body = Util.scrollerOf(page);
            body.contentY = 600;
            wait(25);
            Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "career-stats-mid");
            body.contentY = 0;
            wait(25);

            // The clear sits below the fold of a table this long; bring it up before pressing.
            const button = named(page, "clearEventDataButton");
            verify(button, "the page carries no clear action");
            Util.reveal(button);
            wait(25);
            mouseClick(button, button.width / 2, button.height / 2);
            const question = Util.ofType(panel, "ConfirmDialog");
            verify(question, "the clear action did not ask");
            compare(question.visible, true, "the question is not up");
            compare(Controller.telemetryDuty.eventBytes === 0, false, "the question cleared first");
            const confirm = Util.findAll(question, function (o) {
                return o.objectName === "confirmButton";
            })[0];
            verify(confirm, "the question has no Confirm");
            mouseClick(confirm, confirm.width / 2, confirm.height / 2);
            tryVerify(() => Controller.telemetryDuty.eventBytes === 0, 2000, "the clear action did not empty the event data");
            wait(25);
            compare(named(page, "careerValue.events").text, "0", "the event counter did not zero");
            compare(named(page, "careerValue.starts").text, "0", "the launch counter did not zero");
            const hint = named(page, "careerEmptyHint");
            verify(hint, "the page has no empty-state hint");
            compare(hint.visible, true, "the empty hint did not appear once the log was cleared");
            Util.saveSnapshot(testCase, panel.contentItem, lensQaSnapshotDir, "career-stats-zero");
        }
    }
}