/** @file page_alignment.h
 * @brief Places OCR words on the page text the reader's accessibility tree exposes.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lens::capture {

/// @brief Lower-case letters and digits of @p text, with everything else dropped.
/// @return The word in the form both sides of the alignment compare; empty when there is no letter or digit.
std::string normalizedWord(std::string_view text);

/// @brief Split page text into words: each run of letters and digits, lower-cased, in reading order.
std::vector<std::string> pageWords(std::string_view text);

/// @brief Mark the words of page text that are names: capitalized, and never written with a lower-case first letter on the page.
/// @return One flag per word, parallel to pageWords(text).
std::vector<bool> nameFlags(std::string_view text);

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
