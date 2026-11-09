/**
 * @file ocr_lifecycle_test.cpp
 * @brief The test and detect phases, and the tokens the settings page reads from them.
 */

#include <QCoreApplication>
#include <gtest/gtest.h>

#include <filesystem>

#include "app/duties/ocr_lifecycle.h"
#include "app/duties/storage_duty.h"
#include "capture/ocr_engine.h"
#include "core/document/known_store.h"

namespace {

using lens::app::OcrLifecycle;
using lens::app::StorageDuty;
using lens::app::statusToken;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;
using lens::core::KnownStore;

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "ocr_lifecycle_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

struct OcrLifecycleTest : ::testing::Test {
    void SetUp() override
    {
        std::filesystem::remove(path);
    }
    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    QtApplication         qt;
    std::filesystem::path path  = std::filesystem::temp_directory_path() / "lens_ocr_lifecycle_test.json";
    KnownStore            store = KnownStore::load(path);
    StorageDuty           storage{store};
    OcrLifecycle          lifecycle{storage, [] {}};
};

}

/// The refusal is the token the page explains; leaving the refused phase clears it.
TEST_F(OcrLifecycleTest, TheTestPhaseKeepsItsRefusalToken)
{
    EXPECT_EQ(lifecycle.testState(), QStringLiteral("idle"));
    lifecycle.testStarted();
    EXPECT_EQ(lifecycle.testState(), QStringLiteral("checking"));
    lifecycle.testRefused(QStringLiteral("budget-paused"));
    EXPECT_EQ(lifecycle.testState(), QStringLiteral("budget-paused"));
    lifecycle.testCancelled();
    EXPECT_EQ(lifecycle.testState(), QStringLiteral("idle"));
}

/// A finished test is either the ready pass or the refusal its status maps to.
TEST_F(OcrLifecycleTest, AFinishedTestReadsReadyOrTheRefusalToken)
{
    OcrResult ready;
    ready.status = OcrStatus::Ready;
    lifecycle.testFinished(ready);
    EXPECT_EQ(lifecycle.testState(), QStringLiteral("ready"));

    OcrResult refused;
    refused.status = OcrStatus::RuntimeMissing;
    lifecycle.testFinished(refused);
    EXPECT_EQ(lifecycle.testState(), statusToken(OcrStatus::RuntimeMissing));
}

/// A detection that ran but did not pass says `failed`; a refusal keeps its own token.
TEST_F(OcrLifecycleTest, TheDetectPhaseSeparatesFailedFromARefusal)
{
    EXPECT_EQ(lifecycle.detectState(), QStringLiteral("idle"));
    lifecycle.detectStarted();
    EXPECT_EQ(lifecycle.detectState(), QStringLiteral("checking"));

    lifecycle.detectDone(QStringLiteral("failed"), 42);
    EXPECT_EQ(lifecycle.detectState(), QStringLiteral("failed"));
    EXPECT_EQ(lifecycle.detectMs(), 42);

    lifecycle.detectDone(QStringLiteral("ready"), 7);
    EXPECT_EQ(lifecycle.detectState(), QStringLiteral("ready"));

    lifecycle.detectRefused(OcrStatus::RuntimeMissing);
    EXPECT_EQ(lifecycle.detectState(), statusToken(OcrStatus::RuntimeMissing));

    lifecycle.detectIdle();
    EXPECT_EQ(lifecycle.detectState(), QStringLiteral("idle"));
    EXPECT_EQ(lifecycle.detectMs(), 0);
}
