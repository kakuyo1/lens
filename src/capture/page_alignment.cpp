/** @file page_alignment.cpp
 * @brief Run-of-three anchoring of OCR words to the page's word sequence.
 */
#include "page_alignment.h"

#include <limits>
#include <unordered_set>
#include <utility>

#include "util/text.h"

namespace lens::capture {
namespace {

constexpr std::size_t kRepeated = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kRun      = 3;

std::string runKey(std::string const& a, std::string const& b, std::string const& c)
{
    // The unit separator cannot occur in a normalized word, so two different runs never share a key.
    return a + '\x1f' + b + '\x1f' + c;
}

}

std::string normalizedWord(std::string_view const text)
{
    std::string out;
    for (char const c : text)
        if (util::isAsciiAlnum(c)) out.push_back(util::asciiLower(c));
    return out;
}

namespace {

/// One word of page text: lower-cased, and whether the page wrote its first letter in capitals.
struct PageToken {
    std::string text;
    bool        capitalized = false;
};

std::vector<PageToken> pageTokens(std::string_view const text)
{
    std::vector<PageToken> tokens;
    PageToken              current;
    for (char const c : text) {
        if (util::isAsciiAlnum(c)) {
            if (current.text.empty()) current.capitalized = util::isAsciiUpper(c);
            current.text.push_back(util::asciiLower(c));
            continue;
        }
        if (not current.text.empty()) tokens.push_back(std::exchange(current, {}));
    }
    if (not current.text.empty()) tokens.push_back(std::move(current));
    return tokens;
}

}

std::vector<std::string> pageWords(std::string_view const text)
{
    std::vector<std::string> words;
    for (auto& token : pageTokens(text))
        words.push_back(std::move(token.text));
    return words;
}

std::vector<bool> nameFlags(std::string_view const text)
{
    auto const tokens = pageTokens(text);
    // A word the page also writes with a lower-case first letter is a common word, capitalized only where it opens a sentence.
    std::unordered_set<std::string> lowerOnPage;
    for (auto const& token : tokens)
        if (not token.capitalized) lowerOnPage.insert(token.text);

    std::vector<bool> names;
    names.reserve(tokens.size());
    for (auto const& token : tokens)
        names.push_back(token.capitalized and lowerOnPage.count(token.text) == 0);
    return names;
}

std::unordered_set<std::string> nameSet(std::string_view const text)
{
    auto const                      words = pageWords(text);
    auto const                      flags = nameFlags(text);
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < words.size(); ++i)
        if (flags[i]) names.insert(words[i]);
    return names;
}

PageIndex::PageIndex(std::vector<std::string> words)
    : words_{std::move(words)}
{
    for (std::size_t i = 0; i + kRun <= words_.size(); ++i) {
        auto const [slot, fresh] = runs_.try_emplace(runKey(words_[i], words_[i + 1], words_[i + 2]), i);
        if (not fresh) slot->second = kRepeated;
    }
}

std::vector<WordAnchor> PageIndex::align(std::vector<std::string> const& ocr) const
{
    std::vector<WordAnchor> anchors;
    if (ocr.size() < kRun) return anchors;
    for (std::size_t i = 0; i < ocr.size(); ++i) {
        // Try the runs that start at the word, then one and two words earlier; the first unique one places it.
        for (std::size_t back = 0; back < kRun and back <= i; ++back) {
            auto const start = i - back;
            if (start + kRun > ocr.size()) continue;
            auto const found = runs_.find(runKey(ocr[start], ocr[start + 1], ocr[start + 2]));
            if (found == runs_.end() or found->second == kRepeated) continue;
            anchors.push_back({.ocrIndex = i, .pageIndex = found->second + back});
            break;
        }
    }
    return anchors;
}

std::size_t PageIndex::size() const noexcept
{
    return words_.size();
}

}
