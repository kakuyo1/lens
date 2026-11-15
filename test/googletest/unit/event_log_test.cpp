/**
 * @file event_log_test.cpp
 * @brief The behavior-event registry, its payload checks, the JSON line, and the local month log:
 *        append, rollover, retention and sequence recovery, all offline.
 *
 * The interesting failures are the ones a report would see as missing history: a line the log
 * refuses without saying so, a month file that rolls over into the wrong month, and a sequence
 * that restarts after a crash and makes two events look like one.
 */

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/crypto/hash.h"
#include "core/telemetry/event.h"
#include "core/telemetry/event_log.h"

namespace {

using lens::core::telemetry::Event;
using lens::core::telemetry::EventLog;
using lens::core::telemetry::FieldKind;
using lens::core::telemetry::FieldSpec;
using lens::core::telemetry::eventSpecs;
using lens::core::telemetry::findEventSpec;
using lens::core::telemetry::parseLine;
using lens::core::telemetry::toLine;
using lens::core::telemetry::validatePayload;

nlohmann::json popPayload(std::string lemma = "apple")
{
    return {{"src", "scan"}, {"lemma", std::move(lemma)}, {"verdict", "new"}};
}

nlohmann::json sampleFor(const FieldSpec& field)
{
    switch (field.kind) {
        case FieldKind::Text:
            return "sample";
        case FieldKind::Integer:
            return 1;
        case FieldKind::Boolean:
            return true;
        case FieldKind::Choice:
            return std::string{field.choices.front()};
    }
    return nullptr;
}

std::string monthText(int year, int month) // NOLINT(bugprone-easily-swappable-parameters): a month is only meaningful with its year, and the two are never passed in the other order
{
    const std::string part = month < 10 ? "0" + std::to_string(month) : std::to_string(month);
    return std::to_string(year) + "-" + part;
}

struct EventLogTest : ::testing::Test {
protected:
    void SetUp() override
    {
        std::filesystem::remove_all(root);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }

    std::filesystem::path root = std::filesystem::temp_directory_path() / "lens_event_log_test";
};

TEST(TelemetryEvent, RegistryNamesAreUnique)
{
    std::set<std::string_view> names;
    for (const auto& spec : eventSpecs())
        EXPECT_TRUE(names.insert(spec.name).second) << spec.name;
}

TEST(TelemetryEvent, EveryChoiceFieldNamesItsValues)
{
    for (const auto& spec : eventSpecs())
        for (const FieldSpec& field : spec.fields)
            if (field.kind == FieldKind::Choice)
                EXPECT_FALSE(field.choices.empty()) << spec.name << "." << field.name;
}

TEST(TelemetryEvent, EveryRegisteredEventAcceptsItsRequiredFields)
{
    ASSERT_FALSE(eventSpecs().empty());
    for (const auto& spec : eventSpecs()) {
        nlohmann::json payload = nlohmann::json::object();
        for (const FieldSpec& field : spec.fields)
            if (field.required)
                payload[std::string(field.name)] = sampleFor(field);
        EXPECT_EQ(validatePayload(spec, payload), "") << spec.name;
    }
}

TEST(TelemetryEvent, EveryRegisteredEventAcceptsEveryFieldItLists)
{
    for (const auto& spec : eventSpecs()) {
        nlohmann::json payload = nlohmann::json::object();
        for (const FieldSpec& field : spec.fields)
            payload[std::string(field.name)] = sampleFor(field);
        EXPECT_EQ(validatePayload(spec, payload), "") << spec.name;
    }
}

TEST(TelemetryEvent, RejectsAnUnknownField)
{
    const auto* const spec = findEventSpec("lookup.pop");
    ASSERT_NE(spec, nullptr);

    nlohmann::json payload = popPayload();
    payload["extra"]       = 1;
    EXPECT_NE(validatePayload(*spec, payload).find("unknown field 'extra'"), std::string::npos);
}

TEST(TelemetryEvent, RejectsAMissingRequiredField)
{
    const auto* const spec = findEventSpec("lookup.pop");
    ASSERT_NE(spec, nullptr);

    const nlohmann::json payload = {{"src", "scan"}, {"lemma", "apple"}};
    EXPECT_NE(validatePayload(*spec, payload).find("missing field 'verdict'"), std::string::npos);
}

TEST(TelemetryEvent, RejectsTextOverItsLimit)
{
    const auto* const spec = findEventSpec("lookup.pop");
    ASSERT_NE(spec, nullptr);

    nlohmann::json payload = popPayload(std::string(65, 'a'));
    EXPECT_NE(validatePayload(*spec, payload).find("longer than 64 characters"), std::string::npos);
}

TEST(TelemetryEvent, RejectsAChoiceOutsideItsList)
{
    const auto* const spec = findEventSpec("lookup.pop");
    ASSERT_NE(spec, nullptr);

    nlohmann::json payload = popPayload();
    payload["verdict"]     = "mastered"; // a real verdict, but not one a pop is stamped with
    EXPECT_NE(validatePayload(*spec, payload).find("not one of its values"), std::string::npos);
}

TEST(TelemetryEvent, RejectsInvalidUtf8)
{
    const auto* const spec = findEventSpec("lookup.pop");
    ASSERT_NE(spec, nullptr);

    nlohmann::json payload = popPayload();
    payload["lemma"]       = std::string("\xFF", 1);
    EXPECT_NE(validatePayload(*spec, payload).find("not valid UTF-8"), std::string::npos);
}

TEST(TelemetryEvent, RoundTripsALine)
{
    Event original;
    original.tsMs   = 123456789;
    original.seq    = 7;
    original.device = "a1b2c3d4";
    original.name   = "lookup.pop";
    original.data   = popPayload("ubiquitous");

    const auto parsed = parseLine(toLine(original));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->tsMs, original.tsMs);
    EXPECT_EQ(parsed->seq, original.seq);
    EXPECT_EQ(parsed->device, original.device);
    EXPECT_EQ(parsed->name, original.name);
    EXPECT_EQ(parsed->data.value("lemma", ""), "ubiquitous");
}

