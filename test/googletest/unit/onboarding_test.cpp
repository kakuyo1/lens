/**
 * @file onboarding_test.cpp
 * @brief Offline tests for the first-run rule (core/domain/onboarding.h, docs/adr/0144, docs/adr/0148).
 */

#include <gtest/gtest.h>

#include "core/domain/onboarding.h"

using lens::core::onboarding::launchPlan;

TEST(Onboarding, FirstLaunchWithoutKeyRaisesTheWizardAndDefersTheRecord)
{
    auto const plan{launchPlan(false, false, false)};
    EXPECT_TRUE(plan.raiseWizard);
    EXPECT_FALSE(plan.markDone);
}

TEST(Onboarding, GuidedReaderIsNotTold)
{
    auto const plan{launchPlan(true, false, false)};
    EXPECT_FALSE(plan.raiseWizard);
    EXPECT_FALSE(plan.markDone);
}

TEST(Onboarding, ReaderWithAKeyIsNotToldButIsRecordedAsGuided)
{
    auto const plan{launchPlan(false, true, false)};
    EXPECT_FALSE(plan.raiseWizard);
    EXPECT_TRUE(plan.markDone);
}

TEST(Onboarding, ForcedLaunchRaisesTheWizardWhateverTheDocumentSays)
{
    for (auto const done : {false, true}) {
        for (auto const hasApiKey : {false, true}) {
            auto const plan{launchPlan(done, hasApiKey, true)};
            EXPECT_TRUE(plan.raiseWizard) << "done=" << done << " hasApiKey=" << hasApiKey;
            EXPECT_FALSE(plan.markDone) << "a forced launch must not record anything";
        }
    }
}
