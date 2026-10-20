#pragma once

/**
 * @file glass_settings.h
 * @brief The glass themes' numbers: each parameter's default and the range the settings page offers.
 *
 * Every parameter is a root key of the settings document (docs/adr/0035). The setters and the read
 * path both go through clampGlass(): a hand edit can write anything, and no surface should see a
 * value outside the range the page shows.
 */

#include <algorithm>
#include <string_view>

namespace lens::app {

/// @brief One glass parameter: the default it falls back to and the closed range it can be set in.
struct GlassRange {
    int low;
    int high;
    int fallback;
};

/// @brief Frosted blur, percent of the full scale; 100% is 60 px (UI.md 3.1c).
inline constexpr GlassRange kFrostedBlur{0, 100, 10};
/// @brief Frosted saturation, percent; 100% is untouched (UI.md 3.1c).
inline constexpr GlassRange kFrostedSaturation{0, 200, 120};
/// @brief Frosted rim highlight, percent of white opacity (UI.md 3.1c).
inline constexpr GlassRange kFrostedRim{0, 100, 20};
/// @brief Liquid refraction in px: the displacement at the edge is half of this (UI.md 3.1d).
inline constexpr GlassRange kLiquidRefraction{0, 40, 20};
/// @brief Liquid blur, percent, applied before the refraction (UI.md 3.1d).
inline constexpr GlassRange kLiquidBlur{0, 100, 10};
/// @brief Liquid rim highlight, percent of white opacity (UI.md 3.1d).
inline constexpr GlassRange kLiquidRim{0, 100, 10};
/// @brief The liquid theme's text colour when the reader has not chosen one, as #rrggbb (UI.md 3.1d).
inline constexpr std::string_view kLiquidTextColorFallback = "#101319";

/**
 * @brief How the live backdrop is kept fresh, as the reader chooses it (docs/adr/0073).
 *
 * The ladder is about the source and the pace of the desktop read, not an on/off GPU switch: Qt 6 has no CPU-only UI
 * backend, and whether a screen can be duplicated is a per-output fact (docs/adr/0072). Eco keeps the picture still;
 * Standard reads it with GDI about fifteen times a second; Auto duplicates up to about sixty times a second where the
 * screen offers it; Smooth goes further and follows the screen's own refresh rate (docs/adr/0074).
 */
enum class GlassQuality : int {
    Auto     = 0, ///< Duplication where it works up to about sixty a second, GDI otherwise: the default.
    Eco      = 1, ///< No live refresh; the sheet is retaken when the card appears or moves.
    Standard = 2, ///< GDI, about fifteen reads a second.
    Smooth   = 3, ///< Duplication where it works, at the screen's refresh rate; GDI otherwise.
};

/// The highest stored value; values outside 0..3 clamp to the nearest tier.
inline constexpr int kGlassQualityHigh = static_cast<int>(GlassQuality::Smooth);
/// The tier a document that names none, and restore-defaults, uses.
inline constexpr int kGlassQualityDefault = static_cast<int>(GlassQuality::Auto);

/**
 * @brief Read a stored quality as one of the four tiers.
 * @param value The document's number: a hand edit can write anything.
 * @return The nearest of Auto, Eco, Standard and Smooth.
 */
inline GlassQuality glassQualityFrom(int value)
{
    return static_cast<GlassQuality>(std::clamp(value, 0, kGlassQualityHigh));
}

/// @return Whether this tier keeps the backdrop live at all. Eco does not.
inline bool glassQualityRefreshes(GlassQuality quality)
{
    return quality != GlassQuality::Eco;
}

/// @return Whether this tier may use a screen's duplication. Standard is the GDI tier.
inline bool glassQualityDuplicates(GlassQuality quality)
{
    return quality == GlassQuality::Auto or quality == GlassQuality::Smooth;
}

/// The poll period Automatic gives a duplicated screen, in ms: about sixty a second.
inline constexpr int kGlassAutoFrameMs = 16;
/// The fastest poll any tier takes, in ms: a floor for a pathological refresh rate, not a target.
inline constexpr int kGlassFrameFloorMs = 4;
/// The GDI period, in ms: about fifteen reads a second, shared by the screens that cannot duplicate.
inline constexpr int kGlassGdiFrameMs = 67;

/**
 * @brief The poll period for a duplicated screen under @p quality.
 * @param quality The tier in force.
 * @param refreshRate The screen's rate in Hz, as QScreen reports it; zero or negative when unknown.
 * @return The period in milliseconds. Automatic keeps kGlassAutoFrameMs; Smooth polls at or just above the screen's own
 *         period, held between kGlassFrameFloorMs and kGlassGdiFrameMs. Standard and Eco do not duplicate, so their pace
 *         is the caller's business.
 * @note Smooth rounds the period down rather than up: at 144 Hz a 6 ms tick polls 166 times a second and catches every
 *       frame, while a 7 ms tick samples at 142 Hz and misses one in seventy (docs/adr/0074).
 */
inline int glassQualityFrameMs(GlassQuality quality, double refreshRate)
{
    if (quality != GlassQuality::Smooth or not(refreshRate > 0.0)) return kGlassAutoFrameMs;
    return std::clamp(static_cast<int>(1000.0 / refreshRate), kGlassFrameFloorMs, kGlassGdiFrameMs);
}

/// @return The tier's name, as the developer command and the log spell it.
inline std::string_view glassQualityName(GlassQuality quality)
{
    switch (quality) {
        case GlassQuality::Eco: return "eco";
        case GlassQuality::Standard: return "standard";
        case GlassQuality::Smooth: return "smooth";
        case GlassQuality::Auto: break;
    }
    return "auto";
}

/**
 * @brief Bring a glass parameter into the range the page offers.
 * @param range The parameter's range.
 * @param value The value the reader picked, or the one the document holds.
 * @return @p value, or the nearer edge of @p range.
 */
inline int clampGlass(GlassRange range, int value)
{
    return std::clamp(value, range.low, range.high);
}

} // namespace lens::app