TEST(TelemetryEvent, ReadsAnUnknownTopLevelFieldWithoutFailing)
{
    Event original;
    original.tsMs   = 1;
    original.seq    = 1;
    original.device = "a1b2c3d4";
    original.name   = "app.start";
    original.data   = {{"version", "1.0.0"}, {"installed", true}};

    nlohmann::json document = nlohmann::json::parse(toLine(original), nullptr, false);
    document["later"]       = "ignored";
    EXPECT_TRUE(parseLine(document.dump()).has_value());
}

TEST(TelemetryEvent, RejectsAnotherFormatVersion)
{
    Event original;
    original.tsMs   = 1;
    original.seq    = 1;
    original.device = "a1b2c3d4";
    original.name   = "app.exit";
    original.data   = {{"uptimeMs", 10}};

    nlohmann::json document = nlohmann::json::parse(toLine(original), nullptr, false);
    document["v"]           = 2;
    EXPECT_FALSE(parseLine(document.dump()).has_value());
}

TEST(TelemetryEvent, RejectsLinesThatAreNotEvents)
{
    EXPECT_FALSE(parseLine("not json").has_value());
    EXPECT_FALSE(parseLine("[]").has_value());

    Event original;
    original.tsMs   = 1;
    original.seq    = 1;
    original.device = "a1b2c3d4";
    original.name   = "app.exit";
    original.data   = {{"uptimeMs", 10}};

    nlohmann::json missing = nlohmann::json::parse(toLine(original), nullptr, false);
    missing.erase("data");
    EXPECT_FALSE(parseLine(missing.dump()).has_value());
}

TEST_F(EventLogTest, WritesOneLinePerEvent)
{
    EventLog log(root, "device-a");
    ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-11", popPayload()));
    ASSERT_TRUE(log.append("lookup.pop", 2000, "2026-11", popPayload("pear")));
    log.flush();

    const auto events = log.readMonth("2026-11");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].tsMs, 1000);
    EXPECT_EQ(events[0].seq, 1u);
    EXPECT_EQ(events[1].seq, 2u);
    EXPECT_EQ(events[0].device, lens::core::crypto::sha1Hex("device-a").substr(0, 8));
    EXPECT_EQ(events[0].name, "lookup.pop");
    EXPECT_EQ(events[0].data.value("lemma", ""), "apple");
    EXPECT_TRUE(std::filesystem::exists(root / "events-2026-11.jsonl"));
    EXPECT_TRUE(std::filesystem::exists(root / "state.json"));
}

TEST_F(EventLogTest, RolloverWritesEachMonthsOwnFile)
{
    EventLog log(root, "device-a");
    ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-10", popPayload()));
    ASSERT_TRUE(log.append("lookup.pop", 2000, "2026-11", popPayload()));
    log.flush();

    EXPECT_EQ(log.months(), std::vector<std::string>({"2026-10", "2026-11"}));
    EXPECT_EQ(log.readMonth("2026-10").size(), 1u);
    EXPECT_EQ(log.readMonth("2026-11").size(), 1u);
}

TEST_F(EventLogTest, RejectsAnUnregisteredEventAndWritesNothing)
{
    EventLog log(root, "device-a");
    EXPECT_FALSE(log.append("lookup.unknown", 1000, "2026-11", nlohmann::json::object()));
    EXPECT_TRUE(log.months().empty());
}

TEST_F(EventLogTest, RejectsAMonthThatIsNotYearMonth)
{
    EventLog log(root, "device-a");
    EXPECT_FALSE(log.append("lookup.pop", 1000, "2026-13", popPayload()));
    EXPECT_FALSE(log.append("lookup.pop", 1000, "../2026-11", popPayload()));
    EXPECT_TRUE(log.months().empty());
}

