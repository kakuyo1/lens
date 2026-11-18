/**
 * @file telemetry_career_test.cpp
 * @brief The career counters: one pass per event family, the sums, the zero table, unknown names
 *        and payload fields whose type the writer would have refused.
 *
 * Every event is synthetic, so the expected counters do not depend on the machine running the test.
 */

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/telemetry/career.h"
#include "core/telemetry/event.h"

namespace {

using lens::core::telemetry::Career;
using lens::core::telemetry::Event;
using lens::core::telemetry::buildCareer;
using lens::core::telemetry::careerJson;
using lens::core::telemetry::emptyCareer;

Event event(std::string name, long long tsMs, nlohmann::json data)
{
    Event made;
    made.tsMs   = tsMs;
    made.seq    = 1;
    made.device = "test";
    made.name   = std::move(name);
    made.data   = std::move(data);
    return made;
}

TEST(CareerTest, EmptyCareerListsEveryCounterAtZero)
{
    const Career career = buildCareer({});
    EXPECT_EQ(career.at("events"), 0);
    EXPECT_EQ(career.at("firstTs"), 0);
    EXPECT_EQ(career.at("ocr.captures"), 0);
    EXPECT_EQ(career.at("llm.example"), 0);
    EXPECT_EQ(career.size(), emptyCareer().size());
}

TEST(CareerTest, CountsEachFamilyAndSumsDurationsAndTokens)
{
    const std::vector<Event> events{
        event("app.start", 1000, {{"version", "1.3.2"}, {"installed", true}}),
        event("app.exit", 5000, {{"uptimeMs", 4000}}),
        event("lookup.pop", 2000, {{"src", "selection"}, {"lemma", "spring"}, {"verdict", "new"}}),
        event("lookup.pop", 2100, {{"src", "hover"}, {"lemma", "spring"}, {"verdict", "known"}}),
        event("lookup.peek", 2200, {{"lemma", "spring"}}),
        event("verdict.set", 2300, {{"lemma", "spring"}, {"to", "mastered"}, {"via", "bubble"}}),
        event("scan.session", 2400, {{"words", 14}, {"durationMs", 183000}}),
        event("ocr.capture", 2500, {{"mode", "local"}, {"ok", true}}),
        event("ocr.capture", 2600, {{"mode", "online"}, {"ok", false}}),
        event("review.shown", 2700, {{"lemma", "ephemeral"}, {"level", 2}}),
        event("review.answered", 2800, {{"lemma", "ephemeral"}, {"level", 2}, {"result", "known"}}),
        event("llm.call", 2900, {{"kind", "explain"}, {"ok", true}, {"promptTokens", 412}, {"completionTokens", 268}}),
        event("llm.call", 3000, {{"kind", "story"}, {"ok", false}}),
        event("not.registered", 3100, nlohmann::json::object()),
    };
    const Career career = buildCareer(events);
    EXPECT_EQ(career.at("events"), 13);
    EXPECT_EQ(career.at("firstTs"), 1000);
    EXPECT_EQ(career.at("starts"), 1);
    EXPECT_EQ(career.at("uptimeMs"), 4000);
    EXPECT_EQ(career.at("pops"), 2);
    EXPECT_EQ(career.at("pop.selection"), 1);
    EXPECT_EQ(career.at("pop.hover"), 1);
    EXPECT_EQ(career.at("pop.new"), 1);
    EXPECT_EQ(career.at("pop.known"), 1);
    EXPECT_EQ(career.at("peeks"), 1);
    EXPECT_EQ(career.at("verdict.mastered"), 1);
    EXPECT_EQ(career.at("scan.sessions"), 1);
    EXPECT_EQ(career.at("scan.words"), 14);
    EXPECT_EQ(career.at("scan.durationMs"), 183000);
    EXPECT_EQ(career.at("ocr.captures"), 2);
    EXPECT_EQ(career.at("ocr.ok"), 1);
    EXPECT_EQ(career.at("review.shown"), 1);
    EXPECT_EQ(career.at("review.answered"), 1);
    EXPECT_EQ(career.at("review.known"), 1);
    EXPECT_EQ(career.at("llm.calls"), 2);
    EXPECT_EQ(career.at("llm.ok"), 1);
    EXPECT_EQ(career.at("llm.failed"), 1);
    EXPECT_EQ(career.at("llm.promptTokens"), 412);
    EXPECT_EQ(career.at("llm.completionTokens"), 268);
    EXPECT_EQ(career.at("llm.explain"), 1);
    EXPECT_EQ(career.at("llm.story"), 1);
}

TEST(CareerTest, PayloadFieldsOfTheWrongTypeDoNotThrow)
{
    const std::vector<Event> events{
        event("llm.call", 1000, {{"kind", "explain"}, {"ok", "yes"}, {"promptTokens", "many"}}),
        event("ocr.capture", 1100, {{"mode", "local"}, {"ok", 1}}),
        event("app.exit", 1200, {{"uptimeMs", -5}}),
    };
    const Career career = buildCareer(events);
    EXPECT_EQ(career.at("llm.calls"), 1);
    EXPECT_EQ(career.at("llm.failed"), 1);
    EXPECT_EQ(career.at("llm.promptTokens"), 0);
    EXPECT_EQ(career.at("ocr.captures"), 1);
    EXPECT_EQ(career.at("ocr.ok"), 0);
    EXPECT_EQ(career.at("uptimeMs"), -5);
}

TEST(CareerTest, JsonCarriesEveryCounter)
{
    const nlohmann::json json = careerJson(buildCareer({}));
    EXPECT_TRUE(json.is_object());
    EXPECT_EQ(json.size(), emptyCareer().size());
    EXPECT_EQ(json.at("firstTs"), 0);
}

}