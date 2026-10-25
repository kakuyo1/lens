import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/// The year card's keyboard chain: the grid is one Tab stop whose arrows move the selected day,
/// and the legend and hue dots are ordinary Tab stops with a press action.
Item {
    id: root
    width: 800
    height: 700

    Component { id: yearComponent; YearWordsPopup {} }

    TestCase {
        name: "YearKeyboard"
        when: windowShown

        function make() {
            const popup = createTemporaryObject(yearComponent, root);
            verify(popup);
            popup.visible = true;
            wait(50);
            return popup;
        }

        function named(popup, name) {
            return Util.findAll(popup, function (o) { return o.objectName === name; });
        }

        function test_arrowsMoveTheSelectedDay() {
            const popup = make();
            const grid = named(popup, "yearGrid")[0];
            verify(grid, "the year card has no grid");
            grid.forceActiveFocus();
            wait(20);
            verify(popup.selectedDate !== "", "focusing the grid selected no day");

            const days = Controller.yearDays;
            let index = days.findIndex(function (day) { return day.date === popup.selectedDate; });
            verify(index > 0, "the grid did not start on a movable day");
            keyClick(Qt.Key_Left);
            wait(20);
            compare(popup.selectedDate, days[index - 1].date, "Left did not move one day back");

            index -= 1;
            keyClick(Qt.Key_Down);
            wait(20);
            compare(popup.selectedDate, days[Math.min(days.length - 1, index + 7)].date,
                    "Down did not move one week forward");
        }

        function test_theLegendAndHueDotsTakeTheKeyboard() {
            const popup = make();
            popup.openPalette(2);
            tryCompare(popup, "paletteReady", true);

            const legend = named(popup, "yearLegendCell")[0];
            verify(legend, "the legend has no keyboard target");
            legend.forceActiveFocus();
            keyClick(Qt.Key_Return);
            compare(popup.selectedLevel, 0, "the legend's Return did not open level 0");

            const hues = named(popup, "yearHueCell");
            verify(hues.length === 12, "the palette does not offer twelve hue targets");
            hues[3].forceActiveFocus();
            keyClick(Qt.Key_Return);
            compare(popup.hueIndex, 3, "the hue dot's Return did not select that hue");
            popup.closePalette();
        }
    }
}
