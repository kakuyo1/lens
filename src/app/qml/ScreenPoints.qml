import QtQuick

/**
 * Maps a position the mouse hook reports, in native screen pixels, to DIPs, the unit a window is placed in. It holds no
 * window and draws nothing: Main.qml owns the surfaces and SurfaceHost.qml places them, while this only turns points, so
 * both stay under the QML size cap (QML.md section 1), as ProfileScenario.qml does.
 */
QtObject {
    /// SurfaceHost.qml, whose screenForPhysical() finds the screen a native point is on.
    required property var host

    /**
     * @brief The DIP point of a native-pixel point, measured from @p screen's own origin.
     *
     * Qt draws a window on a screen at native = origin + (dip - origin) * scale. Measured on the
     * portrait monitor above the primary at 125% (origin 197,-2560): a card placed at DIP y -1120
     * was drawn at native y -760. Scaling from zero is right only for a screen whose origin is zero;
     * on this one it misplaces every point by origin * (1 - 1 / scale), which is 512 DIPs.
     */
    // qmllint disable missing-property
    function dipOn(screen, point) {
        const ratio = screen.devicePixelRatio || 1;
        return Qt.point(screen.virtualX + (point.x - screen.virtualX) / ratio,
                        screen.virtualY + (point.y - screen.virtualY) / ratio);
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
            out.anchorWidth = payload.anchorWidth / ratio;
        if (payload.anchorHeight !== undefined)
            out.anchorHeight = payload.anchorHeight / ratio;
        return out;
    }
    // qmllint enable missing-property
}
