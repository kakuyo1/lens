/**
 * @file telemetry_report_test.cpp
 * @brief The behavior report: local calendar grouping, session splitting and capping, the words,
 *        features and applications rankings, and the handling of unknown or duplicate events.
 *
 * Every event is synthetic and every local zone is pinned, so the expected days, weeks and hours
 * do not depend on the machine running the test.
 */

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/telemetry/event_log.h"
#include "core/telemetry/report.h"

namespace {

using lens::core::telemetry::Event;
using lens::core::telemetry::EventLog;
using lens::core::telemetry::Localize;
using lens::core::telemetry::LocalMoment;
using lens::core::telemetry::NamedCount;
using lens::core::telemetry::PeriodTotal;
using lens::core::telemetry::Report;
using lens::core::telemetry::ReportOptions;
using lens::core::telemetry::WordCount;
using lens::core::telemetry::buildReport;
using lens::core::telemetry::buildReportFromLog;
using lens::core::telemetry::reportJson;

long long epochMs(int const year, int const month, int const day, int const hour, int const minute = 0) // NOLINT(bugprone-easily-swappable-parameters): a calendar instant is positional, and a test never passes its fields in another order
{
    auto const date = std::chrono::year_month_day{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::sys_days{date}.time_since_epoch()).count() +
           (hour * 60LL + minute) * 60'000LL;
}

LocalMoment localMoment(long long const tsMs, int const offsetMinutes) // NOLINT(bugprone-easily-swappable-parameters): the instant and its offset are a fixed positional pair; the same-typed group cannot be reordered without changing the call's meaning
{
    auto const shifted = std::chrono::milliseconds{tsMs + offsetMinutes * 60LL * 1000LL};
    auto const day     = std::chrono::sys_days{std::chrono::floor<std::chrono::days>(shifted)};
    auto const date    = std::chrono::year_month_day{day};
    auto const hour    = std::chrono::duration_cast<std::chrono::hours>(shifted - day.time_since_epoch()).count();

    char text[11] = {};
    std::snprintf(text,
                  sizeof text,
                  "%04d-%02d-%02d",
                  static_cast<int>(date.year()),
                  static_cast<int>(static_cast<unsigned>(date.month())),
                  static_cast<int>(static_cast<unsigned>(date.day())));
    return LocalMoment{text, static_cast<int>(hour)};
}

Localize zone(int const offsetMinutes)
{
    return [offsetMinutes](long long const tsMs) { return localMoment(tsMs, offsetMinutes); };
}

Event event(long long const tsMs, std::string name, std::string device, unsigned long long const seq, nlohmann::json data = {}) // NOLINT(bugprone-easily-swappable-parameters): event takes its 2 string arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    return Event{.tsMs = tsMs, .seq = seq, .device = std::move(device), .name = std::move(name), .data = std::move(data)};
}

nlohmann::json pop(std::string lemma, std::string app = {}) // NOLINT(bugprone-easily-swappable-parameters): pop takes its 2 string arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    nlohmann::json data = {{"src", "scan"}, {"lemma", std::move(lemma)}, {"verdict", "new"}};
    if (not app.empty())
        data["app"] = std::move(app);
    return data;
}

long long countOf(std::vector<NamedCount> const& rows, std::string_view const name)
{
    for (NamedCount const& row : rows)
        if (row.name == name)
            return row.count;
    return 0;
}

struct TelemetryReportTest : ::testing::Test {
protected:
    void SetUp() override
    {
        std::filesystem::remove_all(root);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }

    std::filesystem::path root = std::filesystem::temp_directory_path() / "lens_telemetry_report_test";
};

