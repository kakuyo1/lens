#pragma once

/**
 * @file reveal_settings.h
 * @brief The typing pace the reveal runs at, as the settings page offers it (UI-SPEC.md 4.3).
 *
 * The reader picks a speed, not a millisecond count: the slider runs 1..5 from slow to fast, and
 * kRevealMillisPerCharacter turns that level into the real pace RevealText takes. The blur stays a
 * ratio of the pace inside RevealText, so a slow speed makes each character soft for longer without
 * widening the tail into a band of shaders.
 *
 * The level itself is one row of core/domain/settings_table.h, which owns its default and the range
 * the page offers. What lives here is the mapping from that level to a pace, which is not a setting.
 */

#include <algorithm>

#include "core/domain/settings_table.h"

namespace lens::app {

/// @brief The slowest speed the setting accepts, one character every kRevealMillisPerCharacter[0].
inline constexpr int kRevealSpeedMin = core::settings::find("revealSpeed")->low;
/// @brief The fastest speed the setting accepts.
inline constexpr int kRevealSpeedMax = core::settings::find("revealSpeed")->high;
/// @brief What a reader who has not chosen a speed gets: the 35 ms pace the reveal shipped with.
inline constexpr int kRevealSpeedDefault = core::settings::find("revealSpeed")->fallback.number;
/// @brief Milliseconds per character at each speed 1..5, slowest first; the default sits in the middle.
inline constexpr int kRevealMillisPerCharacter[] = {80, 55, 35, 25, 15};

/**
 * @brief The pace a speed level means.
 * @param speed The level; clamped here so a hand-edited document cannot index past the table.
 * @return Milliseconds per character.
 */
inline int revealMillisPerCharacter(int speed)
{
    return kRevealMillisPerCharacter[std::clamp(speed, kRevealSpeedMin, kRevealSpeedMax) - kRevealSpeedMin];
}

} // namespace lens::app
