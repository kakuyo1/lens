import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

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
        name: "SettingsOcr"
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

        function openCategory(panel, title) {
            const row = categoryRow(panel, title);
            verify(row, "the settings catalog has no " + title + " category");

            // The catalog is longer than the body, so the last categories start below the fold:
            // a press on a row that is not on screen goes past the panel. Bring it up first.
            bringIntoView(row);

            mouseClick(row, row.width / 2, row.height / 2);
            wait(20);
        }

        function back(panel) {
            // A page can keep a back arrow of its own that is hidden off the panel; only the one the reader sees is pressed.
            const icons = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Icon") === 0 && String(o.source).indexOf("ui-back.svg") >= 0 && o.visible;
            });
            verify(icons.length > 0, "the settings page has no back control");
            mouseClick(icons[0], icons[0].width / 2, icons[0].height / 2);
            wait(20);
        }

        /// Scroll the body a row sits in until the row's bottom edge is on screen. A press on a row
        /// below the fold goes past the panel and never reaches the row.
        function bringIntoView(item) {
            const body = scrollerOf(item);
            if (body && body.contentHeight > body.height) {
                const bottom = item.mapToItem(body, 0, item.height).y;
                if (bottom > body.height)
                    body.contentY = Math.min(bottom - body.height, body.contentHeight - body.height);
                wait(20);
            }
        }

        /// The scrolling body a page is loaded into, found by walking up from the page.
        function scrollerOf(item) {
            for (let it = item.parent; it; it = it.parent) {
                if (it.contentY !== undefined && it.contentHeight !== undefined)
                    return it;
            }
            return null;
        }

        /// Back from either OCR sub-page returns to the OCR category, not to the capture page: the sub-page
        /// is a route under the category, and Back clears only the route (docs/adr/0153).
        function test_ocrSubPagesReturnToTheOcrCategory() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "OCR");
            compare(panel.category, "ocr");

            const onlineLink = Util.findAll(panel, function (o) { return o.objectName === "onlineOcrLink"; })[0];
            verify(onlineLink, "the OCR page has no online OCR link");
            bringIntoView(onlineLink);
            mouseClick(onlineLink, onlineLink.width / 2, onlineLink.height / 2);
            wait(20);
            compare(panel.route, "ocr-online");
            back(panel);
            compare(panel.category, "ocr", "Back from Online OCR left the OCR category");
            compare(panel.route, "");

            const pathsLink = Util.findAll(panel, function (o) { return o.objectName === "ocrPathsLink"; })[0];
            verify(pathsLink, "the OCR page has no local engine paths link");
            bringIntoView(pathsLink);
            mouseClick(pathsLink, pathsLink.width / 2, pathsLink.height / 2);
            wait(20);
            compare(panel.route, "ocr-paths");
            compare(panel.routeTitle("ocr-paths"), "Local engine paths", "the breadcrumb names the wrong sub-page");
            back(panel);
            compare(panel.category, "ocr", "Back from the engine paths left the OCR category");
            compare(panel.route, "");
        }
    }
}
