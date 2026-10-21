/**
 * @file sync_merge.cpp
 * @brief The merge rules of docs/adr/0042, field by field.
 */

#include "core/domain/sync_merge.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace lens::core::sync {
namespace {

using json = nlohmann::json;

/// @return @p node's member @p key, or a null value when @p node is not an object or lacks it.
const json& field(const json& node, const char* key)
{
    static const json absent;
    if (!node.is_object())
        return absent;
    const auto found = node.find(key);
    return found == node.end() ? absent : *found;
}

/// @return @p node as a timestamp in milliseconds, or 0 when it is absent or not an integer.
std::int64_t stampOf(const json& node)
{
    return node.is_number_integer() ? node.get<std::int64_t>() : 0;
}

/// One lemma's mark in the known set.
struct Mark {
    bool         learned = false;
    std::int64_t at      = 0;
};

/// One setting's value and when it last changed.
struct Setting {
    json         value;
    std::int64_t at = 0;
};

using Marks      = std::map<std::string, Mark>;
using Tombstones = std::map<std::string, std::int64_t>;
using Settings   = std::map<std::string, Setting>;

/// @return The known marks in @p document. An entry without a boolean "learned" is skipped.
Marks readMarks(const json& document)
{
    Marks       marks;
    const json& known = field(document, "known");
    if (!known.is_object())
        return marks;
    for (auto it = known.begin(); it != known.end(); ++it) {
        const json& learned = field(it.value(), "learned");
        if (learned.is_boolean())
            marks[it.key()] = Mark{learned.get<bool>(), stampOf(field(it.value(), "at"))};
    }
    return marks;
}

/// @return The tombstones in @p document. An entry that is not an integer is skipped.
Tombstones readTombstones(const json& document)
{
    Tombstones  tombstones;
    const json& deleted = field(document, "deleted");
    if (!deleted.is_object())
        return tombstones;
    for (auto it = deleted.begin(); it != deleted.end(); ++it)
        if (it.value().is_number_integer())
            tombstones[it.key()] = it.value().get<std::int64_t>();
    return tombstones;
}

/// @return The settings in @p document. An entry without a "value" member is skipped.
Settings readSettings(const json& document)
{
    Settings    settings;
    const json& held = field(document, "settings");
    if (!held.is_object())
        return settings;
    for (auto it = held.begin(); it != held.end(); ++it)
        if (it.value().is_object() && it.value().contains("value"))
            settings[it.key()] = Setting{it.value().at("value"), stampOf(field(it.value(), "at"))};
    return settings;
}

/// @brief Merge two per-key maps, keeping the newer of each key's two entries.
/// @note Remote replaces local only when it is strictly newer, so a tie keeps local.
template <typename Value, typename Stamp>
std::map<std::string, Value> newerOf(std::map<std::string, Value>        local,
                                     const std::map<std::string, Value>& remote,
                                     Stamp                               stamp)
{
    for (const auto& [key, theirs] : remote) {
        const auto found = local.find(key);
        if (found == local.end() || stamp(theirs) > stamp(found->second))
            local[key] = theirs;
    }
    return local;
}

/// @return The union of both histories, one row per (word, minute), newest first, at most kMaxHistory.
json mergeHistory(const json& local, const json& remote)
{
    struct Row {
        const json* entry;
        std::string word;
        std::string minute;
    };
    std::vector<Row>                              rows;
    std::set<std::pair<std::string, std::string>> seen;
    const json* const                             sources[] = {&local, &remote};
    for (const json* source : sources) {
        const json& list = field(*source, "history");
        if (!list.is_array())
            continue;
        for (const json& entry : list) {
            const json& word   = field(entry, "word");
            const json& minute = field(entry, "minute");
            if (!word.is_string() || !minute.is_string())
                continue;
            if (seen.emplace(word.get<std::string>(), minute.get<std::string>()).second)
                rows.push_back(Row{&entry, word.get<std::string>(), minute.get<std::string>()});
        }
    }

    // (word, minute) is unique after the dedupe, so this order is total and the result does not
    // depend on which side was local.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.minute != b.minute ? a.minute > b.minute : a.word < b.word;
    });
    if (rows.size() > kMaxHistory)
        rows.resize(kMaxHistory);

    json out = json::array();
    for (const Row& row : rows)
        out.push_back(*row.entry);
    return out;
}

/// @return Every device's daily buckets: this device's from @p local, each other device's from
///         @p remote whole, and from @p local only where @p remote has none for that device.
json mergeDaily(const json& local, const json& remote, const std::string& localDevice)
{
    json        out    = json::object();
    const json& theirs = field(remote, "daily");
    if (theirs.is_object())
        for (auto it = theirs.begin(); it != theirs.end(); ++it)
            if (it.key() != localDevice && it.value().is_object())
                out[it.key()] = it.value();

    const json& mine = field(local, "daily");
    if (mine.is_object())
        for (auto it = mine.begin(); it != mine.end(); ++it)
            if (it.value().is_object() && (it.key() == localDevice || !out.contains(it.key())))
                out[it.key()] = it.value();
    return out;
}

} // namespace

json merge(const json& local, const json& remote, const std::string& localDevice)
{
    const Marks      marks  = newerOf(readMarks(local), readMarks(remote), [](const Mark& mark) { return mark.at; });
    const Tombstones tombs  = newerOf(readTombstones(local), readTombstones(remote), [](std::int64_t at) { return at; });
    const Settings   values = newerOf(readSettings(local), readSettings(remote), [](const Setting& s) { return s.at; });

    // A lemma keeps one side: the tombstone when it is at least as new as the mark, else the mark.
    json known   = json::object();
    json deleted = json::object();
    for (const auto& [lemma, mark] : marks) {
        const auto tomb = tombs.find(lemma);
        if (tomb != tombs.end() && tomb->second >= mark.at)
            continue;
        known[lemma] = json{{"learned", mark.learned}, {"at", mark.at}};
    }
    for (const auto& [lemma, at] : tombs) {
        const auto mark = marks.find(lemma);
        if (mark != marks.end() && mark->second.at > at)
            continue;
        deleted[lemma] = at;
    }

    json settings = json::object();
    for (const auto& [key, setting] : values)
        settings[key] = json{{"value", setting.value}, {"at", setting.at}};

    json history = mergeHistory(local, remote);
    // A tombstone keeps the word out of the merged history too: otherwise sync would resurrect a
    // word the reader deleted on this or another device.
    if (deleted.is_object()) {
        json filtered = json::array();
        for (const json& entry : history) {
            if (entry.is_object() && entry.contains("word") && entry.at("word").is_string() &&
                deleted.contains(entry.at("word").get<std::string>()))
                continue;
            filtered.push_back(entry);
        }
        history = std::move(filtered);
    }

    return json{{"format", kFormat},
                {"known", known},
                {"deleted", deleted},
                {"history", std::move(history)},
                {"daily", mergeDaily(local, remote, localDevice)},
                {"settings", settings}};
}

} // namespace lens::core::sync
