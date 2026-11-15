/**
 * @file telemetry_duty_test.cpp
 * @brief The behavior sink: the foreground app name it may add, the burst flush, the summary and
 *        the batch that is written when the duty goes away.
 */

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "app/duties/telemetry_duty.h"
#include "core/telemetry/event_log.h"

namespace {

using lens::app::TelemetryDuty;
using lens::core::telemetry::EventLog;
using lens::core::telemetry::LocalMoment;

nlohmann::json popPayload(std::string lemma = "apple")
{
    return {{"src", "scan"}, {"lemma", std::move(lemma)}, {"verdict", "new"}};
}

std::unique_ptr<TelemetryDuty> makeDuty(std::filesystem::path const& root,
                                        bool const                   recordAppName,
                                        std::string const&           app      = "chrome.exe",
                                        TelemetryDuty::Localize      localize = {})
{
    // The app name is a std::string copy on a test-only callback; the diagnostic treats the
    // allocation as an exception that should not escape a function nothing marks noexcept.
    return std::make_unique<TelemetryDuty>(root, "device-a", [recordAppName] { return recordAppName; }, [app] { return app; }, [] { return TelemetryDuty::Now{1750000000000LL, "2026-11"}; }, std::move(localize)); // NOLINT(bugprone-exception-escape)
}

struct TelemetryDutyTest : ::testing::Test {
protected:
    void SetUp() override
    {
        std::filesystem::remove_all(root);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }

    std::filesystem::path root = std::filesystem::temp_directory_path() / "lens_telemetry_duty_test";
};

TEST_F(TelemetryDutyTest, AddsTheForegroundAppWhenTheReaderAllowsIt)
{
    auto duty = makeDuty(root, true);
    duty->record("lookup.pop", popPayload());
    duty->flush();

    const auto events = duty->log().readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data.value("app", ""), "chrome.exe");
}

TEST_F(TelemetryDutyTest, LeavesTheAppOutWhenTheReaderTurnsItOff)
{
    auto duty = makeDuty(root, false);
    duty->record("lookup.pop", popPayload());
    duty->flush();

    const auto events = duty->log().readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].data.contains("app"));
}

TEST_F(TelemetryDutyTest, KeepsAnAppTheCallerSupplied)
{
    auto duty = makeDuty(root, true, "chrome.exe");
    duty->record("scan.session", {{"words", 3}, {"durationMs", 10}, {"app", "notepad.exe"}});
    duty->flush();

    const auto events = duty->log().readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data.value("app", ""), "notepad.exe");
}

TEST_F(TelemetryDutyTest, DropsACallerSuppliedAppWhenTheReaderTurnsItOff)
{
    auto duty = makeDuty(root, false, "chrome.exe");
    duty->record("scan.session", {{"words", 3}, {"durationMs", 10}, {"app", "notepad.exe"}});
    duty->flush();

    const auto events = duty->log().readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].data.contains("app"));
}

TEST_F(TelemetryDutyTest, DoesNotAddAnAppToAnEventThatDoesNotListOne)
{
    auto duty = makeDuty(root, true);
    duty->record("app.exit", {{"uptimeMs", 10}});
    duty->flush();

    const auto events = duty->log().readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].data.contains("app"));
}

TEST_F(TelemetryDutyTest, RefusesAnUnregisteredEvent)
{
    auto duty = makeDuty(root, true);
    duty->record("lookup.unknown", {{"lemma", "apple"}});
    duty->flush();

    EXPECT_TRUE(duty->log().months().empty());
}

TEST_F(TelemetryDutyTest, WritesTheQueueWhenItReachesItsBurst)
{
    auto duty = makeDuty(root, true);
    for (std::size_t index = 0; index < TelemetryDuty::kFlushCount; ++index)
        duty->record("lookup.pop", popPayload());

    // The burst flush is a direct connection on this thread, so the file is already complete.
    const auto events = duty->log().readMonth("2026-11");
    EXPECT_EQ(events.size(), TelemetryDuty::kFlushCount);
}

TEST_F(TelemetryDutyTest, WritesTheQueueWhenTheDutyGoesAway)
{
    {
        auto duty = makeDuty(root, true);
        duty->record("app.start", {{"version", "1.0.0"}, {"installed", true}});
    }

    EventLog   reader(root, "device-a");
    const auto events = reader.readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "app.start");
}

TEST_F(TelemetryDutyTest, SummarizesTheLogByEventName)
{
    auto duty = makeDuty(root, true);
    duty->record("lookup.pop", popPayload());
    duty->record("lookup.pop", popPayload("pear"));
    duty->record("app.exit", {{"uptimeMs", 20}});
    duty->flush();

    const nlohmann::json summary = duty->summary();
    EXPECT_EQ(summary.value("total", 0), 3);
    EXPECT_EQ(summary.at("events").at("lookup.pop").get<int>(), 2);
    EXPECT_EQ(summary.at("events").at("app.exit").get<int>(), 1);
    EXPECT_EQ(summary.at("months").size(), 1u);
}

TEST_F(TelemetryDutyTest, ReportsTheLogWithTheDutysLocalCalendar)
{
    auto duty = makeDuty(root, true, "chrome.exe", [](long long) { return LocalMoment{"2026-11-17", 10}; });
    duty->record("lookup.pop", popPayload("apple"));
    duty->flush();

    const nlohmann::json report = duty->report();
    EXPECT_TRUE(report.value("ok", false));
    EXPECT_EQ(report.value("events", 0), 1);
    EXPECT_EQ(report.value("sessions", 0), 1);
    ASSERT_EQ(report.at("days").size(), 1u);
    EXPECT_EQ(report.at("days").at(0).at("date"), "2026-11-17");
    ASSERT_EQ(report.at("topWords").size(), 1u);
    EXPECT_EQ(report.at("topWords").at(0).at("lemma"), "apple");
}

}
