import QtQuick
import QtQuick.Window
import Lens

/**
 * A shown window that owns a colour table, as a surface window does (docs/adr/0041). A component reads its table
 * through Window.window, so a component under test is created in this window's contentItem rather than under the
 * test's own item, which has no table of its own. The component and surface suites both import it from here.
 *
 * focusSink is an item in this window that a test moves keyboard focus to, to leave a field. The test's own item is in
 * another window, and focus only leaves a field for an item in the same window.
 */
Window {
    width: 400
    height: 300
    visible: true
    readonly property SurfaceTokens tokens: SurfaceTokens {}
    readonly property alias focusSink: sink

    Item {
        id: sink
    }
}
