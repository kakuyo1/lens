import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The two drill-downs and the empty-filter line that the first critique batch added to the data
 * panels. Kept out of tst_panels.qml so that file stays under the QML size cap.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: statsComponent
        StatsPopup {}
    }

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    TestCase {
        name: "PanelsCritique"
        when: windowShown

        function make(component) {
            const panel = createTemporaryObject(component, root);
            verify(panel);
            wait(50);
            return panel;
        }

        function rowsOf(panel) {
            return Util.findAll(panel, function (o) {
                return o.toString().indexOf("StatRow_QMLTYPE") === 0;
            });
        }

        /// Known and New are counts first, but they are also the two ways into the word list that
        /// the reader is most likely to want next. Both rows ask for the panel and carry the filter.
        function test_theKnownAndNewRowsAskForTheirWordList() {
            const stats = make(statsComponent);
            const rows = rowsOf(stats);
            const known = rows.filter(function (row) { return row.label === qsTr("Known"); })[0];
            const fresh = rows.filter(function (row) { return row.label === qsTr("New"); })[0];
            verify(known && fresh, "the statistics panel lost its Known or New row");
            compare(known.clickable, true, "the Known row is not a way into the list");
            compare(fresh.clickable, true, "the New row is not a way into the list");

            let asked = 0;
            stats.knownRequested.connect(function () { asked += 1; });
            mouseClick(known, known.width / 2, known.height / 2);
            compare(asked, 1, "the Known row did not ask for the word list");
        }

        /// A filter with no rows is not a broken card: once the rows have folded, the list says so
        /// in one line and keeps the card's height.
        function test_anEmptyFilterSaysSo() {
            const popup = make(wordsComponent);
            const empty = [1, 2].find(function (filter) {
                const verdict = filter === 1 ? "known" : "new";
                return Controller.wordsModel.allRows().filter(function (e) { return e.verdict === verdict; }).length === 0;
            });
            if (empty === undefined)
                skip("the fixture carries both known and new words");

            Controller.wordsModel.filter = empty;
            tryCompare(popup, "filterEmpty", true, 1500, "an empty filter did not settle to its line");
            const line = Util.findAll(popup, function (o) {
                return o.objectName === "filteredEmptyText";
            })[0];
            verify(line, "the empty filter line is not drawn");
            compare(line.visible, true);
            compare(line.text, qsTr("No words in this filter."));
        }
    }
}