TEST(TelemetryReport, CountsDaysWeeksMonthsHoursAndFeatures)
{
    std::vector<Event> events{
        event(epochMs(2026, 11, 17, 9, 59), "app.start", "device-a", 1, {{"version", "1.0.0"}, {"installed", true}}),
        event(epochMs(2026, 11, 17, 10, 0), "lookup.pop", "device-a", 2, pop("apple", "chrome.exe")),
        event(epochMs(2026, 11, 17, 10, 1), "lookup.peek", "device-a", 3, {{"lemma", "apple"}, {"app", "chrome.exe"}}),
        event(epochMs(2026, 11, 17, 10, 2), "verdict.set", "device-a", 4, {{"lemma", "apple"}, {"to", "known"}, {"via", "bubble"}}),
        event(epochMs(2026, 11, 18, 9, 0), "review.shown", "device-a", 5, {{"lemma", "pear"}, {"level", 2}}),
        event(epochMs(2026, 11, 18, 9, 1), "review.answered", "device-a", 6, {{"lemma", "pear"}, {"level", 2}, {"result", "known"}}),
        event(epochMs(2026, 11, 18, 9, 2), "app.exit", "device-a", 7, {{"uptimeMs", 1000}}),
    };

    Report const report = buildReport(std::move(events), zone(0));

    EXPECT_EQ(report.firstTsMs, epochMs(2026, 11, 17, 9, 59));
    EXPECT_EQ(report.lastTsMs, epochMs(2026, 11, 18, 9, 2));
    EXPECT_EQ(report.events, 7);
    EXPECT_EQ(report.ignored, 0);
    EXPECT_EQ(report.sessions, 2);
    EXPECT_EQ(report.activeMs, 120000 + 60000);

    EXPECT_EQ(report.hours[9], 2);
    EXPECT_EQ(report.hours[10], 3);
    EXPECT_EQ(report.hours[11], 0);

    ASSERT_EQ(report.days.size(), 2u);
    EXPECT_EQ(report.days[0].key, "2026-11-17");
    EXPECT_EQ(report.days[0].events, 4);
    EXPECT_EQ(report.days[0].sessions, 1);
    EXPECT_EQ(report.days[0].activeMs, 120000);
    EXPECT_EQ(report.days[1].key, "2026-11-18");
    EXPECT_EQ(report.days[1].events, 3);
    EXPECT_EQ(report.days[1].sessions, 1);
    EXPECT_EQ(report.days[1].activeMs, 60000);

    ASSERT_EQ(report.weeks.size(), 1u);
    EXPECT_EQ(report.weeks[0].key, "2026-11-16");
    EXPECT_EQ(report.weeks[0].events, 7);
    EXPECT_EQ(report.weeks[0].sessions, 2);
    EXPECT_EQ(report.weeks[0].activeMs, 180000);

    ASSERT_EQ(report.months.size(), 1u);
    EXPECT_EQ(report.months[0].key, "2026-11");
    EXPECT_EQ(report.months[0].events, 7);
    EXPECT_EQ(report.months[0].sessions, 2);

    ASSERT_EQ(report.topWords.size(), 1u);
    EXPECT_EQ(report.topWords[0].lemma, "apple");
    EXPECT_EQ(report.topWords[0].lookups, 1);

    ASSERT_EQ(report.features.size(), 5u);
    EXPECT_EQ(report.features[0].name, "lookup.peek");
    EXPECT_EQ(report.features[1].name, "lookup.pop");
    EXPECT_EQ(report.features[2].name, "review.answered");
    EXPECT_EQ(report.features[3].name, "review.shown");
    EXPECT_EQ(report.features[4].name, "verdict.set");
    for (NamedCount const& feature : report.features)
        EXPECT_EQ(feature.count, 1);

    ASSERT_EQ(report.apps.size(), 1u);
    EXPECT_EQ(report.apps[0].name, "chrome.exe");
    EXPECT_EQ(report.apps[0].count, 2);
}

TEST(TelemetryReport, SplitsAtTheIdleGapAndCapsTheDuration)
{
    std::vector<Event> const events{
        event(epochMs(2026, 11, 17, 10, 0), "lookup.pop", "device-a", 1, pop("apple")),
        event(epochMs(2026, 11, 17, 10, 5), "lookup.pop", "device-a", 2, pop("pear")),
        event(epochMs(2026, 11, 17, 10, 10), "lookup.pop", "device-a", 3, pop("plum")),
        event(epochMs(2026, 11, 17, 10, 16), "lookup.pop", "device-a", 4, pop("kiwi")),
    };

    Report const split = buildReport(events, zone(0));
    EXPECT_EQ(split.sessions, 2);
    EXPECT_EQ(split.activeMs, 10 * 60 * 1000);
    ASSERT_EQ(split.days.size(), 1u);
    EXPECT_EQ(split.days[0].sessions, 2);
    EXPECT_EQ(split.days[0].activeMs, 10 * 60 * 1000);

    ReportOptions const cappedOptions{.sessionIdleMs = 30LL * 60 * 1000, .sessionMaxMs = 10LL * 60 * 1000};
    Report const        capped = buildReport({events[0], events[3]}, zone(0), cappedOptions);
    EXPECT_EQ(capped.sessions, 1);
    EXPECT_EQ(capped.activeMs, 10 * 60 * 1000);
}

