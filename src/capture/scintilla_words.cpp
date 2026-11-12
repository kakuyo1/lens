/** @file scintilla_words.cpp
 * @brief Word-unit splitting for a Scintilla document, for scintilla_words.h.
 */

#include "scintilla_words.h"

namespace lens::capture {
namespace {

constexpr bool isAsciiLetter(unsigned char const c)
{
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');
}

constexpr bool isDigit(unsigned char const c)
{
    return c >= '0' and c <= '9';
}

constexpr bool isWordByte(unsigned char const c)
{
    return isAsciiLetter(c) or isDigit(c);
}

/// @brief Whether @p c is the punctuation a word unit keeps after its letters.
/// @note A non-ASCII byte starts a different script; it separates, rather than trails, an English
///       word. Hyphens and quotes are left to the caller's word filter, not glued on here.
constexpr bool isTrailingPunct(unsigned char const c)
{
    if (c >= 0x80 or isWordByte(c) or c == '\'') return false;
    if (c == ' ' or c == '\t' or c == '\r' or c == '\n' or c == '\v' or c == '\f') return false;
    return c >= 0x21 and c <= 0x7e;
}

} // namespace

std::vector<ScintillaWordUnit> scintillaWordUnits(std::string_view const text)
{
    std::vector<ScintillaWordUnit> units;
    std::size_t                    at = 0;
    while (at < text.size()) {
        unsigned char const c = static_cast<unsigned char>(text[at]);
        if (not isWordByte(c)) {
            ++at;
            continue;
        }
        ScintillaWordUnit unit{.start = at};
        // An apostrophe inside letters keeps "don't" one unit; one at either edge does not.
        while (at < text.size()) {
            unsigned char const here = static_cast<unsigned char>(text[at]);
            if (isWordByte(here)) {
                ++at;
                continue;
            }
            if (here == '\'' and at + 1 < text.size() and isAsciiLetter(static_cast<unsigned char>(text[at + 1])) and
                at > unit.start and isAsciiLetter(static_cast<unsigned char>(text[at - 1]))) {
                ++at;
                continue;
            }
            break;
        }
        unit.lettersEnd = at;
        while (at < text.size() and isTrailingPunct(static_cast<unsigned char>(text[at])))
            ++at;
        unit.end = at;
        units.push_back(unit);
    }
    return units;
}

}
