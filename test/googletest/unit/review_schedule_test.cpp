/**
 * @file review_schedule_test.cpp
 * @brief The review curve, offline: how answers move a word's level and due time, which word
 *        the pet asks about next, and the check window each frequency maps to.
 */

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/review_schedule.h"

namespace {

using lens::core::ReviewCandidate;
using lens::core::ReviewFrequency;
using lens::core::ReviewState;

constexpr long long kMinute = 60LL * 1000;
constexpr long long kHour   = 60 * kMinute;
constexpr long long kDay    = 24 * kHour;
constexpr long long kNow    = 1'800'000'000'000LL;

} // namespace

TEST(ReviewSchedule, FreshStateIsLevelOneAndDueADayAfterTheLookup)
{
    const auto state = lens::core::freshState(kNow);

    EXPECT_EQ(state.level, 1);
    EXPECT_EQ(state.due, kNow + kDay);
    EXPECT_EQ(state.asked, 0);
}

TEST(ReviewSchedule, RememberingMovesUpAndRestartsTheIntervalFromTheAnswer)
{
    const auto next = lens::core::afterAnswer(ReviewState{1, kNow, 0}, true, kNow);

    EXPECT_EQ(next.level, 2);
    EXPECT_EQ(next.due, kNow + 3 * kDay);
}

TEST(ReviewSchedule, ForgettingMovesDownAndComesBackInTenMinutesFromLevelOne)
{
    const auto next = lens::core::afterAnswer(ReviewState{1, kNow, 0}, false, kNow);

    EXPECT_EQ(next.level, 0);
    EXPECT_EQ(next.due, kNow + 10 * kMinute);
}

TEST(ReviewSchedule, ForgettingAtTheBottomStaysAtZero)
{
    const auto next = lens::core::afterAnswer(ReviewState{0, kNow, 0}, false, kNow);

    EXPECT_EQ(next.level, 0);
    EXPECT_EQ(next.due, kNow + 10 * kMinute);
}

TEST(ReviewSchedule, RememberingAtTheTopStaysAtFive)
{
    const auto next = lens::core::afterAnswer(ReviewState{5, kNow, 0}, true, kNow);

    EXPECT_EQ(next.level, 5);
    EXPECT_EQ(next.due, kNow + 60 * kDay);
}

TEST(ReviewSchedule, IgnoringKeepsTheWordDueAndStampsTheCooldown)
{
    const auto state = ReviewState{3, kNow - kHour, 0};

    const auto next = lens::core::afterIgnore(state, kNow);

    EXPECT_EQ(next.level, 3);
    EXPECT_EQ(next.due, kNow - kHour);
    EXPECT_EQ(next.asked, kNow);
}

TEST(ReviewSchedule, PicksTheEarliestDueWordAndIgnoresTheRest)
{
    const std::vector<ReviewCandidate> candidates{
        {"later", ReviewState{2, kNow - kMinute, 0}},
        {"earliest", ReviewState{2, kNow - 2 * kHour, 0}},
        {"not-yet", ReviewState{2, kNow + kHour, 0}},
    };

    EXPECT_EQ(lens::core::pickDue(candidates, kNow), "earliest");
}

TEST(ReviewSchedule, SkipsAWordInCooldownAndTakesTheNextDueOne)
{
    const std::vector<ReviewCandidate> candidates{
        {"cooling", ReviewState{2, kNow - 2 * kHour, kNow - kHour}},
        {"free", ReviewState{2, kNow - kMinute, 0}},
    };

    EXPECT_EQ(lens::core::pickDue(candidates, kNow), "free");
}

TEST(ReviewSchedule, CooldownEndsTwoHoursAfterTheIgnore)
{
    const std::vector<ReviewCandidate> candidates{
        {"again", ReviewState{2, kNow - 3 * kHour, kNow - 2 * kHour}},
    };

    EXPECT_EQ(lens::core::pickDue(candidates, kNow), "again");
}

TEST(ReviewSchedule, PicksNothingWhenNothingIsDueOrEveryDueWordIsCooling)
{
    const std::vector<ReviewCandidate> notDue{
        {"not-yet", ReviewState{2, kNow + kHour, 0}},
    };
    const std::vector<ReviewCandidate> cooling{
        {"cooling", ReviewState{2, kNow - kHour, kNow - kMinute}},
    };

    EXPECT_EQ(lens::core::pickDue(notDue, kNow), std::nullopt);
    EXPECT_EQ(lens::core::pickDue(cooling, kNow), std::nullopt);
}

TEST(ReviewSchedule, FrequencyOffHasNoCheckWindowAndEachOtherLevelHasOne)
{
    EXPECT_FALSE(lens::core::checkWindow(ReviewFrequency::Off).has_value());

    const auto medium = lens::core::checkWindow(ReviewFrequency::Every30To60Min);
    ASSERT_TRUE(medium.has_value());
    EXPECT_EQ(medium->minMs, 30 * kMinute);
    EXPECT_EQ(medium->maxMs, 60 * kMinute);

    const auto longest = lens::core::checkWindow(ReviewFrequency::Every1To2Hours);
    ASSERT_TRUE(longest.has_value());
    EXPECT_EQ(longest->maxMs, 2 * kHour);

    // The newest level, appended to the enum rather than inserted so the stored values of the
    // older ones stay where they were (review_schedule.h).
    const auto shortest = lens::core::checkWindow(ReviewFrequency::Every5To15Min);
    ASSERT_TRUE(shortest.has_value());
    EXPECT_EQ(shortest->minMs, 5 * kMinute);
    EXPECT_EQ(shortest->maxMs, 15 * kMinute);
}
