/**
 * @file review_store_test.cpp
 * @brief ReviewStore, offline: the per-word state and the check frequency it keeps in the
 *        settings document, what survives a reload, and what removeWord clears.
 */

#include <filesystem>
#include <optional>
#include <string>

#include <QString>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "app/storage_duty.h"
#include "core/known_store.h"
#include "core/review_store.h"

namespace {

using lens::core::KnownStore;
using lens::core::ReviewFrequency;
using lens::core::ReviewState;
using lens::core::ReviewStore;

constexpr long long kNow = 1'800'000'000'000LL;

/// @brief A KnownStore bound to a fresh temp file, deleted either way.
struct ReviewStoreTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_review_store_test.json";
        std::filesystem::remove(path);
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
};

} // namespace

TEST_F(ReviewStoreTest, StateSurvivesASaveAndReload)
{
    {
        auto        store = KnownStore::load(path);
        ReviewStore review(store.document());
        review.put("apple", ReviewState{3, kNow, kNow - 1});
        review.setFrequency(ReviewFrequency::Every1To2Hours);
        store.save();
    }

    auto        reloaded = KnownStore::load(path);
    ReviewStore review(reloaded.document());

    const auto state = review.get("apple");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->level, 3);
    EXPECT_EQ(state->due, kNow);
    EXPECT_EQ(state->asked, kNow - 1);
    EXPECT_EQ(review.frequency(), ReviewFrequency::Every1To2Hours);
}

TEST(ReviewStore, MalformedEntriesReadAsAbsentAndAnUnknownWordIsAbsent)
{
    nlohmann::json document = {{"review", {{"words", {
                                                         {"outOfRange", {{"level", 9}, {"due", kNow}, {"asked", 0}}},
                                                         {"missingField", {{"level", 1}, {"due", kNow}}},
                                                         {"notAnObject", "x"},
                                                     }}}}};
    ReviewStore    review(document);

    EXPECT_EQ(review.get("outOfRange"), std::nullopt);
    EXPECT_EQ(review.get("missingField"), std::nullopt);
    EXPECT_EQ(review.get("notAnObject"), std::nullopt);
    EXPECT_EQ(review.get("unknown"), std::nullopt);
}

TEST(ReviewStore, UnknownKeysInsideTheSectionSurviveWrites)
{
    nlohmann::json document = {{"review", {{"future", true}, {"words", nlohmann::json::object()}}}};
    ReviewStore    review(document);

    review.put("apple", ReviewState{1, kNow, 0});
    review.removeLemma("apple");
    review.setFrequency(ReviewFrequency::Off);

    EXPECT_TRUE(document["review"]["future"].get<bool>());
}

TEST(ReviewStore, RemoveLemmaReportsWhetherAStateExisted)
{
    nlohmann::json document = nlohmann::json::object();
    ReviewStore    review(document);
    review.put("apple", ReviewState{1, kNow, 0});

    EXPECT_TRUE(review.removeLemma("apple"));
    EXPECT_EQ(review.get("apple"), std::nullopt);
    EXPECT_FALSE(review.removeLemma("apple"));
}

TEST(ReviewStore, FrequencyFallsBackToTheDefaultWhenUnsetOrOutOfRange)
{
    nlohmann::json document = {{"review", {{"frequency", 7}}}};
    ReviewStore    review(document);

    EXPECT_EQ(review.frequency(), lens::core::kDefaultReviewFrequency);

    review.setFrequency(ReviewFrequency::Off);
    EXPECT_EQ(review.frequency(), ReviewFrequency::Off);
}

TEST(ReviewStore, SettingTheFrequencyLeavesEveryCurveAlone)
{
    nlohmann::json document = nlohmann::json::object();
    ReviewStore    review(document);
    review.put("apple", ReviewState{4, kNow, 0});

    review.setFrequency(ReviewFrequency::Every15To30Min);

    const auto state = review.get("apple");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->level, 4);
    EXPECT_EQ(state->due, kNow);
}

TEST_F(ReviewStoreTest, RemoveWordClearsTheReviewStateWithTheRest)
{
    auto                   store = KnownStore::load(path);
    lens::app::StorageDuty storage{store};
    storage.reviewStore().put("apple", ReviewState{2, kNow, 0});

    EXPECT_TRUE(storage.removeWord(QStringLiteral("apple")));
    EXPECT_EQ(storage.reviewStore().get("apple"), std::nullopt);
}
