import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The theme dropdown's option preview (UI.md 4.4): resting the pointer on a row raises a miniature
 * settings card drawn in that row's theme beside the list, and taking the list down puts it down.
 *
 * The card is ThemePreview.qml, a window of its own created on the first hover. It is not declared
 * under the field, so its labels never join the list's -- the preview is reached through the
 * field's own `optionPreviewWindow` handle rather than by walking the window tree.
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
        name: "ThemePreview"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        /// @return The theme dropdown, with its list opened.
        function openedThemeField(main) {
            const panel = settingsPanel(main);
            main.showSettings();
            panel.openCategory("general");
            wait(25);
            const field = Util.findAll(panel, function (o) {
                return o.objectName === "themeField";
            })[0];
            verify(field, "the General page has no theme control");
            field.openList();
            wait(50);
            return field;
        }

        /// @return The row showing @p label in the list's own window.
        function rowOf(field, label) {
            const row = Util.textWith(Util.textsInNestedWindow(field), [label]);
            verify(row, "the theme list has no '" + label + "' row");
            return row;
        }

        function hover(field, label) {
            const row = rowOf(field, label);
            mouseMove(row, row.width / 2, row.height / 2);
        }

        /// @return The preview's card left edge, in screen coordinates: the window less the
        ///         transparent margin its shadow is drawn in.
        function cardLeftOf(preview) {
            return preview.x + preview.shadowMargin;
        }

        function cardRightOf(preview) {
            return preview.x + preview.width - preview.shadowMargin;
        }

        function test_hoveringARowRaisesThatThemesPreviewBesideTheList() {
            const main = make();
            const field = openedThemeField(main);
            compare(field.optionPreviewWindow, null, "a preview was built before anything was hovered");

            hover(field, "Dark");
            tryCompare(field, "hoveredIndex", 1);
            const preview = field.optionPreviewWindow;
            verify(preview, "hovering a row built no preview window");
            tryCompare(preview, "visible", true);
            compare(preview.themeValue, "dark");
            compare(preview.optionLabel, "Dark", "the preview does not show the hovered row's label");

            // The next row takes the same window over rather than a second one being built, and
            // the window is still beside the list. A solid theme on both sides: the offscreen
            // platform cannot drive a glass window's capture, which is a real-machine check.
            hover(field, "Light");
            tryCompare(field, "hoveredIndex", 0);
            compare(field.optionPreviewWindow, preview, "a second preview window was built");
            tryCompare(preview, "themeValue", "light");
            compare(preview.optionLabel, "Light");

            // The card takes the side with room, clear of the list's card. The offscreen screen is
            // narrow, so which side that is is read from the geometry rather than assumed.
            const list = field.openChildRect;
            const area = main.workArea(main.screenFor(Qt.point(list.x, list.y)));
            const gap = 8;
            if (list.x + list.width + gap + preview.width - preview.shadowMargin <= area.right - gap) {
                compare(cardLeftOf(preview), list.x + list.width + gap,
                        "the card did not take the room on the list's right");
            } else if (list.x - gap - preview.width + preview.shadowMargin >= area.left + gap) {
                compare(cardRightOf(preview), list.x - gap,
                        "the card did not take the room on the list's left");
            }

            field.closeList();
        }

        function test_closingTheListTakesThePreviewDown() {
            const main = make();
            const field = openedThemeField(main);

            hover(field, "Light");
            tryCompare(field, "hoveredIndex", 0);
            const preview = field.optionPreviewWindow;
            verify(preview);
            tryCompare(preview, "visible", true);

            field.closeList();
            tryCompare(preview, "visible", false);
            compare(field.hoveredIndex, -1, "the field kept pointing at a row after the list went");
        }

        /// Leaving the list is held a beat so a pointer crossing the panel does not blink the card;
        /// after that beat it goes.
        function test_leavingTheListTakesThePreviewDown() {
            const main = make();
            const field = openedThemeField(main);

            hover(field, "Dark");
            tryCompare(field, "hoveredIndex", 1);
            const preview = field.optionPreviewWindow;
            verify(preview);
            tryCompare(preview, "visible", true);

            // A place in the list's own window that is not a row: the shadow margin around the
            // card. Cross-window moves are not delivered to a hover handler under the offscreen
            // platform, so the pointer has to leave within the window that owns it.
            const row = rowOf(field, "Dark");
            mouseMove(row.Window.window.contentItem, 2, 2);
            tryCompare(preview, "visible", false);
            compare(field.hoveredIndex, -1, "the field kept pointing at a row the pointer left");
        }
    }
}
