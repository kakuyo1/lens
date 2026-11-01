/**
 * @file story.cpp
 * @brief The weekly story's word choice and length cut, for story.h.
 */

#include "core/document/story.h"

#include <algorithm>
#include <cctype>

namespace lens::core {

namespace {

/// @return Whether @p c is whitespace, as the story's word count and cut read it.
bool isSpace(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

}

std::vector<std::string> pickStoryWords(std::vector<std::string> lemmas, std::mt19937& rng)
{
    std::shuffle(lemmas.begin(), lemmas.end(), rng);
    if (lemmas.size() < kStoryMinWords)
        return lemmas;
    std::uniform_int_distribution<std::size_t> count(kStoryMinWords, std::min(kStoryMaxWords, lemmas.size()));
    lemmas.resize(count(rng));
    return lemmas;
}

std::size_t countWords(const std::string& text)
{
    std::size_t words  = 0;
    bool        inWord = false;
    for (const char c : text) {
        if (!isSpace(c) && !inWord)
            ++words;
        inWord = !isSpace(c);
    }
    return words;
}

std::string truncateWords(const std::string& text, std::size_t maxWords)
{
    std::size_t words  = 0;
    bool        inWord = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool space = isSpace(text[i]);
        if (!space && !inWord && ++words > maxWords) {
            std::size_t end = i;
            while (end > 0 && isSpace(text[end - 1]))
                --end;
            return text.substr(0, end);
        }
        inWord = !space;
    }
    return text;
}

}
