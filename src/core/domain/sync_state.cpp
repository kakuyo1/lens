/**
 * @file sync_state.cpp
 * @brief The Qt-free mapping between the stores and the sync document, for sync_state.h.
 */

#include "core/domain/sync_state.h"

#include "core/domain/settings_table.h"
#include "core/domain/sync_merge.h"

namespace lens::core::sync {
namespace {

/// @return The snapshot's entry for @p key, or null when the snapshot has none.
nlohmann::json previousOf(const nlohmann::json& snapshot, const std::string& key)
{
    return snapshot.is_object() && snapshot.contains(key) ? snapshot.at(key) : nlohmann::json();
}

/// @return One setting as the sync carries it. Its time is kept while the value matches the snapshot,
///         and moves to @p nowMs when it does not, which is how a change made here is told apart.
nlohmann::json stamped(const nlohmann::json& value, const nlohmann::json& previous, long long nowMs)
{
    const bool unchanged = previous.is_object() && previous.contains("value") && previous.at("value") == value &&
                           previous.contains("at") && previous.at("at").is_number_integer();
    return {{"value", value}, {"at", unchanged ? previous.at("at") : nlohmann::json(nowMs)}};
}

}

const std::vector<std::string>& syncedKeys()
{
    // Built from the settings table: a key is carried because its row says so, so there is no second
    // list that can fall behind the one the projection and restore-defaults read (docs/adr/0083).
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> out;
        for (std::string_view const key : settings::syncedKeys())
            out.emplace_back(key);
        return out;
    }();
    return keys;
}

nlohmann::json buildLocalDocument(const KnownStore&     store,
                                  const StatsStore&     stats,
                                  const nlohmann::json& snapshot,
                                  const std::string&    deviceId,
                                  long long             nowMs)
{
    const nlohmann::json marks = store.syncMarks();

    nlohmann::json history = nlohmann::json::array();
    for (const HistoryEntry& entry : stats.history())
        history.push_back({{"word", entry.lemma}, {"minute", entry.minute}, {"verdict", entry.verdict}});

    const nlohmann::json& document = store.document();
    nlohmann::json        settings = nlohmann::json::object();
    for (const std::string& key : syncedKeys()) {
        nlohmann::json value;
        if (key == "level")
            value = store.level();
        else if (key == "explanationLang")
            value = store.explanationLang();
        else if (document.is_object() && document.contains(key) && !document.at(key).is_null())
            value = document.at(key);
        else
            continue; // never set on this device: the default applies, and nothing is sent
        settings[key] = stamped(value, previousOf(snapshot, key), nowMs);
    }

    nlohmann::json local     = {{"format", kFormat},
                                {"known", marks.at("known")},
                                {"deleted", marks.at("deleted")},
                                {"history", std::move(history)},
                                {"settings", std::move(settings)}};
    local["daily"]           = nlohmann::json::object();
    local["daily"][deviceId] = stats.ownDailyJson();
    return local;
}

std::string contentFingerprint(const KnownStore& store, const StatsStore& stats)
{
    nlohmann::json local = buildLocalDocument(store, stats, nlohmann::json::object(), std::string(), 0);
    // Mark times are stamped too: a re-mark with the same verdict moves them without changing the
    // verdict, and that is not a change worth uploading. Only the verdicts and the tombstones are kept.
    for (auto& [lemma, entry] : local["known"].items())
        entry.erase("at");
    for (auto& [lemma, at] : local["deleted"].items())
        at = 0;
    return local.dump();
}

nlohmann::json snapshotOf(const nlohmann::json& settings)
{
    nlohmann::json snapshot = nlohmann::json::object();
    if (!settings.is_object())
        return snapshot;
    for (const std::string& key : syncedKeys())
        if (settings.contains(key) && settings.at(key).is_object())
            snapshot[key] = settings.at(key);
    return snapshot;
}

void applyMarks(KnownStore& store, const nlohmann::json& merged)
{
    store.applySyncMarks(merged.value("known", nlohmann::json::object()), merged.value("deleted", nlohmann::json::object()));
}

void applyHistory(StatsStore& stats, const nlohmann::json& merged)
{
    const nlohmann::json&     deleted = merged.value("deleted", nlohmann::json::object());
    std::vector<HistoryEntry> rows;
    for (const nlohmann::json& row : merged.value("history", nlohmann::json::array())) {
        if (!row.is_object() || !row.contains("word") || !row.at("word").is_string() || !row.contains("minute") ||
            !row.at("minute").is_string())
            continue;
        if (deleted.is_object() && deleted.contains(row.at("word").get<std::string>()))
            continue; // a tombstone keeps the deleted word out, even if a stale copy survived in the cloud
        std::string verdict = row.contains("verdict") && row.at("verdict").is_string() ? row.at("verdict").get<std::string>() : std::string();
        rows.push_back(HistoryEntry{row.at("word").get<std::string>(), row.at("minute").get<std::string>(), std::move(verdict)});
    }
    stats.replaceHistory(std::move(rows));
}

void applyDaily(StatsStore& stats, const nlohmann::json& merged, const std::string& deviceId)
{
    stats.applyRemoteDaily(merged.value("daily", nlohmann::json::object()), deviceId);
}

void applySettings(KnownStore& store, SettingsSection section, const nlohmann::json& merged)
{
    const nlohmann::json  settings = merged.value("settings", nlohmann::json::object());
    const nlohmann::json& document = store.document();
    for (auto const& row : lens::core::settings::kRows) {
        if (not row.synced)
            continue;
        const std::string key{row.key};
        if (!settings.contains(key) || !settings.at(key).is_object() || !settings.at(key).contains("value"))
            continue;
        const nlohmann::json& value = settings.at(key).at("value");
        if (value.is_null())
            continue;

        switch (row.signal) {
            case lens::core::settings::Signal::Level:
                // Outside the row's range the value is refused, not clamped: the cloud copy is ours,
                // but a corrupted one should not be able to move the reader's difficulty either.
                if (value.is_number_integer() && value.get<int>() >= row.low && value.get<int>() <= row.high &&
                    value.get<int>() != store.level())
                    store.setLevel(value.get<int>());
                break;
            case lens::core::settings::Signal::ExplanationLang:
                if (value.is_string() && !value.get<std::string>().empty() && value.get<std::string>() != store.explanationLang())
                    store.setExplanationLang(value.get<std::string>());
                break;
            default: {
                if (document.contains(key) && document.at(key) == value)
                    break;
                // Typed writes keep the encoding the key already has, so a value an older build wrote
                // as text stays text (docs/adr/0024). setRaw would store a real bool over "true". The
                // shape of the incoming value decides the write, not the row's type: the value is our
                // own document's, and a text-encoded number is a shape a reader can still open.
                if (value.is_boolean())
                    section.set<bool>(key, value.get<bool>());
                else if (value.is_number_integer())
                    section.set<int>(key, value.get<int>());
                else if (value.is_number())
                    section.set<double>(key, value.get<double>());
                else if (value.is_string())
                    section.set<std::string>(key, value.get<std::string>());
                break;
            }
        }
    }
}

}
