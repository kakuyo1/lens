#pragma once

#include <nlohmann/json.hpp>

#include <charconv>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/**
 * @file settings_section.h
 * @brief One object of the settings document, as the unit a key is read and written through.
 *
 * The document is one JSON object several owners write into. The outermost section is the document
 * itself -- the top-level scalars -- and a named one is a nested object (`UPDATE`, `PET`,
 * `MODELS`); one handle serves both.
 *
 * A value is read whether the document holds it as its own type or as text (a real `bool` or
 * `"true"`), and a write keeps the encoding the key already has (docs/adr/0024). Absent, null and
 * empty are three different things and stay three: value() falls back, clear() writes null,
 * remove() takes the key away.
 *
 * Ownership is deliberately not enforced: nine keys are re-stamped from a strong-typed owner's
 * own state, so a write to one of them through this handle is silently reverted. docs/adr/0024
 * names them and they are asserted in the unit suite.
 */

namespace lens::core {

/// @brief Persists the document after a write. An empty one makes the section a view that saves nothing.
using SettingsCommit = std::function<void()>;

namespace detail {

template <typename T>
inline constexpr bool always_false = false;

/// @return Whether a node holds the text "true" or "false"; anything else is @p fallback.
bool textFlag(nlohmann::json const& held, bool fallback);

/**
 * @brief Read a number the document stored as text.
 *
 * The whole string has to be one: "3" reads as 3, "3x" does not read at all. Stricter than
 * `std::stoi`, which would take the 3 and leave the rest.
 *
 * @return The number, or @p fallback.
 */
template <typename T>
T numberFromText(nlohmann::json const& held, T fallback)
{
    if (!held.is_string())
        return fallback;
    const std::string& text = held.get_ref<const std::string&>();
    T                  out{};
    const auto         parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return fallback;
    return out;
}

} // namespace detail

/**
 * @brief A view over one object of the settings document, one object deeper per `object()` call.
 *
 * @note Reads never touch the document: a section bound to an object that is not there reads as
 *       defaults, and the object appears on the first write to it.
 */
class SettingsSection {
public:
    /**
     * @brief Bind to @p document itself, or to one of the objects inside it.
     * @param document The document's root object. KnownStore::load leaves it as one.
     * @param path     Object names to walk, outermost first; empty is the document itself.
     * @param commit   Called after every write; see SettingsCommit.
     */
    SettingsSection(nlohmann::json& document, std::vector<std::string> path = {}, SettingsCommit commit = {});

    /**
     * @brief Read one key.
     *
     * A key held as text and one held as its own type both read; an absent key, a null one, and
     * anything of the wrong type read as @p fallback.
     *
     * @param key      The key, as the document spells it.
     * @param fallback Returned when there is nothing to read.
     */
    template <typename T>
    T value(std::string_view key, T fallback) const;

    /**
     * @brief Write one key, keeping the encoding the key already has, and save.
     *
     * A key the document holds as text stays text, which is what keeps an older build able to
     * read it; a key that is absent is born as its own type. See docs/adr/0024.
     *
     * @param key   The key, as the document spells it.
     */
    template <typename T>
    void set(std::string_view key, const T& value);

    /// @brief Write a value the caller already holds as JSON, as it is, and save.
    /// @note Unlike set<T>, this does not keep the key's existing encoding; it is for values such as
    ///       a whole object that have no typed form.
    void setRaw(std::string_view key, const nlohmann::json& value);

    /// @return Whether @p key is in the object at all; a null or an empty one is there.
    bool has(std::string_view key) const;

    /// @brief Write null under @p key and save; an empty string is a different thing.
    void clear(std::string_view key);

    /// @brief Take @p key out of the object and save; unlike clear(), nothing is left behind.
    void remove(std::string_view key);

    /**
     * @brief The object named @p name, as another section over the same document.
     * @param name The key the object sits under.
     * @note Nothing is created until something is written.
     */
    SettingsSection object(std::string_view name) const;

private:
    /// @return The object this section names, or nullptr when a segment is missing or not an object.
    const nlohmann::json* find() const;
    /// @return The same, writable, or nullptr when it is not there; never creates anything.
    nlohmann::json* find();
    /// @return The object this section names, creating every missing object on the way.
    nlohmann::json& ensure();
    void            commit() const;

    nlohmann::json&          document_;
    std::vector<std::string> path_;
    SettingsCommit           commit_;
};

template <typename T>
T SettingsSection::value(std::string_view const key, T fallback) const
{
    const nlohmann::json* node = find();
    if (node == nullptr)
        return fallback;
    const auto found = node->find(std::string{key});
    if (found == node->end() || found->is_null())
        return fallback;
    const nlohmann::json& held = *found;

    if constexpr (std::is_same_v<T, bool>) {
        return held.is_boolean() ? held.get<bool>() : detail::textFlag(held, fallback);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return held.is_string() ? held.get<std::string>() : fallback;
    } else if constexpr (std::is_same_v<T, int>) {
        return held.is_number_integer() ? held.get<int>() : detail::numberFromText(held, fallback);
    } else if constexpr (std::is_same_v<T, double>) {
        return held.is_number() ? held.get<double>() : detail::numberFromText(held, fallback);
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (!held.is_array())
            return fallback;
        std::vector<std::string> out;
        out.reserve(held.size());
        for (const nlohmann::json& item : held)
            if (item.is_string())
                out.push_back(item.get<std::string>());
        return out;
    } else {
        static_assert(detail::always_false<T>, "SettingsSection has no read rule for this type");
        return fallback;
    }
}

template <typename T>
void SettingsSection::set(std::string_view const key, const T& value)
{
    nlohmann::json&   node = ensure();
    const std::string name{key};
    const auto        held   = node.find(name);
    const bool        asText = held != node.end() && held->is_string();

    if constexpr (std::is_same_v<T, bool>) {
        node[name] = asText ? nlohmann::json(value ? "true" : "false") : nlohmann::json(value);
    } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
        // dump() rather than std::to_string, which would spell 2.5 as "2.500000". ponytail: a
        // non-finite double has no JSON number to write and lands as null by either path; no
        // caller passes one (CostDuty refuses a non-finite budget).
        node[name] = asText ? nlohmann::json(nlohmann::json(value).dump()) : nlohmann::json(value);
    } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<std::string>>) {
        node[name] = value;
    } else {
        static_assert(detail::always_false<T>, "SettingsSection has no write rule for this type");
    }
    commit();
}

}
