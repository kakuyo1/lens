import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The week's report card and its story page: what opens the card, the height it keeps on either page, and
 * what closes it. Split out of tst_main.qml so that file stays under the QML size cap.
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
        name: "Report"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// Util.ofType reaches a panel that Main builds without an id.
        function panel(main, typeName) {
            const found = Util.ofType(main, typeName);
            verify(found, "Main did not build a " + typeName);
            return found;
        }

        /// The profile hands the reading to the week's report card, which is modal: a press outside it takes nothing
        /// down, and its own close control does.
        function test_theWeekReportOpensFromTheProfileAndOnlyItsOwnControlsCloseIt() {
            const main = make();
            const profile = panel(main, "ReadingProfilePopup");
            const report = panel(main, "ReportCard");

            Util.surfaceHost(main).showProfile();
            profile.reportRequested();
            compare(report.visible, true, "the report card did not open");
            tryCompare(profile, "visible", false, 1000, "the profile panel stayed up behind the report card");

            Util.surfaceHost(main).dismissOutside(Qt.point(0, 0));
            compare(report.visible, true, "a press outside the report card closed it");

            report.dismiss();
            tryCompare(report, "visible", false);
        }

        /// The story page takes the report's place at the report's height, so the card keeps its size whichever page it
        /// shows; the back arrow returns to the report.
        function test_theStoryPageKeepsTheCardHeightAndReturnsToTheReport() {
            const main = make();
            const report = panel(main, "ReportCard");
            const card = function () {
                return Util.findAll(report.contentItem, function (item) {
                    return typeof item.shown === "boolean" && typeof item.dragging === "boolean";
                })[0];
            };

            Util.surfaceHost(main).showReport();
            tryCompare(card(), "opacity", 1);
            const height = card().height;

            // The page is switched directly: openStory() would ask the service for a story.
            report.page = "story";
            wait(50);
            compare(card().height, height, "the story page changed the card's height");

            report.backToReport();
            compare(report.page, "report", "the back arrow did not return to the report");
            report.dismiss();
            tryCompare(report, "visible", false);
        }

        /// The report card is never taller than the screen less a 24 px scrim margin above and below. Content that does
        /// not fit scrolls inside the card instead of pushing it off the screen.
        function test_theReportCardIsCappedAtTheScreenAndScrolls() {
            const main = make();
            const report = panel(main, "ReportCard");
            const card = function () {
                return Util.findAll(report.contentItem, function (item) {
                    return typeof item.shown === "boolean" && typeof item.dragging === "boolean";
                })[0];
            };
            const scroll = Util.findAll(report.contentItem, function (item) {
                return typeof item.interactive === "boolean" && typeof item.contentHeight === "number";
            })[0];

            Util.surfaceHost(main).showReport();
            tryCompare(card(), "opacity", 1);

            report.height = 300;
            wait(50);
            compare(card().height, 300 - 48 + 2 * 26, "the card is not capped at the screen less the scrim margin");
            verify(scroll.interactive, "the report content does not scroll inside the capped card");

            report.dismiss();
            tryCompare(report, "visible", false);
        }
    }
}
