/** @file word_track.h
 * @brief Places a tracked page's words relative to an anchor word, which is how the scan's lines follow a scroll.
 */
#pragma once

#include <vector>

namespace lens::capture {

/// @brief A word of a tracked page: its id, its box's top-left relative to the anchor word, and its size.
struct TrackedSpan {
    int id     = 0;
    int dx     = 0;
    int dy     = 0;
    int width  = 0;
    int height = 0;
};

/// @brief A word placed on the screen, in the pixels the anchor was measured in.
struct PlacedSpan {
    int id     = 0;
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

/// @brief The visible area a word's centre must lie in for the word to be drawn.
struct Band {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

/// @return The spans whose centre lies in @p band, with the anchor's top-left at (@p anchorX, @p anchorY).
std::vector<PlacedSpan> placeSpans(std::vector<TrackedSpan> const& spans, int anchorX, int anchorY, Band const& band);

/// @brief Re-base every span on another anchor: @p dx and @p dy are that anchor's top-left, relative to the current one.
void rebaseSpans(std::vector<TrackedSpan>& spans, int dx, int dy);

}
