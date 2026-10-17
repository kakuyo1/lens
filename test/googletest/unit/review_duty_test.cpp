/**
 * @file review_duty_test.cpp
 * @brief ReviewDuty, offline: when the pet may ask, which word it asks, and what the answer, the pass-over,
 *        and the frequency do to the stored state. The clock is passed in, so no timer has to fire.
 */

#include <filesystem>
#include <memory>
#include <string>

#include <QDateTime>
#include <QString>
#include <gtest/gtest.h>

#include "app/review_duty.h"
#include "app/storage_duty.h"
#include "core/known_store.h"

namespace {

using lens::app::ReviewDuty;
using lens::app::StorageDuty;
using lens::core::KnownStore;
using lens::core::ReviewFrequency;

constexpr long long kMinute = 60LL * 1000;
constexpr long long kHour   = 60 * kMinute;
constexpr long long kDay    = 24 * kHour;

/// @return Epoch ms of a local "YYYY-MM-DD HH:MM", the form the history keeps its minutes in.
long long at(char const* minute)
{
    return QDateTime::fromString(QString::fromUtf8(minute), QStringLiteral("yyyy-MM-dd HH:mm")).toMSecsSinceEpoch();
}

/// @brief A ReviewDuty over a fresh settings file, with the pet's state set by the test.
struct ReviewDutyTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_review_duty_test.json";
        std::filesystem::remove(path);
        store   = std::make_unique<KnownStore>(KnownStore::load(path));
        storage = std::make_unique<StorageDuty>(*store);
        duty    = std::make_unique<ReviewDuty>(*storage, [this] { return enabled; }, [this] { return idle; });
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    /// Looked up at @p minute, with an explanation stored in the current language.
    void lookUpCached(std::string const& lemma, char const* minute)
    {
        storage->statsStore().recordPop(lemma, minute);
        lens::core::WordCache cached;
        cached.en = "a definition";
        store->cachePut(lemma, cached, lens::core::CacheContext{"en", false});
    }

    /// Steps the clock from @p from to @p to, one minute at a time, so every look the duty draws is taken.
    void runClock(long long from, long long to)
    {
        for (long long now = from; now <= to; now += kMinute)
            duty->tick(now);
    }

    std::filesystem::path        path;
    std::unique_ptr<KnownStore>  store;
    std::unique_ptr<StorageDuty> storage;
    std::unique_ptr<ReviewDuty>  duty;
    bool                         enabled = true;
    bool                         idle    = true;
};

} // namespace

TEST_F(ReviewDutyTest, AsksAboutADueCachedWordWhenThePetIsFree)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;

    runClock(now, now + 2 * kHour);

    EXPECT_EQ(duty->word().toStdString(), "apple");
}

TEST_F(ReviewDutyTest, NoQuestionWhileNothingIsDue)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + kHour;

    runClock(now, now + 2 * kHour);

    EXPECT_TRUE(duty->word().isEmpty());
}

TEST_F(ReviewDutyTest, NoQuestionWhileThePetIsBusyOrOff)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;

    idle = false;
    runClock(now, now + 2 * kHour);
    EXPECT_TRUE(duty->word().isEmpty());

    idle    = true;
    enabled = false;
    runClock(now, now + 2 * kHour);
    EXPECT_TRUE(duty->word().isEmpty());
}

TEST_F(ReviewDutyTest, AWordWithNoStoredExplanationIsSkipped)
{
    storage->statsStore().recordPop("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;

    runClock(now, now + 2 * kHour);

    EXPECT_TRUE(duty->word().isEmpty());
}

TEST_F(ReviewDutyTest, AnIgnoredWordIsNotAskedAgainWithinTwoHours)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;
    runClock(now, now + 2 * kHour);
    ASSERT_EQ(duty->word().toStdString(), "apple");

    duty->ignore(now + 2 * kHour);
    EXPECT_TRUE(duty->word().isEmpty());

    runClock(now + 2 * kHour, now + 4 * kHour - kMinute);
    EXPECT_TRUE(duty->word().isEmpty()) << "still cooling down";

    runClock(now + 4 * kHour, now + 4 * kHour + kHour);
    EXPECT_EQ(duty->word().toStdString(), "apple") << "the cooldown is over and the word is still due";
}

TEST_F(ReviewDutyTest, RememberingMovesTheWordUpAndPutsItAgainstTheClock)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;
    runClock(now, now + 2 * kHour);
    ASSERT_EQ(duty->word().toStdString(), "apple");

    auto const answered = now + 2 * kHour;
    EXPECT_EQ(duty->answer(true, answered).toStdString(), "apple");

    EXPECT_TRUE(duty->word().isEmpty());
    auto const state = storage->reviewStore().get("apple");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->level, 2);
    EXPECT_EQ(state->due, answered + 3 * kDay);
}

TEST_F(ReviewDutyTest, ForgettingFromTheFirstLevelComesBackInTenMinutes)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;
    runClock(now, now + 2 * kHour);

    auto const answered = now + 2 * kHour;
    duty->answer(false, answered);

    auto const state = storage->reviewStore().get("apple");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->level, 0);
    EXPECT_EQ(state->due, answered + 10 * kMinute);
}

TEST_F(ReviewDutyTest, AnswerWithNoQuestionUpChangesNothing)
{
    lookUpCached("apple", "2026-10-03 09:00");

    EXPECT_TRUE(duty->answer(true, at("2026-10-03 09:00")).isEmpty());
    EXPECT_FALSE(storage->reviewStore().get("apple").has_value());
}

TEST_F(ReviewDutyTest, SwitchingThePetOffTakesTheQuestionDownAndKeepsItsCooldown)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;
    runClock(now, now + 2 * kHour);
    ASSERT_EQ(duty->word().toStdString(), "apple");

    enabled = false;
    duty->tick(now + 2 * kHour);

    EXPECT_TRUE(duty->word().isEmpty());
    auto const state = storage->reviewStore().get("apple");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->level, 1) << "no verdict, so the level stays";
    EXPECT_EQ(state->asked, now + 2 * kHour);
}

TEST_F(ReviewDutyTest, FrequencyOffNeverAsksAndTheChoiceIsKept)
{
    lookUpCached("apple", "2026-10-03 09:00");
    auto const now = at("2026-10-03 09:00") + 2 * kDay;

    duty->setFrequency(ReviewFrequency::Off, now);
    runClock(now, now + 5 * kHour);

    EXPECT_TRUE(duty->word().isEmpty());
    EXPECT_EQ(duty->frequency(), ReviewFrequency::Off);
}
