#pragma once

/**
 * @file reveal_settings.h
 * @brief The typing pace the reveal runs at, as the settings page offers it (UI.md 4.3).
 *
 * The reader picks a speed, not a millisecond count: the slider runs 1..5 from slow to fast, and
 * kRevealMillisPerCharacter turns that level into the real pace RevealText takes. The blur stays a
 * ratio of the pace inside RevealText, so a slow speed makes each character soft for longer without
 * widening the tail into a band of shaders.
 *
 * The level is a root key of the settings document (docs/adr/0035), and both the setter and the
 * read path go through clampRevealSpeed(): a hand edit can write anything.
 */

#include <algorithm>

namespace lens::app {

/// @brief The slowest speed the setting accepts, one character every kRevealMillisPerCharacter[0].
inline constexpr int kRevealSpeedMin = 1;
/// @brief The fastest speed the setting accepts.
inline constexpr int kRevealSpeedMax = 5;
/// @brief What a reader who has not chosen a speed gets: the 35 ms pace the reveal shipped with.
inline constexpr int kRevealSpeedDefault = 3;
/// @brief Milliseconds per character at each speed 1..5, slowest first; the default sits in the middle.
inline constexpr int kRevealMillisPerCharacter[] = {80, 55, 35, 25, 15};

/**
 * @brief Bring a speed into the range the page offers.
 * @param speed The level the reader picked, or the one the document holds.
 * @return @p speed, or the nearer edge of [kRevealSpeedMin, kRevealSpeedMax].
 */
inline int clampRevealSpeed(int speed)
{
    return std::clamp(speed, kRevealSpeedMin, kRevealSpeedMax);
}

/**
 * @brief The pace a speed level means.
 * @param speed The level; clamped here so a hand-edited document cannot index past the table.
 * @return Milliseconds per character.
 */
inline int revealMillisPerCharacter(int speed)
{
    return kRevealMillisPerCharacter[clampRevealSpeed(speed) - kRevealSpeedMin];
}

} // namespace lens::app
