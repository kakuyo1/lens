/**
 * @file career.cpp
 * @brief The lifetime counters, accumulated in one pass per month.
 */

#include "core/telemetry/career.h"

#include <algorithm>

namespace lens::core::telemetry {

namespace {

constexpr std::string_view kKeys[]{
    "firstTs",
    "events",
    "starts",
    "uptimeMs",
    "pops",
    "pop.selection",
    "pop.hover",
    "pop.scan",
    "pop.words",
    "pop.new",
    "pop.known",
    "peeks",
    "verdict.known",
    "verdict.mastered",
    "verdict.new",
    "scan.sessions",
    "scan.words",
    "scan.durationMs",
    "ocr.captures",
    "ocr.ok",
    "review.shown",
    "review.answered",
    "review.known",
    "review.new",
    "review.dismissed",
    "llm.calls",
    "llm.ok",
    "llm.failed",
    "llm.promptTokens",
    "llm.completionTokens",
    "llm.explain",
    "llm.peek",
    "llm.sense",
    "llm.breakdown",
    "llm.correction",
    "llm.portrait",
    "llm.story",
    "llm.example",
};

/// @brief Add one to a counter the career carries; a value outside the table contributes nothing.
void bump(Career& career, std::string const& key, long long by = 1)
{
    const auto it = career.find(key);
    if (it != career.end())
        it->second += by;
}

long long number(nlohmann::json const& data, char const* key)
{
    const auto it = data.find(key);
    return it != data.end() && it->is_number_integer() ? it->get<long long>() : 0;
}

std::string text(nlohmann::json const& data, char const* key)
{
    const auto it = data.find(key);
    return it != data.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool flag(nlohmann::json const& data, char const* key)
{
    const auto it = data.find(key);
    return it != data.end() && it->is_boolean() && it->get<bool>();
}

void accumulate(Career& career, std::vector<Event> const& events)
{
    for (Event const& event : events) {
        if (findEventSpec(event.name) == nullptr)
            continue;
        bump(career, "events");
        if (career["firstTs"] == 0 || event.tsMs < career["firstTs"])
            career["firstTs"] = event.tsMs;

        const nlohmann::json& data = event.data;
        if (event.name == "app.start") {
            bump(career, "starts");
        } else if (event.name == "app.exit") {
            bump(career, "uptimeMs", number(data, "uptimeMs"));
        } else if (event.name == "scan.session") {
            bump(career, "scan.sessions");
            bump(career, "scan.words", number(data, "words"));
            bump(career, "scan.durationMs", number(data, "durationMs"));
        } else if (event.name == "lookup.pop") {
            bump(career, "pops");
            bump(career, "pop." + text(data, "src"));
            bump(career, "pop." + text(data, "verdict"));
        } else if (event.name == "lookup.peek") {
            bump(career, "peeks");
        } else if (event.name == "verdict.set") {
            bump(career, "verdict." + text(data, "to"));
        } else if (event.name == "review.shown") {
            bump(career, "review.shown");
        } else if (event.name == "review.answered") {
            bump(career, "review.answered");
            bump(career, "review." + text(data, "result"));
        } else if (event.name == "ocr.capture") {
            bump(career, "ocr.captures");
            if (flag(data, "ok"))
                bump(career, "ocr.ok");
        } else if (event.name == "llm.call") {
            bump(career, "llm.calls");
            bump(career, flag(data, "ok") ? "llm.ok" : "llm.failed");
            bump(career, "llm.promptTokens", number(data, "promptTokens"));
            bump(career, "llm.completionTokens", number(data, "completionTokens"));
            bump(career, "llm." + text(data, "kind"));
        }
    }
}

} // namespace

Career emptyCareer()
{
    Career career;
    for (const std::string_view key : kKeys)
        career[std::string(key)] = 0;
    return career;
}

Career buildCareer(std::vector<Event> const& events)
{
    Career career = emptyCareer();
    accumulate(career, events);
    return career;
}

Career buildCareerFromLog(EventLog const& log)
{
    Career career = emptyCareer();
    for (std::string const& month : log.months())
        accumulate(career, log.readMonth(month));
    return career;
}

nlohmann::json careerJson(Career const& career)
{
    nlohmann::json json = nlohmann::json::object();
    for (auto const& [key, value] : career)
        json[key] = value;
    return json;
}

}