TEST(TelemetryReport, GroupsByTheLocalCalendarAcrossMonths)
{
    std::vector<Event> const events{
        event(epochMs(2026, 10, 31, 23, 30), "lookup.pop", "device-a", 1, pop("apple")),
        event(epochMs(2026, 11, 1, 0, 30), "lookup.pop", "device-a", 2, pop("pear")),
    };

    Report const utc = buildReport(events, zone(0));
    ASSERT_EQ(utc.months.size(), 2u);
    EXPECT_EQ(utc.months[0].key, "2026-10");
    EXPECT_EQ(utc.months[0].events, 1);
    EXPECT_EQ(utc.months[1].key, "2026-11");
    EXPECT_EQ(utc.months[1].events, 1);
    ASSERT_EQ(utc.days.size(), 2u);
    EXPECT_EQ(utc.days[0].key, "2026-10-31");
    EXPECT_EQ(utc.days[1].key, "2026-11-01");
    EXPECT_EQ(utc.sessions, 2);

    Report const plusTwo = buildReport(events, zone(120));
    ASSERT_EQ(plusTwo.months.size(), 1u);
    EXPECT_EQ(plusTwo.months[0].key, "2026-11");
    EXPECT_EQ(plusTwo.months[0].events, 2);
    ASSERT_EQ(plusTwo.days.size(), 1u);
    EXPECT_EQ(plusTwo.days[0].key, "2026-11-01");
    EXPECT_EQ(plusTwo.days[0].events, 2);
    EXPECT_EQ(plusTwo.hours[1], 1);
    EXPECT_EQ(plusTwo.hours[2], 1);

    Report const minusFive = buildReport(events, zone(-300));
    ASSERT_EQ(minusFive.months.size(), 1u);
    EXPECT_EQ(minusFive.months[0].key, "2026-10");
    EXPECT_EQ(minusFive.months[0].events, 2);
    ASSERT_EQ(minusFive.days.size(), 1u);
    EXPECT_EQ(minusFive.days[0].key, "2026-10-31");
    EXPECT_EQ(minusFive.days[0].events, 2);
    EXPECT_EQ(minusFive.hours[18], 1);
    EXPECT_EQ(minusFive.hours[19], 1);
}

TEST(TelemetryReport, IgnoresUnknownNamesAndDeduplicatesSequences)
{
    long long const    start = epochMs(2026, 11, 17, 10, 0);
    std::vector<Event> events{
        event(start, "lookup.pop", "device-a", 1, pop("apple")),
        event(start + 60000, "lookup.pop", "device-a", 1, pop("apple")),
        event(start, "lookup.pop", "device-b", 1, pop("pear")),
        event(start, "future.event", "device-c", 1, nlohmann::json::object()),
    };

    Report const report = buildReport(std::move(events), zone(0));

    EXPECT_EQ(report.events, 2);
    EXPECT_EQ(report.ignored, 1);
    EXPECT_EQ(report.sessions, 1);
    EXPECT_EQ(report.activeMs, 0);
    EXPECT_EQ(countOf(report.features, "lookup.pop"), 2);
    ASSERT_EQ(report.topWords.size(), 2u);
    EXPECT_EQ(report.topWords[0].lemma, "apple");
    EXPECT_EQ(report.topWords[0].lookups, 1);
    EXPECT_EQ(report.topWords[1].lemma, "pear");
}

