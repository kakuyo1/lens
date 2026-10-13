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
