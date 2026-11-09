/**
 * @file reading_profile_duty_test.cpp
 * @brief One paid request at a time: the story in flight keeps the interpretation out of the air.
 */

#include <QCoreApplication>
#include <QDate>
#include <QUrl>
#include <gtest/gtest.h>

#include <filesystem>

#include "app/duties/cost_duty.h"
#include "app/duties/reading_profile_duty.h"
#include "app/duties/storage_duty.h"
#include "core/document/known_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "llm_support.h"
#include "scripted_manager.h"

namespace {

using lens::app::CostDuty;
using lens::app::ReadingProfileDuty;
using lens::app::StorageDuty;
using lens::core::KnownStore;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[32] = "reading_profile_duty_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

const QDate testDate()
{
    return QDate::fromString(QStringLiteral("2026-11-09"), Qt::ISODate);
}

const lens::llm::Pricing& pricing()
{
    static const lens::llm::Pricing loaded = lens::llm::Pricing::load(
        std::filesystem::path(LENS_SOURCE_DIR) / "data" / "llm" / "pricing.json");
    return loaded;
}

/// The duty over a document of its own, with a network that never answers: a request that is sent
/// stays the one in flight, which is what the guard is read against.
struct ReadingProfileDutyTest : ::testing::Test {
    void SetUp() override
    {
        lens::test::requireLlmProtocolLoaded();
        std::filesystem::remove(path);
    }
    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    QtApplication         qt;
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lens_reading_profile_duty_test.json";
    ScriptedManager       manager{302, {}, /*answers=*/false};
    lens::llm::LlmClient  llm{lens::llm::Config{QUrl{QStringLiteral("https://api.example.com")},
                                               QStringLiteral("test-key"),
                                               QStringLiteral("test-model")},
                             &manager};
    KnownStore            store = KnownStore::load(path);
    StorageDuty           storage{store};
    CostDuty              cost{storage.statsStore(), llm, pricing(), [] { return testDate(); }};
    ReadingProfileDuty    duty{storage, llm, cost, [] { return testDate(); }};
};

}

/// The story is a paid request too. Starting an interpretation while one is being written would
/// put both in the air at once, so the interpretation waits -- the mirror of storyAvailable(),
/// which already refuses while an interpretation is in flight.
TEST_F(ReadingProfileDutyTest, AStoryInFlightKeepsTheInterpretationOutOfTheAir)
{
    // One lookup this week: enough for the profile to have something to interpret and the story
    // something to be written from.
    storage.statsStore().recordPop("bank", "2026-11-09 09:00", false, "zh");
    ASSERT_TRUE(duty.storyAvailable());

    duty.tellStory();
    ASSERT_TRUE(duty.storyWriting());

    duty.interpret();

    EXPECT_FALSE(duty.interpreting()) << "the story is the request in flight; the interpretation must wait";
    EXPECT_EQ(manager.sent.size(), 1u) << "only the story's POST left";
}
