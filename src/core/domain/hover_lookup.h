#pragma once

/**
 * @file hover_lookup.h
 * @brief When a hover lookup fires, and which token it sends: a pointer that has rested, and a word.
 *
 * Pure, so the dwell rule and the word rule run without a desktop. The read that finds the token
 * lives in app/platform/hover_word_lookup.h.
 */

#include <string>
#include <string_view>

namespace lens::core {

/**
 * @brief Decides when a resting pointer has rested long enough to look up the word under it.
 *
 * The clock belongs to the caller, in milliseconds, so the rule runs without a timer. A move of more
 * than the slop starts a new rest. A rest fires once, then stays quiet until the pointer moves again.
 */
class HoverDwell {
public:
    /// @param dwellMs How long the pointer must stay within the slop before the lookup fires.
    /// @param slopPx  How far the pointer may drift and still count as resting.
    explicit HoverDwell(long long dwellMs = 1000, int slopPx = 4);

    /// @brief Feed one pointer sample.
    /// @return True exactly once per rest, on the sample that completes the dwell.
    bool update(long long nowMs, int x, int y);

private:
    long long dwellMs_;
    int       slopPx_;
    bool      anchored_ = false;
    bool      fired_    = false;
    int       x_        = 0;
    int       y_        = 0;
    long long since_    = 0;
};

/**
 * @brief The word a hover lookup sends for a token the text control handed over.
 * @param token The text of one word, possibly with spaces around it, as UI Automation gives it.
 * @return The token without its surrounding spaces when it is a word, otherwise empty.
 * @note A word is two or more ASCII letters, with apostrophes allowed inside (`What's`). Digits,
 *       other punctuation and a lone letter are refused: the classifier would answer them as an
 *       entity or a sentence, and the reader only pointed at a word.
 */
std::string hoverWordFrom(std::string_view token);

}
