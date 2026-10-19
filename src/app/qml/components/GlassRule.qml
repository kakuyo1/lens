pragma Singleton

import QtQuick

/**
 * The one rule that decides whether a glass card draws a backdrop at all (docs/adr/0069).
 *
 * Blur at 0 and nothing to bend means there is nothing the backdrop can add: the desktop under the card is exactly
 * what the reader asked to see. The window is already per-pixel transparent, so DWM composites the live desktop for
 * free -- sharper than any sheet, at the screen's own refresh rate, and at none of the cost of reading the desktop
 * (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.6 prices one read at 10 to 25 ms).
 *
 * This is a singleton because both halves of a surface need the answer and must agree: the card that draws
 * (GlassSurface) and the window that decides when to read the desktop (GlassHost). Each theme asks its own numbers, so
 * the rule takes all of them and picks, rather than every caller picking.
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
