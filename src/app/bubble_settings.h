#pragma once

/**
 * @file bubble_settings.h
 * @brief The ranges and defaults of the two bubble settings with a number in them: how long the
 *        card stays up, and the size its answer is read at.
 *
 * The reader picks both, and the document can hold anything a hand edit wrote, so both the setters
 * and the read paths go through clampBubbleDismissSeconds() and clampBubbleFontSize(). The
 * placement side has no arithmetic here: which edge the bubble hangs from is Main.qml's, beside
 * the rest of the placing.
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

/// @brief The size the answer is read at when the reader has not chosen one (UI.md 4.4).
inline constexpr int kBubbleFontSizeDefault = 13;
/// @brief The smallest size the setting accepts: below this the card stops being readable at a glance.
inline constexpr int kBubbleFontSizeMin = 12;
/// @brief The largest size the setting accepts: above this the card grows faster than the screen has room.
inline constexpr int kBubbleFontSizeMax = 18;

/**
 * @brief Bring an answer size into the range the setting accepts.
 * @param pixels The value the reader picked, or the one the document holds.
 * @return @p pixels, or the nearer edge of [kBubbleFontSizeMin, kBubbleFontSizeMax].
 */
inline int clampBubbleFontSize(int pixels)
{
    return std::clamp(pixels, kBubbleFontSizeMin, kBubbleFontSizeMax);
}

} // namespace lens::app
