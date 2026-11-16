/** @file page_alignment.h
 * @brief Page-text analysis: word splitting, the name set, the page's language, and the anchoring
 *        of OCR words on the text the reader's accessibility tree exposes.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lens::capture {

/// @brief Lower-case letters and digits of @p text, with everything else dropped.
/// @return The word in the form both sides of the alignment compare; empty when there is no letter or digit.
std::string normalizedWord(std::string_view text);

/// @brief Split page text into words: each run of letters and digits, lower-cased, in reading order.
std::vector<std::string> pageWords(std::string_view text);

/// @brief Mark the words of page text that are names: capitalized, and either never written with a
///        lower-case first letter on the page, or written capitalized where a sentence had begun
///        (docs/adr/0161) -- a name the page spells lower-case in a URL or a byline still counts.
/// @return One flag per word, parallel to pageWords(text).
std::vector<bool> nameFlags(std::string_view text);

/// @brief The names of page text: the words nameFlags() marks, each once, spelled as pageWords() gives them.
/// @return The set; a word is a name when its normalizedWord() is in it.
std::unordered_set<std::string> nameSet(std::string_view text);

/// @brief Whether a page's text reads as English, as another language, or as too little to say.
enum class PageLanguage { Unknown,
                          English,
                          Foreign };

/// @brief Judge a page's language from the share of English function words among its letter tokens.
/// @return Foreign when a CJK majority carries the page, or when enough letter tokens are present
///         but too few of them are English function words; Unknown when the text is malformed or
///         holds too few letter tokens to judge. The scan treats Unknown as English (fail open), so
///         a short page or a transient empty read is never held back (docs/adr/0187).
PageLanguage pageLanguageOf(std::string_view text);

/// @brief One OCR word placed on the page text.
struct WordAnchor {
    std::size_t ocrIndex  = 0; ///< Index into the OCR words given to PageIndex::align().
    std::size_t pageIndex = 0; ///< Index into the words the PageIndex was built from.
};

/// @brief Anchors OCR words to a page's word sequence through their neighbours.
///
/// A word is anchored when one run of three consecutive words that contains it occurs exactly once in
/// the page. A phrase repeated on the page anchors nothing, and a misread word only costs the runs it
/// sits in, so the words around it still anchor.
class PageIndex {
public:
    /// @param words The page's words in reading order, as pageWords() splits them.
    explicit PageIndex(std::vector<std::string> words);

    /// @param ocr The OCR words' normalized forms, in reading order.
    /// @return One anchor per placed word, in the order of @p ocr.
    [[nodiscard]] std::vector<WordAnchor> align(std::vector<std::string> const& ocr) const;

    /// @return How many words the page holds.
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<std::string>                     words_;
    std::unordered_map<std::string, std::size_t> runs_; ///< Index of the run's first word; kRepeated when it occurs more than once.
};

}
