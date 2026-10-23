pragma Singleton

import QtQuick

/**
 * Whether a glass card draws a backdrop at all (docs/adr/0069).
 *
 * Blur at 0 and nothing to bend means the backdrop adds nothing: the window is per-pixel transparent, so DWM composites
 * the live desktop for free -- sharper than any sheet, at the screen's refresh rate, and with no desktop read (one read
 * costs 10 to 25 ms: docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.6).
 *
 * A singleton because both halves of a surface need the answer and must agree: the card that draws (GlassSurface) and
 * the window that decides when to read the desktop (GlassHost). Each theme asks its own numbers, so the rule takes all
 * of them and picks, rather than every caller picking.
 */
QtObject {
    /**
     * @param mode The glass in force: "frosted", "liquid", or empty when there is none.
     * @param frostedBlur The frosted theme's blur, in percent of the full scale.
     * @param liquidBlur The liquid theme's blur, in percent of the full scale.
     * @param liquidRefraction The liquid theme's refraction, in px.
     * @return True when the card should draw no backdrop and let the live desktop through.
     * @note Liquid asks both numbers because bending the edge needs the desktop in hand: a card with refraction left
     *       on would show the live desktop unbent and read as a different theme.
     */
    function passthrough(mode, frostedBlur, liquidBlur, liquidRefraction) {
        if (mode === "frosted")
            return frostedBlur <= 0;
        if (mode === "liquid")
            return liquidBlur <= 0 && liquidRefraction <= 0;
        return false;
    }
}
