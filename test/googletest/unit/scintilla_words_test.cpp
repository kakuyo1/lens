/** @file scintilla_words_test.cpp
 * @brief The word units a Scintilla document is split into, with no window in the test.
 */

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "capture/scintilla_words.h"

namespace {

using lens::capture::scintillaWordUnits;

/// @return The text of every unit, in reading order.
std::vector<std::string> spellings(std::string_view const text)
{
    std::vector<std::string> out;
    for (auto const& unit : scintillaWordUnits(text))
        out.push_back(std::string{text.substr(unit.start, unit.end - unit.start)});
    return out;
}

TEST(ScintillaWords, SplitsWordsAndKeepsTrailingPunctuation)
{
    EXPECT_EQ(spellings("The semicolon is a curious piece of punctuation."),
              (std::vector<std::string>{"The", "semicolon", "is", "a", "curious", "piece", "of", "punctuation."}));
}

TEST(ScintillaWords, LettersEndIsWhereTheLineStops)
{
    auto const units = scintillaWordUnits("punctuation. next");
    ASSERT_EQ(units.size(), 2u);
    EXPECT_EQ(units[0].start, 0u);
    EXPECT_EQ(units[0].lettersEnd, 11u);
    EXPECT_EQ(units[0].end, 12u);
    EXPECT_EQ(units[1].start, 13u);
}

TEST(ScintillaWords, ApostropheInsideAWordStaysInside)
{
    EXPECT_EQ(spellings("don't, it's"), (std::vector<std::string>{"don't,", "it's"}));
    // An apostrophe at either edge is not part of the word: the unit starts at the first letter and
    // the trailing quote is left to the caller's trimming.
    EXPECT_EQ(spellings("'quoted'"), (std::vector<std::string>{"quoted"}));
}

TEST(ScintillaWords, LineBreaksAndTabsSeparateUnits)
{
    auto const units = scintillaWordUnits("alpha,\r\n\tbeta");
    ASSERT_EQ(units.size(), 2u);
    EXPECT_EQ(units[0].end, 6u); // the comma, not the line break
    EXPECT_EQ(units[1].start, 9u);
}

TEST(ScintillaWords, NonAsciiBytesSeparateEnglishWords)
{
    // A CJK run has no ASCII letters; it must not glue the two English words into one unit.
    auto const units = scintillaWordUnits("alpha中文beta");
    ASSERT_EQ(units.size(), 2u);
    EXPECT_EQ(units[0].start, 0u);
    EXPECT_EQ(units[0].end, 5u);
    EXPECT_EQ(units[1].start, 11u);
    EXPECT_EQ(units[1].end, 15u);
}

TEST(ScintillaWords, DigitsAreWordsAndEmptyDocumentsHaveNone)
{
    EXPECT_TRUE(scintillaWordUnits("").empty());
    EXPECT_TRUE(scintillaWordUnits(" \r\n\t.,!").empty());
    // An underscore is punctuation here, the way the trimming rule already treats it, so it trails
    // the unit it follows instead of joining two identifiers.
    EXPECT_EQ(spellings("SCI_GETTEXTRANGEFULL 2039"),
              (std::vector<std::string>{"SCI_", "GETTEXTRANGEFULL", "2039"}));
}

TEST(ScintillaWords, EveryUnitHasLettersAndStaysInBounds)
{
    std::string const text  = "A reader who hesitates between two clauses; SCI_GETTEXTRANGEFULL(2039).";
    auto const        units = scintillaWordUnits(text);
    EXPECT_FALSE(units.empty());
    for (auto const& unit : units) {
        EXPECT_LT(unit.start, unit.lettersEnd);
        EXPECT_LE(unit.lettersEnd, unit.end);
        EXPECT_LE(unit.end, text.size());
        for (std::size_t i = unit.start; i < unit.lettersEnd; ++i)
            EXPECT_NE(text[i], '\n');
    }
}

}
