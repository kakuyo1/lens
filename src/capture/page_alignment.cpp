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

/// One word of page text: lower-cased, whether the page wrote its first letter in capitals, and
/// whether it wrote the word where a sentence had begun.
struct PageToken {
    std::string text;
    bool        capitalized   = false;
    bool        sentenceStart = true;
};

std::vector<PageToken> pageTokens(std::string_view const text)
{
    std::vector<PageToken> tokens;
    PageToken              current;
    // A sentence begins the page and resumes after a terminator; a newline bounds list items and
    // headings the same way, so a bullet's capital does not read as mid-sentence.
    bool atBoundary = true;
    for (char const c : text) {
        if (util::isAsciiAlnum(c)) {
            if (current.text.empty()) {
                current.capitalized   = util::isAsciiUpper(c);
                current.sentenceStart = atBoundary;
            }
            current.text.push_back(util::asciiLower(c));
            atBoundary = false;
            continue;
        }
        if (not current.text.empty()) tokens.push_back(std::exchange(current, {}));
        if (c == '.' || c == '!' || c == '?' || c == '\n' || c == '\r') atBoundary = true;
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
    // A word the page also writes with a lower-case first letter is a common word, capitalized
    // only where it opens a sentence -- unless the page also capitalizes it mid-sentence, which
    // is a name the page spells lower-case somewhere: a URL, a byline, a tag (docs/adr/0161).
    std::unordered_set<std::string> lowerOnPage;
    std::unordered_set<std::string> midSentence;
    for (auto const& token : tokens) {
        if (not token.capitalized)
            lowerOnPage.insert(token.text);
        else if (not token.sentenceStart)
            midSentence.insert(token.text);
    }

    std::vector<bool> names;
    names.reserve(tokens.size());
    for (auto const& token : tokens)
        names.push_back(token.capitalized and
                        (lowerOnPage.count(token.text) == 0 or midSentence.count(token.text) != 0));
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

namespace {

/// Letter tokens a page must hold before its language is judged at all; below this the verdict is
/// Unknown and the scan fails open. A settings list or a heading-only page is not held back.
constexpr std::size_t kMinLetterTokens = 50;
/// The English function-word share, in percent, that separates an English page from another
/// language. Calibrated on representative prose: English runs 37-64%, French 6%, Spanish 5%,
/// German 1%, Italian 3%, pinyin 2% (docs/adr/0187). 14 leaves margin on both sides.
constexpr std::size_t kEnglishPercent = 14;

/// @brief Whether @p code is a CJK ideograph, a Japanese kana, or a Hangul syllable: the scripts a
///        page written for a Chinese, Japanese or Korean reader carries.
bool isCjk(char32_t const code)
{
    return (code >= 0x3400 and code <= 0x4DBF) or // CJK Extension A
           (code >= 0x4E00 and code <= 0x9FFF) or // CJK Unified Ideographs
           (code >= 0x3040 and code <= 0x30FF) or // Hiragana and Katakana
           (code >= 0xAC00 and code <= 0xD7AF);   // Hangul syllables
}

/// @brief The English function words an English page is full of and a page of another language is
///        not. Kept distinct from the Romance and Germanic homographs where it costs little: the
///        share, not any one word, carries the verdict.
std::unordered_set<std::string> const& englishMarkers()
{
    static std::unordered_set<std::string> const markers{
        "the", "be", "is", "are", "was", "were", "been", "being", "am", "and", "of", "to", "in", "that", "it", "for", "with", "as", "this", "but", "they", "have", "has", "had", "not", "you", "he", "she", "we", "his", "her", "its", "their", "them", "from", "by", "will", "can", "would", "there", "what", "which", "when", "who", "how", "all", "any", "both", "each", "few", "more", "most", "other", "some", "such", "only", "own", "same", "than", "too", "very", "just", "because", "until", "while", "about", "against", "between", "through", "during", "before", "after", "above", "below", "up", "down", "out", "off", "over", "under", "again", "further", "then", "once", "here", "where", "why", "does", "do", "did", "doing", "should", "could", "might", "must", "shall", "may", "these", "those", "having", "him", "himself", "herself", "itself", "themselves", "myself", "yourself", "ourselves", "yourselves", "my", "your", "our", "me", "us", "i", "a", "an", "or", "on", "at", "so", "if", "no", "nor", "yet", "either", "neither", "whether", "whose", "whom", "whoever", "whatever", "whenever", "wherever", "however", "moreover", "therefore", "thus", "hence", "although", "though", "unless", "since", "whereas", "whereby", "wherein", "upon", "onto", "into", "within", "without", "throughout", "toward", "towards", "amid", "amidst", "among", "amongst", "beside", "besides", "beyond", "underneath", "atop", "via", "per", "plus", "minus", "versus"};
    return markers;
}

}

PageLanguage pageLanguageOf(std::string_view const text)
{
    auto const code = util::decodeUtf8(text);
    if (not code) return PageLanguage::Unknown; // malformed bytes cannot be judged; fail open

    std::size_t cjk     = 0;
    std::size_t letters = 0;
    for (char32_t const c : *code) {
        if (isCjk(c))
            ++cjk;
        else if ((c >= 'A' and c <= 'Z') or (c >= 'a' and c <= 'z'))
            ++letters;
    }
    // A page carried mostly by CJK scripts is not an English page, whatever its ASCII words read as.
    if (cjk > letters) return PageLanguage::Foreign;

    auto const& markers = englishMarkers();
    std::size_t tokens  = 0;
    std::size_t hits    = 0;
    for (auto const& word : pageWords(text)) {
        bool letterOnly = not word.empty();
        for (char const c : word)
            if (util::isAsciiDigit(c)) {
                letterOnly = false;
                break;
            }
        if (not letterOnly) continue;
        ++tokens;
        if (markers.contains(word)) ++hits;
    }
    if (tokens < kMinLetterTokens) return PageLanguage::Unknown;
    return hits * 100 >= tokens * kEnglishPercent ? PageLanguage::English : PageLanguage::Foreign;
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
