/**
 * @file pet_mood_test.cpp
 * @brief The mood's pure tables: the day stretches, the two chance factors, and a scene's weight under a mood.
 */

#include <gtest/gtest.h>

#include "core/domain/pet/pet_mood.h"

namespace {

using lens::core::pet::DayPeriod;
using lens::core::pet::PetMood;
using lens::core::pet::SceneMood;
using lens::core::pet::dayPeriodOf;
using lens::core::pet::moodNapFactor;
using lens::core::pet::moodSceneFactor;
using lens::core::pet::sceneWeight;

TEST(PetMood, DayPeriodBoundaries)
{
    // Morning 5:00-11:00, afternoon 11:00-17:00, evening 17:00-23:00, night the rest; strays clamp in.
    EXPECT_EQ(dayPeriodOf(4 * 60 + 59), DayPeriod::Night);
    EXPECT_EQ(dayPeriodOf(5 * 60), DayPeriod::Morning);
    EXPECT_EQ(dayPeriodOf(10 * 60 + 59), DayPeriod::Morning);
    EXPECT_EQ(dayPeriodOf(11 * 60), DayPeriod::Afternoon);
    EXPECT_EQ(dayPeriodOf(16 * 60 + 59), DayPeriod::Afternoon);
    EXPECT_EQ(dayPeriodOf(17 * 60), DayPeriod::Evening);
    EXPECT_EQ(dayPeriodOf(22 * 60 + 59), DayPeriod::Evening);
    EXPECT_EQ(dayPeriodOf(23 * 60), DayPeriod::Night);
    EXPECT_EQ(dayPeriodOf(24 * 60 - 1), DayPeriod::Night);
    EXPECT_EQ(dayPeriodOf(-5), DayPeriod::Night);
    EXPECT_EQ(dayPeriodOf(24 * 60), DayPeriod::Night);
}

TEST(PetMood, TheSceneChanceFollowsTheDayAndTheContexts)
{
    PetMood morning;
    morning.minuteOfDay = 8 * 60;
    PetMood afternoon;
    afternoon.minuteOfDay = 14 * 60;
    PetMood evening;
    evening.minuteOfDay = 20 * 60;
    PetMood night;
    night.minuteOfDay = 1 * 60;

    EXPECT_DOUBLE_EQ(moodSceneFactor(morning), 1.0);
    EXPECT_DOUBLE_EQ(moodSceneFactor(afternoon), 1.0);
    EXPECT_DOUBLE_EQ(moodSceneFactor(evening), 1.25);
    EXPECT_DOUBLE_EQ(moodSceneFactor(night), 0.5);

    // A long reading lifts the scene chance, being just home lowers it; the two multiply.
    auto afterReading                = evening;
    afterReading.justFinishedReading = true;
    EXPECT_DOUBLE_EQ(moodSceneFactor(afterReading), 1.25 * 1.5);

    auto justHome         = morning;
    justHome.justReturned = true;
    EXPECT_DOUBLE_EQ(moodSceneFactor(justHome), 0.5);
}

TEST(PetMood, TheNapChanceFollowsTheDayAndTheContexts)
{
    PetMood morning;
    morning.minuteOfDay = 8 * 60;
    PetMood afternoon;
    afternoon.minuteOfDay = 14 * 60;
    PetMood evening;
    evening.minuteOfDay = 20 * 60;
    PetMood night;
    night.minuteOfDay = 1 * 60;

    EXPECT_DOUBLE_EQ(moodNapFactor(morning), 0.5);
    EXPECT_DOUBLE_EQ(moodNapFactor(afternoon), 2.0);
    EXPECT_DOUBLE_EQ(moodNapFactor(evening), 1.0);
    EXPECT_DOUBLE_EQ(moodNapFactor(night), 2.0);

    // Just home it naps, just after a long reading it is still up; the two multiply.
    auto afterReading                = afternoon;
    afterReading.justFinishedReading = true;
    EXPECT_DOUBLE_EQ(moodNapFactor(afterReading), 1.0);

    auto justHome         = afternoon;
    justHome.justReturned = true;
    EXPECT_DOUBLE_EQ(moodNapFactor(justHome), 4.0);
}

TEST(PetMood, ASceneWithoutATableWeighsOneEverywhere)
{
    PetMood mood;
    mood.minuteOfDay = 14 * 60;
    EXPECT_DOUBLE_EQ(sceneWeight(SceneMood{}, mood), 1.0);
}

TEST(PetMood, TheTableWeightIsTheStretchTimesTheContexts)
{
    SceneMood table;
    table.morning      = 3.0;
    table.afternoon    = 0.5;
    table.night        = 4.0;
    table.afterReading = 2.0;
    table.afterReturn  = 1.5;

    PetMood morning;
    morning.minuteOfDay = 8 * 60;
    EXPECT_DOUBLE_EQ(sceneWeight(table, morning), 3.0);

    PetMood afternoon;
    afternoon.minuteOfDay = 14 * 60;
    EXPECT_DOUBLE_EQ(sceneWeight(table, afternoon), 0.5);

    // Evening has no entry, so it keeps 1; the after-return context still multiplies.
    PetMood justHome;
    justHome.minuteOfDay  = 20 * 60;
    justHome.justReturned = true;
    EXPECT_DOUBLE_EQ(sceneWeight(table, justHome), 1.5);

    // The stretch and both contexts multiply together.
    PetMood nightReader;
    nightReader.minuteOfDay         = 1 * 60;
    nightReader.justFinishedReading = true;
    nightReader.justReturned        = true;
    EXPECT_DOUBLE_EQ(sceneWeight(table, nightReader), 4.0 * 2.0 * 1.5);
}

} // namespace
