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

/// @brief Whether a page's on-screen part is large enough to be the page the band is showing.
/// @param visible The part of the page that lies in the band, in the band's pixels.
/// @param band The window's page area the scan draws in.
/// @return Whether @p visible holds at least a quarter of @p band's area.
/// @note A tab parked in the background answers with a sliver strip at the window's bottom edge and
///       a side panel with a sliver column; neither is the page. A half split, and an embedded
///       viewer the page search's own share rule already admits, are.
bool fillsTheBand(Band const& visible, Band const& band);

}
