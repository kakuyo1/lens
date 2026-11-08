/** @file word_track.h
 * @brief Places a tracked page's words relative to an anchor word, which is how the scan's lines follow a scroll,
 *        and cuts a read word to its letters, which is how its line avoids the punctuation it trails off with.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
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

/// @brief Band equality, field by field: a box re-read is compared with the one held.
constexpr bool operator==(Band const& a, Band const& b) noexcept
{
    return a.x == b.x and a.y == b.y and a.width == b.width and a.height == b.height;
}

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

/// @brief One word as the scan reads it: the text with its trailing punctuation left out, and the box cut to the letters.
struct TrimmedWord {
    std::string text; ///< The letters, as the page spells them, UTF-8.
    Band        box;  ///< The letters' box, cut from the word unit's own.
};

/// @return How many trailing characters of @p text are space or punctuation: the comma a word unit
///         keeps with its word, the em dash a novel keeps against its words, the full-width stop,
///         and the space it trails off with. Counted from the end, in code points -- the unit a
///         range's end is retracted in; a letter or digit stops the count.
int trailingPunctOf(std::string_view text);

/// @brief The word a scan draws: @p text with its trailing punctuation left out, and @p full cut to
///        the letters where the reader could say where they end.
/// @param full       The word unit's own first box.
/// @param lettersEnd The box of the word unit's first trailing character, whose left edge is where
///                   the letters end, when the reader read one; empty when the letters occupy
///                   nothing at all; nothing when the reader could not say.
/// @return Nothing when the word is nothing but punctuation: the token leaves the scan. A word the
///         reader could not measure keeps its punctuation and its whole box.
std::optional<TrimmedWord> trimmedWord(std::string_view text, Band const& full, std::optional<Band> const& lettersEnd);

}
