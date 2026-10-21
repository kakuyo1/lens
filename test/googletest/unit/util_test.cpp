/**
 * @file util_test.cpp
 * @brief Offline tests for the Qt-free shared utility boundary.
 */

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "util/text.h"

TEST(UtilText, ExtractsTheDateFromAStoredMinute)
{
    EXPECT_EQ(lens::util::datePart("2026-10-03 14:20"), "2026-10-03");
    EXPECT_TRUE(lens::util::datePart("2026-10").empty());
}

TEST(UtilText, ClampedAddNeverProducesANegativeTally)
{
    EXPECT_EQ(lens::util::clampedAdd(4, -1), 3);
    EXPECT_EQ(lens::util::clampedAdd(0, -1), 0);
    EXPECT_EQ(lens::util::clampedAdd(4, 2), 6);
}

TEST(UtilText, RecognisesTheAsciiCharacterClasses)
{
    EXPECT_TRUE(lens::util::isAsciiUpper('A'));
    EXPECT_TRUE(lens::util::isAsciiLower('z'));
    EXPECT_TRUE(lens::util::isAsciiLetter('q'));
    EXPECT_FALSE(lens::util::isAsciiLetter('0'));

    EXPECT_TRUE(lens::util::isAsciiDigit('7'));
    EXPECT_FALSE(lens::util::isAsciiDigit('x'));

    EXPECT_TRUE(lens::util::isAsciiAlnum('Q'));
    EXPECT_TRUE(lens::util::isAsciiAlnum('4'));
    EXPECT_FALSE(lens::util::isAsciiAlnum('_'));

    EXPECT_TRUE(lens::util::isAsciiSpace('\v'));
    EXPECT_FALSE(lens::util::isAsciiSpace('\0'));

    // A UTF-8 lead byte is not a letter, however it compares as a signed char.
    EXPECT_FALSE(lens::util::isAsciiUpper(static_cast<char>(0xC9)));
}

TEST(UtilText, FoldsAsciiCaseAndLeavesOtherBytesAlone)
{
    EXPECT_EQ(lens::util::asciiLower("QML API 2"), "qml api 2");
    EXPECT_EQ(lens::util::asciiLower("\xC3\x89"), "\xC3\x89");
}

TEST(UtilText, DecodesWellFormedUtf8)
{
    const auto check = [](std::string_view text, std::vector<char32_t> expected) {
        auto const points = lens::util::decodeUtf8(text);
        ASSERT_TRUE(points) << text;
        EXPECT_EQ(*points, expected) << text;
    };

    check("A", {0x41});
    check("\xC3\xA9", {0xE9});
    check("\xE6\xB1\x89", {0x6C49});
    check("\xF0\x9F\x98\x80", {0x1F600});
}

TEST(UtilText, RefusesMalformedUtf8)
{
    for (auto const text : {"\x80", "\xC3", "\xC0\xAF", "\xE2\x28\xA1", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80"})
        EXPECT_FALSE(lens::util::decodeUtf8(text)) << text;
}

TEST(UtilText, LocalMinuteNowUsesTheStoredFormat)
{
    const auto minute = lens::util::localMinuteNow();
    ASSERT_EQ(minute.size(), 16u);
    for (const std::size_t at : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u})
        EXPECT_TRUE(lens::util::isAsciiDigit(minute[at])) << "position " << at;
    EXPECT_EQ(minute[4], '-');
    EXPECT_EQ(minute[7], '-');
    EXPECT_EQ(minute[10], ' ');
    EXPECT_EQ(minute[13], ':');
    EXPECT_EQ(lens::util::datePart(minute).size(), 10u);
}
