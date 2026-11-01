/**
 * @file reading_profile_test.cpp
 * @brief The weekly reading profile, offline: what one window of the history counts to, and the
 *        summary the interpretation request carries.
 *
 * The window is the part that is easy to get wrong: a pop on the day before it or the day after it
 * must not count, and a word looked up twice must be one word and one repeat, not two words.
 */

#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/document/reading_profile.h"

namespace {

using lens::core::HistoryEntry;
using lens::core::ReadingProfile;
using lens::core::buildReadingProfile;
using lens::core::readingProfileSummary;
using lens::core::sharePercent;

/// @return A history row for @p lemma, stamped at local time @p minute.
HistoryEntry entry(std::string lemma, std::string minute)
{
    return HistoryEntry{std::move(lemma), std::move(minute), "new"};
}

/// @return A part-of-speech lookup over @p cached; a lemma it does not hold has none.
auto partsOf(std::map<std::string, std::string> cached)
{
    return [cached = std::move(cached)](std::string const& lemma) {
        const auto it = cached.find(lemma);
        return it == cached.end() ? std::string{} : it->second;
    };
}

const std::string kFrom = "2026-10-26";
const std::string kTo   = "2026-11-01";

}

TEST(ReadingProfile, CountsOnlyThePopsInsideTheWindow)
{
    const std::vector<HistoryEntry> history{
        entry("late", "2026-11-02 09:00"),
        entry("bank", "2026-11-01 23:59"),
        entry("scale", "2026-10-26 00:00"),
        entry("early", "2026-10-25 23:59"),
    };
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({}), kFrom, kTo);
    EXPECT_EQ(profile.pops, 2) << "both edges of the window are inclusive, and the days outside it are not counted";
    EXPECT_EQ(profile.words, 2);
}

TEST(ReadingProfile, TheLemmasAreEveryWordTheWindowLookedUpMostLookedUpFirst)
{
    const std::vector<HistoryEntry> history{
        entry("bank", "2026-10-27 09:00"),
        entry("bank", "2026-10-27 09:01"),
        entry("address", "2026-10-28 10:05"),
        entry("scale", "2026-10-30 12:00"),
        entry("late", "2026-11-02 09:00"),
    };
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({}), kFrom, kTo);
    EXPECT_EQ(profile.lemmas, (std::vector<std::string>{"bank", "address", "scale"})) << "the window's own words, outside the top five too, and none outside it";
}

TEST(ReadingProfile, ARepeatedWordIsOneWordAndOneRepeat)
{
    const std::vector<HistoryEntry> history{
        entry("bank", "2026-10-27 09:00"),
        entry("bank", "2026-10-27 09:01"),
        entry("bank", "2026-10-28 10:00"),
        entry("address", "2026-10-28 10:05"),
        entry("address", "2026-10-29 11:00"),
        entry("scale", "2026-10-30 12:00"),
    };
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({}), kFrom, kTo);
    EXPECT_EQ(profile.pops, 6);
    EXPECT_EQ(profile.words, 3);
    EXPECT_EQ(profile.repeated, 2) << "bank and address were each looked up twice; scale once";
    ASSERT_EQ(profile.top.size(), 3u);
    EXPECT_EQ(profile.top[0].lemma, "bank");
    EXPECT_EQ(profile.top[0].lookups, 3);
}

TEST(ReadingProfile, NewWordsAreTheDistinctOnesTheKnownSetDoesNotHold)
{
    const std::vector<HistoryEntry> history{
        entry("bank", "2026-10-27 09:00"),
        entry("scale", "2026-10-27 09:01"),
        entry("address", "2026-10-27 09:02"),
    };
    // The known set is the verdict (ADR 0147): a word the reader marked known is not new, whatever the stamp says.
    const std::unordered_set<std::string> known{"scale"};
    const ReadingProfile                  profile = buildReadingProfile(history, known, partsOf({}), kFrom, kTo);
    EXPECT_EQ(profile.fresh, 2);
    EXPECT_EQ(sharePercent(profile.fresh, profile.words), 67);
}

TEST(ReadingProfile, PartsOfSpeechCountDistinctWordsAndNameTheUncached)
{
    const std::vector<HistoryEntry> history{
        entry("bank", "2026-10-27 09:00"),
        entry("bank", "2026-10-27 09:01"),
        entry("scale", "2026-10-27 09:02"),
        entry("address", "2026-10-27 09:03"),
    };
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({{"bank", "noun"}, {"scale", "noun"}}), kFrom, kTo);
    ASSERT_EQ(profile.partsOfSpeech.size(), 1u);
    EXPECT_EQ(profile.partsOfSpeech[0], (std::pair<std::string, int>{"noun", 2}));
    EXPECT_EQ(profile.uncached, 1) << "address has no cached part of speech and is named as such, not dropped";
}

TEST(ReadingProfile, EveryPopCountsIntoItsLocalHour)
{
    const std::vector<HistoryEntry> history{
        entry("bank", "2026-10-27 09:12"),
        entry("scale", "2026-10-27 09:40"),
        entry("address", "2026-10-27 21:05"),
    };
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({}), kFrom, kTo);
    EXPECT_EQ(profile.hours[9], 2);
    EXPECT_EQ(profile.hours[21], 1);
    int total = 0;
    for (const int count : profile.hours)
        total += count;
    EXPECT_EQ(total, profile.pops);
}

TEST(ReadingProfile, TheTopWordsAreTheFiveMostLookedUpWithAlphabeticalTies)
{
    std::vector<HistoryEntry> history;
    // Nine pops over six words: quay three times, bank twice, and four words once each.
    for (const char* lemma : {"quay", "bank", "quay", "bank", "quay", "wharf", "dock", "pier", "berth"})
        history.push_back(entry(lemma, "2026-10-27 09:00"));
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({}), kFrom, kTo);
    ASSERT_EQ(profile.top.size(), lens::core::kProfileTopWords);
    EXPECT_EQ(profile.top[0].lemma, "quay");
    EXPECT_EQ(profile.top[1].lemma, "bank");
    // Once each after that, in alphabetical order, so the same history always names the same five.
    EXPECT_EQ(profile.top[2].lemma, "berth");
    EXPECT_EQ(profile.top[3].lemma, "dock");
    EXPECT_EQ(profile.top[4].lemma, "pier");
}

TEST(ReadingProfile, ASharePercentRoundsToTheNearestWholeAndIsZeroOfNothing)
{
    EXPECT_EQ(sharePercent(1, 3), 33);
    EXPECT_EQ(sharePercent(2, 3), 67);
    EXPECT_EQ(sharePercent(0, 0), 0);
}

TEST(ReadingProfile, TheSummaryCarriesTheCountsAndTheLemmasButNoMinute)
{
    const std::vector<HistoryEntry> history{
        entry("bank", "2026-10-27 09:12"),
        entry("bank", "2026-10-28 09:40"),
        entry("scale", "2026-10-29 21:05"),
    };
    const ReadingProfile profile = buildReadingProfile(history, {}, partsOf({{"bank", "noun"}}), kFrom, kTo);
    const std::string    summary = readingProfileSummary(profile, kFrom, kTo);
    EXPECT_NE(summary.find("Week 2026-10-26 to 2026-11-01."), std::string::npos);
    EXPECT_NE(summary.find("3 lookups, 2 distinct words"), std::string::npos);
    EXPECT_NE(summary.find("Most looked up: bank 2, scale 1."), std::string::npos);
    EXPECT_NE(summary.find("noun 1"), std::string::npos);
    EXPECT_EQ(summary.find("09:12"), std::string::npos) << "a pop's own minute never leaves the machine";
}
