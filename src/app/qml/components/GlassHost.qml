pragma ComponentBehavior: Bound

import QtQuick

/**
 * The glass lifecycle of one surface window: when its desktop is taken, whether the window is kept out of captures, and
 * when the sheet follows the window (docs/adr/0038, 0039, 0040, 0041).
 *
 * Put one under a window's root, with the window's sheet from GlassBackdrop.surface(name) and, when the card can be
 * dragged, whether it is being dragged. Nothing is taken while the window's table has no glass or the window is hidden,
 * and the sheet is dropped the moment either is so: a hidden window is not photographed. It is an Item only so that its
 * timers and connections can be declared inside it; it draws nothing.
 */
Item {
    id: host

    visible: false

    /// The window the glass is drawn in. Its place moves the sheet; its visibility decides the exclusion.
    required property var window
    /// The surface's sheet, from GlassBackdrop.surface(name).
    required property var sheet
    /// Whether the card is being dragged. A drag moves the sheet with the window and takes no capture until it ends.
    property bool dragging: false
    /// Whether the window is kept out of captures right now, so the sheet is taken without hiding the card.
    property bool excludedFromCapture: false
    /// The theme the picture was taken under. Only a change of theme takes a new one: any other settings write, such as a
    /// slider's, would otherwise dim the window to retake a picture it already has.
    property string pictureTheme: ""

    /// Whether this window shows glass at all, under the theme in force.
    readonly property bool glass: host.window.tokens.glassMode !== ""
    /// The white the card sits under, which the text tone is judged over: the frosted theme's fill, none under liquid.
    readonly property real fillAlpha: host.window.tokens.panel.a

    function windowRect() {
        return Qt.rect(host.window.x, host.window.y, host.window.width, host.window.height);
    }

    /// The desktop under the window, taken as a sheet a little larger than the window (GlassSheet.follow). Where the OS
    /// keeps the window out of captures, the sheet is taken as it is, nothing flickers, and it is kept fresh while the glass
    /// shows (GlassSheet.setLive). Otherwise the window goes transparent for the settle time while the sheet is taken, and
    /// only when the sheet no longer covers the window.
    function capture() {
        host.pictureTheme = Controller.settings.theme;
        // Excluded only while the glass shows: a window kept out of captures is missing from screenshots too.
        host.excludedFromCapture = host.sheet.keepOutOfCapture(host.window, host.window.visible && host.glass);
        if (!host.glass || !host.window.visible) {
            host.sheet.clear();
            return;
        }
        if (host.excludedFromCapture) {
            host.sheet.follow(host.windowRect(), host.fillAlpha);
            host.sheet.setLive(true);
            return;
        }
        // Not excluded: the sheet is taken with the card hidden, and never refreshed live, since a live read would photograph it.
        host.sheet.setLive(false);
        if (host.sheet.covers(host.windowRect()))
            return;
        host.window.opacity = 0;
        captureTimer.restart();
    }

    /// The window moved. Once it is kept out of captures, the sheet follows it at once: GlassSheet takes a new one only
    /// when the window nears the sheet's edge, so a drag takes few captures.
    function followWindow() {
        moveSettle.restart();
        if (host.excludedFromCapture && host.window.visible)
            host.sheet.follow(host.windowRect(), host.fillAlpha);
    }

    onDraggingChanged: {
        if (!host.dragging)
            host.capture();
    }

    /// Any move restarts this. Once it settles, the sheet is taken if the window has outgrown it.
    Timer {
        id: moveSettle
        interval: 300
        onTriggered: {
            if (!host.dragging)
                host.capture();
        }
    }

    Timer {
        id: captureTimer
        interval: 120
        onTriggered: {
            // Taken again rather than kept: this is the path where the window was hidden, so the sheet is retaken whole.
            host.sheet.clear();
            host.sheet.follow(host.windowRect(), host.fillAlpha);
            host.window.opacity = 1;
        }
    }

    Connections {
        target: host.window
        function onXChanged() {
            host.followWindow();
        }
        function onYChanged() {
            host.followWindow();
        }
        function onVisibleChanged() {
            // A shown window takes its sheet afresh: the one it kept was taken where it used to be.
            if (host.window.visible)
                host.sheet.clear();
            host.capture();
        }
    }

    Connections {
        target: Controller
        function onSettingsChanged() {
            if (Controller.settings.theme !== host.pictureTheme)
                host.capture();
        }
    }
}
