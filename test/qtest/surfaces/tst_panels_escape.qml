import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The panels' Escape paths, split out of tst_panels.qml because that file sits at the QML size
 * cap (docs/QML.md section 1).
 *
 * The shortcut is application-wide rather than window-scoped, for the reason the removal
 * question's own Shortcut gives: the offscreen platform never gives a window the active state,
 * so a window-scoped key cannot be exercised there. The handover between the words panel and
 * the question it raises is the interesting edge: the question takes the key while it stands,
 * and the panel takes it back once the question is on its way out.
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
        id: costComponent
        CostPopup {}
    }

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    Component {
        id: yearWordsComponent
        YearWordsPopup {}
    }

    TestCase {
        name: "PanelsEscape"
        when: windowShown

        function make(component) {
            const panel = createTemporaryObject(component, root);
            verify(panel);
            wait(50);
            return panel;
        }

        /// @return The row whose word is @p word.
        function rowFor(popup, word) {
            const text = Util.textWith(Util.textsUnder(popup), [word]);
            return text ? text.parent : null;
        }

        /// @return The delete glyph at the end of @p row.
        function crossFor(row) {
            const cross = Util.findAll(row, function (o) {
                return typeof o.source === "string" && o.source.indexOf("ui-close") >= 0;
            })[0];
            verify(cross, "the row draws no delete glyph");
            return cross;
        }

        /// @brief Open the question on @p word through the row's own cross.
        function askAbout(popup, word) {
            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            mouseClick(crossFor(row), 10, 10);
            compare(popup.pendingRemoval, word, "the cross did not name the word it belongs to");
        }

        /// @return The window the question is drawn in, reached through an item that lands in it.
        function questionWindow(popup) {
            const heading = Util.textWith(Util.textsUnder(popup), [qsTr("Remove word")]);
            verify(heading, "the question is not drawn");
            return heading.Window.window;
        }

        /// Escape is the cross on each of the three data panels. The words panel is the one
        /// with a question to answer first: while it is up, its own shortcut takes the key and
        /// the panel stands -- the same order SettingsPopup's backing-out takes.
        function test_escapePutsEachPanelDown() {
            const stats = make(statsComponent);
            stats.visible = true;
            keyClick(Qt.Key_Escape);
            tryCompare(stats, "visible", false, 1000, "Escape left the statistics panel up");

            const cost = make(costComponent);
            cost.visible = true;
            keyClick(Qt.Key_Escape);
            tryCompare(cost, "visible", false, 1000, "Escape left the cost panel up");

            const popup = make(wordsComponent);
            popup.visible = true;
            askAbout(popup, "counterrevolutionary");
            keyClick(Qt.Key_Escape);
            compare(popup.pendingRemoval, "", "Escape did not put the question down");
            compare(popup.visible, true, "the question's Escape also closed the panel under it");

            // The question is modal to the panel until its fade is done, so the panel's own
            // Escape waits it out: a press while the question is still on screen belongs to it.
            tryCompare(questionWindow(popup), "visible", false, 2000,
                       "the question's fade never ended");

            keyClick(Qt.Key_Escape);
            tryCompare(popup, "visible", false, 1000, "Escape left the words panel up");
        }

        /// The year panel answers Escape twice: the palette goes first, the card after it.
        function test_escapeAnswersTheYearPanelsPaletteBeforeThePanel() {
            const popup = make(yearWordsComponent);
            popup.visible = true;
            wait(50);

            popup.paletteOpen = true;
            wait(50);
            keyClick(Qt.Key_Escape);
            compare(popup.paletteOpen, false, "Escape left the palette out");
            compare(popup.visible, true, "the palette's Escape also took the card down");

            keyClick(Qt.Key_Escape);
            tryCompare(popup, "visible", false, 1000, "Escape left the year panel up");
        }
    }
}