TEST_F(EventLogTest, RejectsAPayloadOutsideTheRegistry)
{
    EventLog       log(root, "device-a");
    nlohmann::json payload = popPayload();
    payload["extra"]       = 1;
    EXPECT_FALSE(log.append("lookup.pop", 1000, "2026-11", payload));
    EXPECT_TRUE(log.months().empty());
}

TEST_F(EventLogTest, PrunesMonthsOlderThanTheRetentionWindow)
{
    EventLog log(root, "device-a");
    for (int index = 0; index < 13; ++index) {
        const int year  = 2025 + (10 + index) / 12;
        const int month = (10 + index) % 12 + 1;
        ASSERT_TRUE(log.append("lookup.pop", 1000 + index, monthText(year, month), popPayload()));
    }
    log.flush();

    const auto removed = log.prune("2026-11");
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0], "2025-11");
    EXPECT_EQ(log.months().size(), 12u);
    EXPECT_EQ(log.months().front(), "2025-12");
}

TEST_F(EventLogTest, RebuildsTheSequenceWhenStateIsMissing)
{
    {
        EventLog log(root, "device-a");
        ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-11", popPayload()));
        ASSERT_TRUE(log.append("lookup.pop", 2000, "2026-11", popPayload()));
        log.flush();
    }
    std::filesystem::remove(root / "state.json");

    EventLog reopened(root, "device-a");
    ASSERT_TRUE(reopened.append("lookup.pop", 3000, "2026-11", popPayload()));
    reopened.flush();

    const auto events = reopened.readMonth("2026-11");
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events.back().seq, 3u);
}

TEST_F(EventLogTest, TakesTheHigherOfTheStateAndTheFileSequence)
{
    {
        EventLog log(root, "device-a");
        ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-11", popPayload()));
        ASSERT_TRUE(log.append("lookup.pop", 2000, "2026-11", popPayload()));
        ASSERT_TRUE(log.append("lookup.pop", 3000, "2026-11", popPayload()));
        log.flush();
    }
    {
        std::ofstream state(root / "state.json", std::ios::binary | std::ios::trunc);
        state << R"({"v":1,"seq":1})";
    }

    EventLog reopened(root, "device-a");
    ASSERT_TRUE(reopened.append("lookup.pop", 4000, "2026-11", popPayload()));
    reopened.flush();

    const auto events = reopened.readMonth("2026-11");
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events.back().seq, 4u);
}

TEST_F(EventLogTest, RecoversFromACorruptStateDocument)
{
    {
        EventLog log(root, "device-a");
        ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-11", popPayload()));
        ASSERT_TRUE(log.append("lookup.pop", 2000, "2026-11", popPayload()));
        log.flush();
    }
    {
        std::ofstream state(root / "state.json", std::ios::binary | std::ios::trunc);
        state << "not json\n";
    }

    EventLog reopened(root, "device-a");
    ASSERT_TRUE(reopened.append("lookup.pop", 3000, "2026-11", popPayload()));
    reopened.flush();

    const auto events = reopened.readMonth("2026-11");
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events.back().seq, 3u);
}

TEST_F(EventLogTest, RecoversTheSequencePastAPartialLastLine)
{
    {
        EventLog log(root, "device-a");
        ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-11", popPayload()));
        ASSERT_TRUE(log.append("lookup.pop", 2000, "2026-11", popPayload()));
        log.flush();
    }
    {
        std::ofstream out(root / "events-2026-11.jsonl", std::ios::binary | std::ios::app);
        out << R"({"v":1,"ts":)"; // the line a crash did not finish
    }
    std::filesystem::remove(root / "state.json");

    EventLog reopened(root, "device-a");
    ASSERT_TRUE(reopened.append("lookup.pop", 3000, "2026-11", popPayload()));
    reopened.flush();

    const auto events = reopened.readMonth("2026-11");
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events.back().seq, 3u);
}

TEST_F(EventLogTest, SkipsMalformedLinesWhenReading)
{
    {
        EventLog log(root, "device-a");
        ASSERT_TRUE(log.append("lookup.pop", 1000, "2026-11", popPayload()));
        log.flush();
    }
    {
        std::ofstream out(root / "events-2026-11.jsonl", std::ios::binary | std::ios::app);
        out << "not json\n";
    }

    EventLog   log(root, "device-a");
    const auto events = log.readMonth("2026-11");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].seq, 1u);
}

TEST_F(EventLogTest, PreservesOtherStateKeys)
{
    {
        EventLog log(root, "device-a");
        log.state()["uploaded"] = {{"2026-10", "lens-events-a1b2c3d4-2026-10-r1-deadbeef.json.enc"}};
        log.flush();
    }

    EventLog reopened(root, "device-a");
    EXPECT_EQ(reopened.state().at("uploaded").at("2026-10").get<std::string>(),
              "lens-events-a1b2c3d4-2026-10-r1-deadbeef.json.enc");
}

}
