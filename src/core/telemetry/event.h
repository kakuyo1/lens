#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

/**
 * @file event.h
 * @brief The behavior-event schema: the registered names, the fields each may carry, and the
 *        JSON line one event is stored as.
 *
 * The registry is the schema's only authority. The log refuses a name it does not know and a
 * payload field the name does not list; a reader ignores what a later build added, so an older
 * build can still read the file.
 */

namespace lens::core::telemetry {

/// @brief The schema version written as `v` on every line.
inline constexpr int kEventFormat = 1;

/// @brief The longest text a field accepts when its own spec names no shorter limit, in code points.
inline constexpr std::size_t kMaxTextLength = 128;

/// @brief How a payload field is stored and checked.
enum class FieldKind : std::uint8_t {
    Text,    ///< A non-empty UTF-8 string.
    Integer, ///< A JSON integer.
    Boolean, ///< A JSON boolean.
    Choice,  ///< One string from the field's own list.
};

/// @brief One field a registered event may carry.
struct FieldSpec {
    std::string_view                  name;
    FieldKind                         kind     = FieldKind::Text;
    bool                              required = false;
    std::span<std::string_view const> choices{};     ///< Choice fields: the values accepted.
    std::size_t                       maxLength = 0; ///< Text fields: the cap in code points; zero means kMaxTextLength.
};

/// @brief One registered event: its name and the complete set of fields its payload may carry.
struct EventSpec {
    std::string_view           name;
    std::span<FieldSpec const> fields;
};

/// @return Every registered event, in declaration order.
std::span<EventSpec const> eventSpecs();

/// @return The spec named @p name, or nullptr when the registry does not know it.
const EventSpec* findEventSpec(std::string_view name);

/**
 * @brief Check a payload against an event's field list.
 *
 * @param spec    The event the payload is for.
 * @param payload The object the log is about to write.
 * @return An empty string when the payload is valid; otherwise the first reason it is not,
 *         phrased for a log line.
 *
 * @note Unknown fields fail: a typo is a schema change, not an extra field. Required fields must
 *       be present, text must be non-empty valid UTF-8 within its cap, and a Choice must name one
 *       of its values.
 */
std::string validatePayload(const EventSpec& spec, const nlohmann::json& payload);

/// @brief One event as the log and the readers carry it.
struct Event {
    long long          tsMs = 0;
    unsigned long long seq  = 0;
    std::string        device;                          ///< The short device id written as `dev`.
    std::string        name;                            ///< The registered event name.
    nlohmann::json     data = nlohmann::json::object(); ///< The validated payload.
};

/// @return The event as one JSON line, without a trailing newline.
std::string toLine(const Event& event);

/**
 * @brief Parse one JSON line.
 *
 * @param line One line from a month file; trailing whitespace is accepted.
 * @return The event, or nothing when the line is malformed, another format version, or missing a
 *         required envelope field.
 *
 * @note The reader does not consult the registry: an event a later build added parses, and the
 *       caller decides what to do with a name it does not know.
 */
std::optional<Event> parseLine(std::string_view line);

}
