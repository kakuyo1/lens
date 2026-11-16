/**
 * @file crash_notice_duty_test.cpp
 * @brief The crash marker's one trip through a launch, on a temporary log directory: read, prune,
 *        announce, remove -- and silence on the launch that follows no crash (docs/adr/0174, 0184).
 */

#include <filesystem>
#include <fstream>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <gtest/gtest.h>

#include "app/duties/cost_duty.h"
#include "app/duties/crash_notice_duty.h"
#include "app/duties/explanation_duty.h"
#include "app/duties/storage_duty.h"
#include "core/document/known_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "scripted_manager.h"
#include "support.h"
#include "util/crash.h"

namespace {

using lens::app::CostDuty;
using lens::app::CrashNoticeDuty;
using lens::app::ExplanationDuty;
using lens::app::StorageDuty;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[32] = "crash_notice_duty_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

/// The duty over a temporary log directory and a notice channel with no network: the marker is
/// the input, the notice map is the answer, and the two desktop actions are what a case never calls.
struct CrashNoticeDutyTest : ::testing::Test {
    [[nodiscard]] std::filesystem::path logDir() const
    {
        return std::filesystem::path{temporary.path().toStdWString()} / L"logs";
    }
    [[nodiscard]] std::filesystem::path dumpDir() const
    {
        return lens::util::crashDumpDir(logDir());
    }

    /// @brief Drop the marker the crash handler leaves, carrying @p text verbatim.
    void writeMarker(std::string const& text) const
    {
        std::filesystem::create_directories(dumpDir());
        std::ofstream marker(dumpDir() / lens::util::kCrashMarkerName, std::ios::binary);
        marker.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    /// @brief @p count dumps, each with the summary the crash path writes beside it.
    void writeDumps(int const count) const
    {
        std::filesystem::create_directories(dumpDir());
        for (int i = 0; i < count; ++i) {
            const std::string stem = "lens-1.0.0-20261116-0900" + std::to_string(10 + i);
            // Empty files: the retention rule counts dumps and their summaries, not their contents.
            std::ofstream dump(dumpDir() / (stem + ".dmp"), std::ios::binary);
            std::ofstream summary(dumpDir() / (stem + ".crash.txt"), std::ios::binary);
        }
    }

    [[nodiscard]] int filesInDumpDir() const
    {
        int files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dumpDir()))
            if (entry.is_regular_file())
                ++files;
        return files;
    }

    QtApplication          qt;
    QTemporaryDir          temporary;
    lens::core::KnownStore store   = lens::core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "settings.json");
    StorageDuty            storage = StorageDuty{store};
    ScriptedManager        manager{500};
    lens::llm::LlmClient   client{lens::llm::Config{QUrl{}, QString{}, QStringLiteral("test-model")}, &manager};
    lens::llm::Pricing     pricing = lens::llm::Pricing::load(lens::test::sourceDir() / "data" / "llm" / "pricing.json");
    CostDuty               cost{storage.statsStore(), client, pricing};
    ExplanationDuty        notices{storage, client, cost};
    CrashNoticeDuty        duty{notices};
};

} // namespace

/// The launch after a crash: the card the reader sees carries the two actions, the dumps are cut
/// back to the retention rule, and the marker is gone so the crash is announced once.
TEST_F(CrashNoticeDutyTest, AMarkerBecomesTheOneNoticeAndTheDumpsArePruned)
{
    writeDumps(7);
    writeMarker(lens::util::serializeCrashSummary(
        lens::util::CrashSummary{"1.0.0", "2026-11-16 09:00:00", "0xC0000005", "0x7ff61234", "lens.exe", {}}));

    duty.reportStartupCrash(logDir());

    const QVariantMap notice = notices.notice();
    EXPECT_EQ(notice.value("title").toString(), QStringLiteral("The previous run crashed"));
    EXPECT_EQ(notice.value("kind").toString(), QStringLiteral("crash"));
    EXPECT_EQ(notice.value("action").toString(), QStringLiteral("Open dump folder"));
    EXPECT_EQ(notice.value("secondaryAction").toString(), QStringLiteral("Report issue"));
    EXPECT_FALSE(notice.value("body").toString().isEmpty());
    EXPECT_FALSE(std::filesystem::exists(dumpDir() / lens::util::kCrashMarkerName)) << "the marker survived its own notice";
    EXPECT_EQ(filesInDumpDir(), static_cast<int>(lens::util::kCrashDumpsKept) * 2) << "each kept dump keeps its summary";
}

/// The marker is removed after the notice, so the launch after that has nothing to say.
TEST_F(CrashNoticeDutyTest, ASecondLaunchHasNothingToAnnounce)
{
    writeMarker(lens::util::serializeCrashSummary(lens::util::CrashSummary{}));
    duty.reportStartupCrash(logDir());
    ASSERT_EQ(notices.notice().value("kind").toString(), QStringLiteral("crash"));

    // Another notice stands where the crash one did; a live marker would replace it again.
    notices.showNotice(QStringLiteral("Something else"), QStringLiteral("body"), QStringLiteral("info"));
    duty.reportStartupCrash(logDir());

    EXPECT_EQ(notices.notice().value("kind").toString(), QStringLiteral("info")) << "the marker outlived its notice, so the crash would be announced twice";
}

/// A launch that followed no crash says nothing, and does not even create the dumps folder.
TEST_F(CrashNoticeDutyTest, ALaunchWithoutACrashSaysNothing)
{
    duty.reportStartupCrash(logDir());

    EXPECT_TRUE(notices.notice().isEmpty());
    EXPECT_FALSE(std::filesystem::exists(dumpDir())) << "a launch without a crash created the dumps folder";
}

/// A marker the crash path could not fill in still announces the crash, and still goes away.
TEST_F(CrashNoticeDutyTest, AMarkerThatCannotBeParsedStillAnnouncesAndStillGoes)
{
    writeMarker("this is not a crash summary\n");

    duty.reportStartupCrash(logDir());

    EXPECT_EQ(notices.notice().value("kind").toString(), QStringLiteral("crash"));
    EXPECT_FALSE(std::filesystem::exists(dumpDir() / lens::util::kCrashMarkerName));
}