TEST(TelemetryReport, ProducesTheSameJsonWhateverTheInputOrder)
{
    std::vector<Event> const events{
        event(epochMs(2026, 11, 17, 10, 0), "lookup.pop", "device-a", 1, pop("apple", "chrome.exe")),
        event(epochMs(2026, 11, 17, 10, 0), "lookup.peek", "device-b", 1, {{"lemma", "pear"}}),
        event(epochMs(2026, 11, 17, 10, 4), "review.shown", "device-a", 2, {{"lemma", "plum"}, {"level", 1}}),
        event(epochMs(2026, 11, 17, 10, 5), "future.event", "device-c", 1, nlohmann::json::object()),
    };

    std::vector<Event> reversed(events.rbegin(), events.rend());
    auto const         forwardJson  = reportJson(buildReport(events, zone(60))).dump();
    auto const         backwardJson = reportJson(buildReport(std::move(reversed), zone(60))).dump();
    EXPECT_EQ(forwardJson, backwardJson);
}

TEST(TelemetryReport, NamesTheTopWordsMostFirstAndCapsTheList)
{
    std::vector<Event> events;
    unsigned long long seq = 1;
    auto const         add = [&events, &seq](char const* const lemma, int const count) {
        for (int index = 0; index < count; ++index) {
            events.push_back(event(epochMs(2026, 11, 17, 10, 0) + static_cast<long long>(seq) * 10000,
                                   "lookup.pop",
                                   "device-a",
                                   seq,
                                   pop(lemma)));
            ++seq;
        }
    };
    add("apple", 3);
    add("pear", 2);
    add("plum", 2);
    add("kiwi", 1);

    ReportOptions const options{.topWords = 2};
    Report const        report = buildReport(std::move(events), zone(0), options);

    EXPECT_EQ(countOf(report.features, "lookup.pop"), 8);
    ASSERT_EQ(report.topWords.size(), 2u);
    EXPECT_EQ(report.topWords[0].lemma, "apple");
    EXPECT_EQ(report.topWords[0].lookups, 3);
    EXPECT_EQ(report.topWords[1].lemma, "pear");
    EXPECT_EQ(report.topWords[1].lookups, 2);
}

TEST(TelemetryReport, KeepsTheJsonShape)
{
    nlohmann::json const json = reportJson(buildReport(std::vector<Event>{}, Localize{}));

    for (char const* const key : {"firstTs", "lastTs", "events", "ignored", "sessions", "activeMs", "hours", "days", "weeks", "months", "topWords", "features", "apps"})
        EXPECT_TRUE(json.contains(key)) << key;

    EXPECT_EQ(json.at("firstTs").get<long long>(), 0);
    EXPECT_EQ(json.at("lastTs").get<long long>(), 0);
    ASSERT_EQ(json.at("hours").size(), 24u);
    for (auto const& hour : json.at("hours"))
        EXPECT_EQ(hour.get<long long>(), 0);
    EXPECT_TRUE(json.at("days").empty());
    EXPECT_TRUE(json.at("weeks").empty());
    EXPECT_TRUE(json.at("months").empty());
    EXPECT_TRUE(json.at("topWords").empty());
    EXPECT_TRUE(json.at("features").empty());
    EXPECT_TRUE(json.at("apps").empty());
}

TEST_F(TelemetryReportTest, ReadsPastAMalformedLineAndAnUnknownEvent)
{
    long long const start = epochMs(2026, 11, 17, 10, 0);
    {
        EventLog writer(root, "device-a");
        ASSERT_TRUE(writer.append("lookup.pop", start, "2026-11", pop("apple", "chrome.exe")));
        writer.flush();
    }

    {
        std::ofstream out(root / "events-2026-11.jsonl", std::ios::app | std::ios::binary);
        ASSERT_TRUE(out);
        out << "not a line\n";
        out << nlohmann::json{{"v", 1}, {"ts", start}, {"dev", "device-x"}, {"seq", 99}, {"ev", "future.event"}, {"data", nlohmann::json::object()}}.dump() << '\n';
    }

    EventLog const reader(root, "device-a");
    Report const   report = buildReportFromLog(reader, zone(0));

    EXPECT_EQ(report.events, 1);
    EXPECT_EQ(report.ignored, 1);
    ASSERT_EQ(report.topWords.size(), 1u);
    EXPECT_EQ(report.topWords[0].lemma, "apple");
    EXPECT_EQ(report.topWords[0].lookups, 1);
}

}
