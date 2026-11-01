#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <vector>

/**
 * @file story.h
 * @brief The weekly story's word choice and its length limit (docs/adr/0152). Qt-free like reading_profile.h: the
 *        engine is passed in, so a draw can be repeated in a test.
 */

namespace lens::core {

/// @brief The fewest words a story is written from; a week with fewer lemmas uses all of them.
inline constexpr std::size_t kStoryMinWords = 10;

/// @brief The most words a story is written from.
inline constexpr std::size_t kStoryMaxWords = 15;

/// @brief The most words of story the panel shows; a longer answer is cut to this.
inline constexpr std::size_t kStoryTextMaxWords = 300;

/**
 * @brief Draw the lemmas one story is written from.
 *
 * With fewer than kStoryMinWords lemmas, all of them. Otherwise the count is drawn uniformly from kStoryMinWords up to
 * the smaller of kStoryMaxWords and the lemma count, and that many lemmas are drawn without replacement.
 * @param lemmas Distinct lemmas the window looked up.
 * @param rng    Source of the draw.
 * @return The chosen lemmas, in no particular order.
 */
std::vector<std::string> pickStoryWords(std::vector<std::string> lemmas, std::mt19937& rng);

/// @return The number of whitespace-separated words in @p text.
std::size_t countWords(const std::string& text);

/**
 * @brief Cut a story to its first @p maxWords words.
 * @return @p text unchanged when it has no more than @p maxWords words; otherwise the text up to the end of the last kept
 *         word, without the whitespace after it.
 */
std::string truncateWords(const std::string& text, std::size_t maxWords);

}
