#pragma once

/**
 * @file glass_settings.h
 * @brief The glass themes' numbers that are arithmetic rather than settings: how the live backdrop is
 *        paced, and which of the four tiers does what.
 *
 * Each parameter a reader chooses -- blur, saturation, rim, refraction, the text colour -- is one row
 * of core/domain/settings_table.h, with its default and the range the page offers. Nothing here
 * clamps a setting: the clamp lives in the row, and settings_access.cpp is the only place that
 * applies it.
 */

#include <algorithm>
#include <string_view>

#include "core/domain/settings_table.h"

namespace lens::app {

/**
 * @brief How the live backdrop is kept fresh, as the reader chooses it (docs/adr/0073).
 *
 * The ladder is about the source and the pace of the desktop read, not an on/off GPU switch: Qt 6 has no CPU-only UI
 * backend, and whether a screen can be followed is a per-output fact (docs/adr/0072, docs/adr/0087). Eco keeps the
 * picture still; Standard reads it with GDI about fifteen times a second; Auto follows up to about sixty times a second
 * where the screen offers it; Smooth goes further and follows the screen's own refresh rate (docs/adr/0074).
 */
enum class GlassQuality : int {
    Auto     = 0, ///< A live source where one works, up to about sixty a second; GDI otherwise: the default.
    Eco      = 1, ///< No live refresh; the sheet is retaken when the card appears or moves.
    Standard = 2, ///< GDI, about fifteen reads a second.
    Smooth   = 3, ///< A live source where one works, at the screen's refresh rate; GDI otherwise.
};

/// The highest stored value, from the tier's own row in the settings table.
inline constexpr int kGlassQualityHigh = core::settings::find("glassQuality")->high;
/// The tier a document that names none, and restore-defaults, uses.
inline constexpr int kGlassQualityDefault = core::settings::find("glassQuality")->fallback.number;

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

/// @return Whether this tier may use a screen's live frame source. Standard is the GDI tier.
inline bool glassQualityFollows(GlassQuality quality)
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

} // namespace lens::app
