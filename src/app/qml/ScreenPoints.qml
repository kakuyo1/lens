import QtQuick

/**
 * Maps a position the mouse hook reports, in native screen pixels, to DIPs, the unit a window is placed in. It holds no
 * window and draws nothing: Main.qml owns the surfaces and SurfaceHost.qml places them, while this turns the hook's
 * payloads around. The conversion itself is SurfaceGeometry's (src/app/geometry/surface_geometry.h), the one copy
 * every surface and the tests read (docs/adr/0116).
 */
QtObject {
    /// SurfaceHost.qml, whose screenForPhysical() finds the screen a native point is on.
    required property var host

    /**
     * @brief The DIP point of a native-pixel point, measured from @p screen's own origin.
     *
     * Qt draws a window on a screen at native = origin + (dip - origin) * scale. Measured on the
     * portrait monitor above the primary at 125% (origin 197,-2560): a card placed at DIP y -1120
     * was drawn at native y -760. SurfaceGeometry.dipFromNativePoint carries the formula and its
     * value test; this finds the screen first.
     */
    // qmllint disable missing-property
    function dipOn(screen, point) {
        return SurfaceGeometry.dipFromNativePoint(Qt.point(point.x, point.y),
                                                  Qt.point(screen.virtualX, screen.virtualY),
                                                  screen.devicePixelRatio || 1);
    }
    // qmllint enable missing-property

    /**
     * Turn a position the mouse hook reported into one a window can be placed at.
     * The hook reads native screen pixels; windows and QScreen geometry use DIPs.
     */
    // qmllint disable missing-property
    function toDip(payload) {
        const target = host.screenForPhysical(Qt.point(payload.x, payload.y));
        const at = dipOn(target, Qt.point(payload.x, payload.y));
        const ratio = target.devicePixelRatio || 1;
        const out = {};
        for (const key in payload)
            out[key] = payload[key];
        out.x = at.x;
        out.y = at.y;
        // The anchor's own size travels with it when the card is placed against a whole object:
        // a native-pixel size divided by the same screen scale gives the DIP size the window sees.
        if (payload.anchorWidth !== undefined)
            out.anchorWidth = SurfaceGeometry.dipFromNativeLength(payload.anchorWidth, ratio);
        if (payload.anchorHeight !== undefined)
            out.anchorHeight = SurfaceGeometry.dipFromNativeLength(payload.anchorHeight, ratio);
        return out;
    }
    // qmllint enable missing-property
}
