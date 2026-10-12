#pragma once

/**
 * @file bubble_settings.h
 * @brief The range and default of the bubble's display time, the one bubble setting with a number in it.
 *
 * The reader types the seconds in, and the document can hold anything a hand edit wrote, so both
 * the setter and the read path go through clampBubbleDismissSeconds(). The placement side has no
 * arithmetic here: which edge the bubble hangs from is Main.qml's, beside the rest of the placing.
 */

#include <algorithm>

namespace lens::app {

/// @brief Seconds a bubble stays up when the reader has not chosen a display time (UI.md 4.3).
inline constexpr int kBubbleDismissDefaultSeconds = 5;
/// @brief The shortest display time the setting accepts.
inline constexpr int kBubbleDismissMinSeconds = 1;
/// @brief The longest display time the setting accepts.
inline constexpr int kBubbleDismissMaxSeconds = 30;

/**
 * @brief Bring a display time into the range the setting accepts.
 * @param seconds The value the reader typed, or the one the document holds.
 * @return @p seconds, or the nearer edge of [kBubbleDismissMinSeconds, kBubbleDismissMaxSeconds].
 */
inline int clampBubbleDismissSeconds(int seconds)
{
    return std::clamp(seconds, kBubbleDismissMinSeconds, kBubbleDismissMaxSeconds);
}

} // namespace lens::app
