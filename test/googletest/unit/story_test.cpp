/**
 * @file story_test.cpp
 * @brief The weekly story's word draw and length cut, offline (core/document/story.h).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "core/document/story.h"

using lens::core::countWords;
using lens::core::kStoryMaxWords;
using lens::core::kStoryMinWords;
using lens::core::pickStoryWords;
using lens::core::truncateWords;

namespace {

std::vector<std::string> lemmas(std::size_t count)
{
    std::vector<std::string> words;
    for (std::size_t i = 0; i < count; ++i)
        words.push_back("word" + std::to_string(i));
    return words;
}

}

TEST(Story, AWeekWithFewerThanTenLemmasWritesFromAllOfThem)
{
    std::mt19937 rng{1};
    auto const   all   = lemmas(kStoryMinWords - 1);
    auto         drawn = pickStoryWords(all, rng);
    std::sort(drawn.begin(), drawn.end());
    auto expected = all;
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(drawn, expected);
}

TEST(Story, ALargerWeekDrawsBetweenTenAndFifteenDistinctLemmasFromThatWeek)
{
    auto const            all = lemmas(40);
    std::set<std::string> known(all.begin(), all.end());
    for (unsigned seed = 1; seed <= 50; ++seed) {
        std::mt19937 rng{seed};
        auto const   drawn = pickStoryWords(all, rng);
        EXPECT_GE(drawn.size(), kStoryMinWords) << "seed " << seed;
        EXPECT_LE(drawn.size(), kStoryMaxWords) << "seed " << seed;
        std::set<std::string> distinct(drawn.begin(), drawn.end());
        EXPECT_EQ(distinct.size(), drawn.size()) << "a lemma was drawn twice, seed " << seed;
        for (auto const& lemma : drawn)
            EXPECT_TRUE(known.contains(lemma)) << lemma << " was not one of the week's lemmas";
    }
}

TEST(Story, TheSameSeedDrawsTheSameWords)
{
    auto const   all = lemmas(30);
    std::mt19937 first{42};
    std::mt19937 second{42};
    EXPECT_EQ(pickStoryWords(all, first), pickStoryWords(all, second));
}

TEST(Story, CountWordsSeparatesOnAnyWhitespaceRun)
{
    EXPECT_EQ(countWords(""), 0u);
    EXPECT_EQ(countWords("  one   two\n\nthree  "), 3u);
}

TEST(Story, ATextWithinTheLimitIsKeptWhole)
{
    std::string const text = "She read the sign twice.\n\nThen she left.";
    EXPECT_EQ(truncateWords(text, 300), text);
}

TEST(Story, ALongerTextIsCutAfterItsLastKeptWordWithoutTrailingSpace)
{
    std::string text;
    for (int i = 0; i < 350; ++i)
        text += "word" + std::to_string(i) + (i % 10 == 9 ? "\n\n" : " ");

    auto const cut = truncateWords(text, 300);
    EXPECT_EQ(countWords(cut), 300u);
    EXPECT_FALSE(cut.empty());
    EXPECT_NE(cut.back(), ' ');
    EXPECT_NE(cut.back(), '\n');
    EXPECT_EQ(text.compare(0, cut.size(), cut), 0) << "the cut is not a prefix of the story";
}
