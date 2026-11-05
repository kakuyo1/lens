/**
 * @file hover_lookup.cpp
 * @brief Implementation of the hover dwell rule and the word rule.
 */

#include "core/domain/hover_lookup.h"

#include <cstddef>
#include <cstdlib>

namespace lens::core {
namespace {

bool isAsciiLetter(char const c)
{
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');
}

}

HoverDwell::HoverDwell(long long const dwellMs, int const slopPx)
    : dwellMs_(dwellMs), slopPx_(slopPx)
{}

bool HoverDwell::update(long long const nowMs, int const x, int const y)
{
    const bool moved = not anchored_ or std::abs(x - x_) > slopPx_ or std::abs(y - y_) > slopPx_;
    if (moved) {
        anchored_ = true;
        fired_    = false;
        x_        = x;
        y_        = y;
        since_    = nowMs;
        return false;
    }
    if (fired_ or nowMs - since_ < dwellMs_) return false;
    fired_ = true;
    return true;
}

std::string hoverWordFrom(std::string_view const token)
{
    auto const first = token.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    auto const word = token.substr(first, token.find_last_not_of(' ') - first + 1);

    int letters = 0;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (isAsciiLetter(word[i])) {
            ++letters;
        } else if (not(word[i] == '\'' and i > 0 and i + 1 < word.size())) {
            return {};
        }
    }
    return letters >= 2 ? std::string{word} : std::string{};
}

}
