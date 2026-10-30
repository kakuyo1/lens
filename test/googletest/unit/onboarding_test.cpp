/**
 * @file onboarding_test.cpp
 * @brief Offline tests for the first-run rule (core/domain/onboarding.h, docs/adr/0144).
 */

#include <gtest/gtest.h>

#include "core/domain/onboarding.h"

using lens::core::onboarding::launchPlan;

TEST(Onboarding, FirstLaunchWithoutKeyRaisesTheNoticeAndRecordsIt)
{
    auto const plan{launchPlan(false, false, false)};
    EXPECT_TRUE(plan.raiseNotice);
    EXPECT_TRUE(plan.markDone);
}

TEST(Onboarding, AnnouncedReaderIsNotTold)
{
    auto const plan{launchPlan(true, false, false)};
    EXPECT_FALSE(plan.raiseNotice);
    EXPECT_FALSE(plan.markDone);
}

TEST(Onboarding, ReaderWithAKeyIsNotToldButIsRecordedAsAnnounced)
{
    auto const plan{launchPlan(false, true, false)};
    EXPECT_FALSE(plan.raiseNotice);
    EXPECT_TRUE(plan.markDone);
}

TEST(Onboarding, ForcedLaunchRaisesTheNoticeWhateverTheDocumentSays)
{
    for (auto const done : {false, true}) {
        for (auto const hasApiKey : {false, true}) {
            auto const plan{launchPlan(done, hasApiKey, true)};
            EXPECT_TRUE(plan.raiseNotice) << "done=" << done << " hasApiKey=" << hasApiKey;
            EXPECT_FALSE(plan.markDone) << "a forced launch must not record anything";
        }
    }
}
