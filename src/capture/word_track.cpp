/** @file word_track.cpp
 * @brief Anchor-relative placement and clipping of tracked words, for word_track.h.
 */
#include "word_track.h"

namespace lens::capture {

std::vector<PlacedSpan> placeSpans(std::vector<TrackedSpan> const& spans, int const anchorX, int const anchorY, Band const& band)
{
    std::vector<PlacedSpan> placed;
    for (auto const& span : spans) {
        int const x  = anchorX + span.dx;
        int const y  = anchorY + span.dy;
        int const cx = x + span.width / 2;
        int const cy = y + span.height / 2;
        if (cx < band.x or cx >= band.x + band.width or cy < band.y or cy >= band.y + band.height) continue;
        placed.push_back({.id = span.id, .x = x, .y = y, .width = span.width, .height = span.height});
    }
    return placed;
}

void rebaseSpans(std::vector<TrackedSpan>& spans, int const dx, int const dy)
{
    for (auto& span : spans) {
        span.dx -= dx;
        span.dy -= dy;
    }
}

}
