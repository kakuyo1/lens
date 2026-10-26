import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The annual word record: its grid, the legend bands and the hue palette. Split out of
 * tst_panels.qml so that file stays under the QML size cap.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: yearWordsComponent
        YearWordsPopup {}
    }

    TestCase {
        id: testCase
        name: "PanelsYear"
        when: windowShown

        function make() {
            const panel = createTemporaryObject(yearWordsComponent, root);
            verify(panel);
            wait(50);
            return panel;
        }

        function test_theYearWordsPanelProjectsTheControllerDaysAndPalette() {
            const popup = make();
            popup.visible = true;
            wait(50);

            compare(popup.dataCellCount, Controller.yearDays.length);
            compare(popup.dataCellCount, 365);
            compare(popup.gridCellCount, 371);
            verify(Controller.yearDays.some(function (day) { return day.pops === 0; }));
            const compactHeight = popup.height;

            const monthLabels = Util.findAll(popup, function (item) {
                return item.objectName === "yearMonthLabel";
            }).sort(function (left, right) {
                return left.x - right.x;
            });
            for (let i = 1; i < monthLabels.length; ++i) {
                verify(monthLabels[i - 1].x + monthLabels[i - 1].width <= monthLabels[i].x);
            }

            const thresholds = Util.findAll(popup, function (item) {
                return item.objectName === "yearThresholds";
            })[0];
            verify(thresholds, "the year legend has no lookup bands");
            compare(thresholds.text, qsTr("1 · 2–3 · 4–5 · 6+ lookups"));

            const selectedDay = Controller.yearDays.find(function (day) { return day.pops > 0; });
            const selectedCell = Util.findAll(popup, function (item) {
                return item.day && item.day.date === selectedDay.date;
            })[0];
            verify(selectedCell, "the fixture has no cell for a day with lookups");
            mouseClick(selectedCell, selectedCell.width / 2, selectedCell.height / 2);
            compare(popup.selectedDate, selectedDay.date);
            const selectedDayText = qsTr("%1: %2 lookups").arg(selectedDay.date).arg(selectedDay.pops);
            verify(Util.textWith(Util.textsUnder(popup), [selectedDayText]),
                   "the selected day's lookup count is not shown as text");

            popup.openPalette(3);
            tryCompare(popup, "paletteOpen", true);
            tryCompare(popup, "paletteReady", true);
            compare(popup.selectedLevel, 3);

            const named = function (name) { return Util.findAll(popup, function (item) { return item.objectName === name; })[0]; };
            const wheel = named("yearHueWheel");
            const shades = named("yearShadeColumn");
            const title = named("yearPaletteTitle");
            const swatch = named("yearShadeSwatch");
            verify(wheel && shades && title && swatch);
            verify(wheel.x + wheel.width <= shades.x);
            // The four levels sit on the wheel's horizontal midline.
            compare(Math.round(swatch.mapToItem(null, 0, swatch.height / 2).y),
                    Math.round(wheel.mapToItem(null, 0, wheel.height / 2).y));
            // One close in the card, the title row's.
            compare(Util.findAll(popup, function (item) { return item.toString().indexOf("QQuickImage") === 0 && String(item.source).indexOf("ui-close") >= 0; }).length, 1);

            const green = popup.cellColor(1);
            popup.selectHue(7);
            compare(popup.hueIndex, 7);
            verify(!Qt.colorEqual(green, popup.cellColor(1)));

            // The pick is the settings document's, not this window's: the window is built once per
            // run, so a hue kept in it is the one that comes back green after a restart.
            compare(Controller.settings.yearHueIndex, 7,
                    "the hue the reader picked is not in the settings document");
            // And the grid reads that document rather than a copy taken when it was built, so a
            // write from elsewhere -- a sync, the next open -- reaches it.
            Controller.setSetting("yearHueIndex", 2);
            compare(popup.hueIndex, 2, "the grid does not follow the hue the document holds");
            // Back to the fixture's green: the rest of this case and the snapshots below read it.
            popup.selectHue(4);
            verify(Qt.colorEqual(green, popup.cellColor(1)));

            popup.closePalette();
            compare(popup.paletteOpen, false);

            popup.openPalette(3);
            tryCompare(popup, "paletteReady", true);
            tryVerify(function () { return popup.height > compactHeight; });
            popup.closePalette();
            tryCompare(popup, "paletteOpen", false);
            tryCompare(popup, "height", compactHeight);

            // The open level shuts the palette again; it has no close button.
            popup.toggleLevel(4);
            tryCompare(popup, "paletteOpen", true);
            popup.toggleLevel(4);
            tryCompare(popup, "paletteOpen", false);

            if (lensQaSnapshotDir) {
                const wasTheme = Tokens.theme;
                popup.visible = true;
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-light"));
                popup.openPalette(2);
                tryCompare(popup, "paletteReady", true);
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-palette"));
                Tokens.theme = "dark";
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-dark"));
                popup.closePalette();
                tryCompare(popup, "height", compactHeight);
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-closed"));
                Tokens.theme = wasTheme;
                popup.visible = false;
            }
        }
    }
}
