/**
 * @file event.cpp
 * @brief The event registry and the payload checks behind core/telemetry/event.h.
 */

#include "core/telemetry/event.h"

#include "util/text.h"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>

namespace lens::core::telemetry {
namespace {

using util::decodeUtf8;

constexpr std::string_view kLookupSources[]{"scan", "selection", "words", "hover"};
constexpr std::string_view kPopVerdicts[]{"known", "new"};
constexpr std::string_view kVerdictChoices[]{"new", "known", "mastered"};
constexpr std::string_view kVerdictRoutes[]{"bubble", "pencil", "words"};
constexpr std::string_view kLlmKinds[]{"explain", "peek", "sense", "breakdown", "correction", "portrait", "story", "example"};
constexpr std::string_view kReviewResults[]{"known", "new", "dismissed"};

constexpr FieldSpec kAppStartFields[]{
    {.name = "version", .kind = FieldKind::Text, .required = true, .maxLength = 32},
    {.name = "installed", .kind = FieldKind::Boolean, .required = true},
};

constexpr FieldSpec kAppExitFields[]{
    {.name = "uptimeMs", .kind = FieldKind::Integer, .required = true},
};

constexpr FieldSpec kScanSessionFields[]{
    {.name = "app", .kind = FieldKind::Text, .maxLength = kMaxTextLength},
    {.name = "words", .kind = FieldKind::Integer, .required = true},
    {.name = "durationMs", .kind = FieldKind::Integer, .required = true},
};

constexpr FieldSpec kLookupPopFields[]{
    {.name = "src", .kind = FieldKind::Choice, .required = true, .choices = std::span<std::string_view const>{kLookupSources}},
    {.name = "lemma", .kind = FieldKind::Text, .required = true, .maxLength = 64},
    {.name = "verdict", .kind = FieldKind::Choice, .required = true, .choices = std::span<std::string_view const>{kPopVerdicts}},
    {.name = "lang", .kind = FieldKind::Text, .maxLength = 16},
    {.name = "app", .kind = FieldKind::Text, .maxLength = kMaxTextLength},
};

constexpr FieldSpec kLookupPeekFields[]{
    {.name = "lemma", .kind = FieldKind::Text, .required = true, .maxLength = 64},
    {.name = "app", .kind = FieldKind::Text, .maxLength = kMaxTextLength},
};

constexpr FieldSpec kVerdictSetFields[]{
    {.name = "lemma", .kind = FieldKind::Text, .required = true, .maxLength = 64},
    {.name = "to", .kind = FieldKind::Choice, .required = true, .choices = std::span<std::string_view const>{kVerdictChoices}},
    {.name = "via", .kind = FieldKind::Choice, .required = true, .choices = std::span<std::string_view const>{kVerdictRoutes}},
};

constexpr FieldSpec kReviewShownFields[]{
    {.name = "lemma", .kind = FieldKind::Text, .required = true, .maxLength = 64},
    {.name = "level", .kind = FieldKind::Integer, .required = true},
};

constexpr FieldSpec kReviewAnsweredFields[]{
    {.name = "lemma", .kind = FieldKind::Text, .required = true, .maxLength = 64},
    {.name = "level", .kind = FieldKind::Integer, .required = true},
    {.name = "result", .kind = FieldKind::Choice, .required = true, .choices = std::span<std::string_view const>{kReviewResults}},
};

constexpr FieldSpec kLlmCallFields[]{
    {.name = "kind", .kind = FieldKind::Choice, .required = true, .choices = std::span<std::string_view const>{kLlmKinds}},
    {.name = "model", .kind = FieldKind::Text, .maxLength = 64},
    {.name = "promptTokens", .kind = FieldKind::Integer},
    {.name = "completionTokens", .kind = FieldKind::Integer},
    {.name = "latencyMs", .kind = FieldKind::Integer},
    {.name = "ok", .kind = FieldKind::Boolean, .required = true},
};

constexpr EventSpec kSpecs[]{
    {.name = "app.start", .fields = std::span<FieldSpec const>{kAppStartFields}},
    {.name = "app.exit", .fields = std::span<FieldSpec const>{kAppExitFields}},
    {.name = "scan.session", .fields = std::span<FieldSpec const>{kScanSessionFields}},
    {.name = "lookup.pop", .fields = std::span<FieldSpec const>{kLookupPopFields}},
    {.name = "lookup.peek", .fields = std::span<FieldSpec const>{kLookupPeekFields}},
    {.name = "verdict.set", .fields = std::span<FieldSpec const>{kVerdictSetFields}},
    {.name = "review.shown", .fields = std::span<FieldSpec const>{kReviewShownFields}},
    {.name = "review.answered", .fields = std::span<FieldSpec const>{kReviewAnsweredFields}},
    {.name = "llm.call", .fields = std::span<FieldSpec const>{kLlmCallFields}},
};

const FieldSpec* findField(const EventSpec& spec, std::string_view name)
{
    for (const FieldSpec& field : spec.fields)
        if (field.name == name)
            return &field;
    return nullptr;
}

std::string fieldReason(const FieldSpec& field, std::string_view reason)
{
    return "field '" + std::string(field.name) + "' " + std::string(reason);
}

std::string validateText(const FieldSpec& field, const nlohmann::json& value)
{
    if (!value.is_string())
        return fieldReason(field, "is not a string");

    const std::string& text    = value.get_ref<const std::string&>();
    const auto         decoded = decodeUtf8(text);
    if (!decoded)
        return fieldReason(field, "is not valid UTF-8");
    if (decoded->empty())
        return fieldReason(field, "is empty");

    const std::size_t cap = field.maxLength == 0 ? kMaxTextLength : field.maxLength;
    if (decoded->size() > cap)
        return fieldReason(field, "is longer than " + std::to_string(cap) + " characters");
    return {};
}

std::string validateChoice(const FieldSpec& field, const nlohmann::json& value)
{
    if (!value.is_string())
        return fieldReason(field, "is not a string");

    const std::string& text = value.get_ref<const std::string&>();
    if (!decodeUtf8(text))
        return fieldReason(field, "is not valid UTF-8");
    if (std::find(field.choices.begin(), field.choices.end(), std::string_view{text}) == field.choices.end())
        return fieldReason(field, "is not one of its values");
    return {};
}

} // namespace

std::span<EventSpec const> eventSpecs()
{
    return std::span<EventSpec const>{kSpecs};
}

const EventSpec* findEventSpec(std::string_view name)
{
    for (const EventSpec& spec : kSpecs)
        if (spec.name == name)
            return &spec;
    return nullptr;
}

std::string validatePayload(const EventSpec& spec, const nlohmann::json& payload)
{
    if (!payload.is_object())
        return "payload is not an object";

    for (const FieldSpec& field : spec.fields)
        if (field.required && !payload.contains(std::string(field.name)))
            return "missing field '" + std::string(field.name) + "'";

    for (auto it = payload.begin(); it != payload.end(); ++it) {
        const FieldSpec* const field = findField(spec, it.key());
        if (field == nullptr)
            return "unknown field '" + it.key() + "'";

        std::string reason;
        switch (field->kind) {
            case FieldKind::Text:
                reason = validateText(*field, it.value());
                break;
            case FieldKind::Integer:
                if (!it.value().is_number_integer())
                    reason = fieldReason(*field, "is not an integer");
                break;
            case FieldKind::Boolean:
                if (!it.value().is_boolean())
                    reason = fieldReason(*field, "is not a boolean");
                break;
            case FieldKind::Choice:
                reason = validateChoice(*field, it.value());
                break;
        }
        if (!reason.empty())
            return reason;
    }
    return {};
}

std::string toLine(const Event& event)
{
    const nlohmann::json line = {
        {"v", kEventFormat},
        {"ts", event.tsMs},
        {"dev", event.device},
        {"seq", event.seq},
        {"ev", event.name},
        {"data", event.data.is_object() ? event.data : nlohmann::json::object()},
    };
    return line.dump();
}

std::optional<Event> parseLine(std::string_view line)
{
    const nlohmann::json parsed = nlohmann::json::parse(line.begin(), line.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        return std::nullopt;

    const auto version = parsed.find("v");
    if (version == parsed.end() || !version->is_number_integer())
        return std::nullopt;
    const bool sameFormat = version->is_number_unsigned()
                                ? version->get<unsigned long long>() == static_cast<unsigned long long>(kEventFormat)
                                : version->get<long long>() == kEventFormat;
    if (!sameFormat)
        return std::nullopt;

    const auto ts   = parsed.find("ts");
    const auto dev  = parsed.find("dev");
    const auto seq  = parsed.find("seq");
    const auto name = parsed.find("ev");
    const auto data = parsed.find("data");
    if (ts == parsed.end() || !ts->is_number_integer())
        return std::nullopt;
    if (dev == parsed.end() || !dev->is_string())
        return std::nullopt;
    if (seq == parsed.end() || !seq->is_number_integer())
        return std::nullopt;
    if (name == parsed.end() || !name->is_string())
        return std::nullopt;
    if (data == parsed.end() || !data->is_object())
        return std::nullopt;

    long long tsValue = 0;
    if (ts->is_number_unsigned()) {
        const auto value = ts->get<unsigned long long>();
        if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
            return std::nullopt;
        tsValue = static_cast<long long>(value);
    } else {
        tsValue = ts->get<long long>();
    }

    unsigned long long seqValue = 0;
    if (seq->is_number_unsigned()) {
        seqValue = seq->get<unsigned long long>();
    } else {
        const auto value = seq->get<long long>();
        if (value < 0)
            return std::nullopt;
        seqValue = static_cast<unsigned long long>(value);
    }

    Event event;
    event.tsMs   = tsValue;
    event.seq    = seqValue;
    event.device = dev->get<std::string>();
    event.name   = name->get<std::string>();
    event.data   = *data;
    if (event.device.empty() || event.name.empty())
        return std::nullopt;
    return event;
}

}
