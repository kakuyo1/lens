/** @file word_track.cpp
 * @brief Anchor-relative placement and clipping of tracked words, for word_track.h.
 */
#include "word_track.h"

#include "util/text.h"

namespace lens::capture {
namespace {

/// @brief Whether a code point is space or punctuation: what a word unit trails off with.
/// @note ASCII is exact. Beyond it the blocks a page's word units actually end with are the ones
///       listed -- Latin-1, Arabic, general punctuation, CJK punctuation, and the fullwidth and
///       halfwidth forms. Punctuation of other scripts is not counted, and such a word keeps its
///       tail; the scan reads Latin pages, and the word's key is normalized without the tail anyway.
bool isSpaceOrPunct(char32_t const code)
{
    if (code <= 0x7F) {
        // The six ASCII whitespaces, and the punctuation-mark subset of ASCII: letters and digits
        // stop the count, and the symbol characters ($ + < > = ^ ` | ~) are symbols, not punctuation.
        return util::isAsciiSpace(static_cast<char>(code)) or
               (code > 0x20 and not util::isAsciiAlnum(static_cast<char>(code)) and code != '$' and
                code != '+' and code != '<' and code != '>' and code != '=' and code != '^' and
                code != '`' and code != '|' and code != '~');
    }
    if (code == 0x00A0 or code == 0x1680 or (code >= 0x2000 and code <= 0x200A) or code == 0x2028 or
        code == 0x2029 or code == 0x202F or code == 0x205F or code == 0x3000)
        return true; // spaces beyond ASCII
    return code == 0x00A1 or (code >= 0x00A6 and code <= 0x00A7) or code == 0x00AB or
           (code >= 0x00B6 and code <= 0x00B7) or code == 0x00BB or code == 0x00BF or
           (code >= 0x060C and code <= 0x060D) or code == 0x061B or code == 0x061F or
           (code >= 0x066A and code <= 0x066D) or code == 0x06D4 or
           (code >= 0x2010 and code <= 0x2027) or                    // dashes, quotes, daggers, leaders
           (code >= 0x2030 and code <= 0x205E and code != 0x2052) or // primes, brackets, reference marks
           (code >= 0x3001 and code <= 0x3004) or (code >= 0x3008 and code <= 0x3012) or
           (code >= 0x3014 and code <= 0x301F) or code == 0x3030 or (code >= 0x3036 and code <= 0x3037) or
           code == 0x303B or code == 0x303D or
           (code >= 0xFF01 and code <= 0xFF03) or (code >= 0xFF05 and code <= 0xFF0A) or
           (code >= 0xFF0C and code <= 0xFF0F) or (code >= 0xFF1A and code <= 0xFF1B) or
           (code >= 0xFF1F and code <= 0xFF20) or (code >= 0xFF3B and code <= 0xFF3D) or code == 0xFF3F or
           code == 0xFF5B or code == 0xFF5D or (code >= 0xFF5F and code <= 0xFF60) or
           (code >= 0xFF61 and code <= 0xFF65);
}

/// @brief Drops the last @p cut code points of @p text, walking code point boundaries from the end.
std::string withoutTrailingCodePoints(std::string_view const text, int const cut)
{
    std::size_t end = text.size();
    for (int dropped = 0; dropped < cut and end > 0; ++dropped) {
        --end;
        while (end > 0 and (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            --end; // continuation bytes
    }
    return std::string{text.substr(0, end)};
}

}

int trailingPunctOf(std::string_view const text)
{
    auto const code = util::decodeUtf8(text);
    if (not code) return 0; // malformed bytes cannot be measured; the word keeps its tail
    int cut = 0;
    while (cut < static_cast<int>(code->size()) and isSpaceOrPunct((*code)[code->size() - 1 - cut]))
        ++cut;
    return cut;
}

std::optional<TrimmedWord> trimmedWord(std::string_view const text, Band const& full, std::optional<Band> const& lettersEnd)
{
    int const cut = trailingPunctOf(text);
    if (cut == 0) {
        if (full.width <= 0 or full.height <= 0) return std::nullopt;
        return TrimmedWord{std::string{text}, full};
    }
    // An empty letters-end box says the letters occupy nothing: the word is punctuation alone.
    if (lettersEnd and lettersEnd->width <= 0 and lettersEnd->height <= 0) return std::nullopt;
    if (lettersEnd) {
        // The junk's own left edge is where the letters end, and it must sit inside the word's box:
        // a character stepped past the range's end lands on the next line's first glyph.
        Band const trimmed{.x = full.x, .y = full.y, .width = lettersEnd->x - full.x, .height = full.height};
        if (trimmed.width > 0 and trimmed.width < full.width)
            return TrimmedWord{withoutTrailingCodePoints(text, cut), trimmed};
    }
    return TrimmedWord{std::string{text}, full};
}

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

bool fillsTheBand(Band const& visible, Band const& band)
{
    // A quarter of the area admits a half split and the embedded viewers the page search's own share
    // rule lets through, and refuses the sliver a background tab or a side panel answers with.
    long long const area  = static_cast<long long>(visible.width) * visible.height;
    long long const whole = static_cast<long long>(band.width) * band.height;
    return area * 4 >= whole;
}

}
