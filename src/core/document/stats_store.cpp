/**
 * @file stats_store.cpp
 * @brief Daily tallies of pops, verdicts, and token use, and the lemma export, for stats_store.h.
 */

#include "core/document/stats_store.h"
#include "core/document/known_store.h"
#include "util/log.h"
#include "util/text.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lens::core {
namespace {

/// @brief Move one word in or out of a day's known / new tally.
///
/// @param delta +1 when a verdict is attached to the day, -1 when one is taken back off it.
/// @note The clamp covers documents an earlier version wrote, which booked the count on the day
///       of the mark rather than the day of the pop: taking a verdict back can reach a bucket
///       that never held it.
void tallyVerdict(DailyUsage& day, const std::string& verdict, int delta)
{
    if (verdict == "known")
        day.learned = util::clampedAdd(day.learned, delta);
    else if (verdict == "new")
        day.fresh = util::clampedAdd(day.fresh, delta);
}

/// @brief Read one `daily` entry, tolerating anything malformed.
DailyUsage dailyFromJson(const nlohmann::json& j)
{
    DailyUsage day;
    if (!j.is_object())
        return day;
    day.pops             = j.value("pops", 0);
    day.learned          = j.value("learned", 0);
    day.fresh            = j.value("fresh", 0);
    day.peeks            = j.value("peeks", 0);
    day.promptTokens     = j.value("promptTokens", 0LL);
    day.completionTokens = j.value("completionTokens", 0LL);
    if (j.contains("models") && j["models"].is_object()) {
        for (auto it = j["models"].begin(); it != j["models"].end(); ++it) {
            if (!it.value().is_object())
                continue;
            day.models[it.key()] = ModelUsage{it.value().value("promptTokens", 0LL),
                                              it.value().value("completionTokens", 0LL)};
        }
    }
    if (day.models.empty() && (day.promptTokens != 0 || day.completionTokens != 0))
        day.models[kLegacyUsageModel] = ModelUsage{day.promptTokens, day.completionTokens};
    return day;
}

/// @brief One day's tally as the document spells it; the inverse of dailyFromJson.
nlohmann::json dailyToJson(const DailyUsage& day)
{
    nlohmann::json json = {{"pops", day.pops},
                           {"learned", day.learned},
                           {"fresh", day.fresh},
                           {"peeks", day.peeks},
                           {"promptTokens", day.promptTokens},
                           {"completionTokens", day.completionTokens}};
    json["models"]      = nlohmann::json::object();
    for (const auto& [model, usage] : day.models)
        json["models"][model] = {{"promptTokens", usage.promptTokens},
                                 {"completionTokens", usage.completionTokens}};
    return json;
}

/// @brief Add one day's tally into another, model by model.
void addInto(DailyUsage& total, const DailyUsage& part)
{
    total.pops += part.pops;
    total.learned += part.learned;
    total.fresh += part.fresh;
    total.peeks += part.peeks;
    total.promptTokens += part.promptTokens;
    total.completionTokens += part.completionTokens;
    for (const auto& [model, usage] : part.models) {
        total.models[model].promptTokens += usage.promptTokens;
        total.models[model].completionTokens += usage.completionTokens;
    }
}

}

StatsStore::StatsStore(nlohmann::json& document)
    : doc_(document)
{
    if (doc_.contains("dailyBudget") && doc_["dailyBudget"].is_number()) {
        const double amount = doc_["dailyBudget"].get<double>();
        if (std::isfinite(amount) && amount >= 0.0)
            dailyBudget_ = amount;
    }

    if (doc_.contains("history") && doc_["history"].is_array()) {
        for (const nlohmann::json& entry : doc_["history"]) {
            if (!entry.is_object())
                continue;
            const std::string lemma  = entry.value("word", std::string());
            const std::string minute = entry.value("time", std::string());
            if (lemma.empty() || util::datePart(minute).empty())
                continue; // a record with no word or no usable time is not one we can show
            history_.push_back(HistoryEntry{lemma, minute, entry.value("verdict", std::string()), entry.value("lang", std::string())});
        }
    }

    if (doc_.contains("daily") && doc_["daily"].is_object()) {
        for (auto it = doc_["daily"].begin(); it != doc_["daily"].end(); ++it) {
            if (it.key().size() != 10)
                continue;
            daily_[it.key()] = dailyFromJson(it.value());
        }
    }

    // Other devices' tallies, as the sync layer wrote them: device id -> date -> bucket.
    if (doc_.contains("remoteDaily") && doc_["remoteDaily"].is_object()) {
        for (auto device = doc_["remoteDaily"].begin(); device != doc_["remoteDaily"].end(); ++device) {
            if (!device.value().is_object())
                continue;
            for (auto it = device.value().begin(); it != device.value().end(); ++it)
                if (it.key().size() == 10)
                    remote_[device.key()][it.key()] = dailyFromJson(it.value());
        }
    }
    mergeDaily();

    LENS_INFO("stats loaded: history={} days={}", history_.size(), daily_.size());
}

void StatsStore::recordPop(std::string lemma, std::string minute, bool known, std::string language)
{
    // The pop is stamped with the verdict the word carries at this moment, so the day's tally
    // starts in the right column: a known word re-looked-up counts as known, every other word
    // as new. A later mark moves the count (recordVerdict); nothing stays uncolumned.
    history_.insert(history_.begin(),
                    HistoryEntry{std::move(lemma), std::move(minute), known ? "known" : "new", std::move(language)});
    if (history_.size() > kMaxHistory) {
        LENS_DEBUG("stats history over {} entries; dropping the oldest", kMaxHistory);
        history_.resize(kMaxHistory);
    }
    DailyUsage& day = daily_[util::datePart(history_.front().minute)];
    day.pops++;
    tallyVerdict(day, history_.front().verdict, 1);
    writeBack();
}

void StatsStore::recordPeek(const std::string& minute)
{
    daily_[util::datePart(minute)].peeks++;
    writeBack();
}

void StatsStore::recordVerdict(const std::string& lemma, std::string minute, std::string verdict)
{
    // The newest entry for the lemma, not the newest unmarked one: the newest entry is what
    // words() reads, so it is the one a mark has to reach, and a word that already carries a
    // verdict takes the new one.
    const auto entry = std::find_if(history_.begin(), history_.end(), [&](const HistoryEntry& e) { return e.lemma == lemma; });

    if (entry == history_.end()) {
        // The pop may have aged out of the history window. The tally still moved, and with no
        // entry left to say which day the word was shown, it lands on the day of the mark.
        LENS_DEBUG("stats: no history entry for '{}'; only the daily tally moves", lemma);
        tallyVerdict(daily_[util::datePart(minute)], verdict, 1);
        writeBack();
        return;
    }

    if (entry->verdict == verdict)
        return; // the word already carries this verdict; nothing moved, so the day does not grow

    // The count belongs to the day the word was shown -- the panel reads "of today's pops, how
    // many you settled this way" -- which is also the only day it can be taken back off, so a
    // re-marked word moves from one column to the other instead of landing in both.
    DailyUsage& day = daily_[util::datePart(entry->minute)];
    tallyVerdict(day, entry->verdict, -1);
    entry->verdict = verdict; // keep the pop's own minute; this is when it was shown
    tallyVerdict(day, verdict, 1);
    writeBack();
}

void StatsStore::recordUsage(const std::string& minute, long long promptTokens, long long completionTokens)
{
    recordUsage(minute, promptTokens, completionTokens, kLegacyUsageModel);
}

void StatsStore::recordUsage(const std::string& minute,
                             long long          promptTokens,
                             long long          completionTokens,
                             std::string        model)
{
    if (model.empty())
        model = kLegacyUsageModel;
    DailyUsage& day = daily_[util::datePart(minute)];
    day.promptTokens += promptTokens;
    day.completionTokens += completionTokens;
    auto& bucket = day.models[std::move(model)];
    bucket.promptTokens += promptTokens;
    bucket.completionTokens += completionTokens;
    writeBack();
}

void StatsStore::replaceHistory(std::vector<HistoryEntry> rows)
{
    std::stable_sort(rows.begin(), rows.end(), [](const HistoryEntry& a, const HistoryEntry& b) { return a.minute > b.minute; });
    if (rows.size() > kMaxHistory)
        rows.resize(kMaxHistory);
    history_ = std::move(rows);
    writeBack();
}

bool StatsStore::setDailyBudget(double amount)
{
    if (!std::isfinite(amount) || amount < 0.0)
        return false;
    dailyBudget_ = amount;
    writeBack();
    return true;
}

bool StatsStore::removeLemma(const std::string& lemma)
{
    bool removed = false;
    for (const auto& entry : history_) {
        if (entry.lemma != lemma)
            continue;
        auto const day = daily_.find(util::datePart(entry.minute));
        if (day != daily_.end()) {
            day->second.pops = util::clampedAdd(day->second.pops, -1);
            tallyVerdict(day->second, entry.verdict, -1);
        }
        removed = true;
    }
    if (!removed)
        return false;

    history_.erase(std::remove_if(history_.begin(), history_.end(), [&](const HistoryEntry& entry) {
                       return entry.lemma == lemma;
                   }),
                   history_.end());
    writeBack();
    return true;
}

namespace {

/// @return The lemmas @p scope covers, one per lemma and newest first: the rows every words export shares.
std::vector<std::string> scopedLemmas(const std::vector<HistoryEntry>&       history,
                                      ExportScope                            scope,
                                      const std::unordered_set<std::string>& knownLemmas,
                                      const std::unordered_set<std::string>& masteredLemmas)
{
    // The scope follows the verdict sets, the way the words popup's verdicts do: a lemma is
    // mastered or known when its set holds it and new when neither does, whatever its newest
    // entry's own stamp says. Mastered is checked first, as the stronger verdict.
    const auto wanted = [scope, &knownLemmas, &masteredLemmas](const std::string& lemma) {
        if (scope == ExportScope::Known) return knownLemmas.count(lemma) > 0;
        if (scope == ExportScope::Mastered) return masteredLemmas.count(lemma) > 0;
        if (scope == ExportScope::New) return knownLemmas.count(lemma) == 0 && masteredLemmas.count(lemma) == 0;
        return true;
    };

    // One line per lemma, taken at its newest entry -- the same dedup the words popup does,
    // so a word shown twice is one line here as it is one row there.
    std::unordered_set<std::string> seen;
    std::vector<std::string>        lemmas;
    for (const HistoryEntry& entry : history) {
        if (seen.insert(entry.lemma).second && wanted(entry.lemma))
            lemmas.push_back(entry.lemma);
    }

    // The verdict store's words the history never saw -- the pencil layer's mark button, an import,
    // another device's sync -- are the reader's vocabulary too: they export after the history's
    // own, in lemma order, the same rows the popup lists (docs/adr/0159).
    std::vector<std::string> extra;
    for (const std::string& lemma : masteredLemmas)
        if (wanted(lemma))
            extra.push_back(lemma);
    for (const std::string& lemma : knownLemmas)
        if (wanted(lemma))
            extra.push_back(lemma);
    std::sort(extra.begin(), extra.end());
    for (const std::string& lemma : extra)
        if (seen.insert(lemma).second)
            lemmas.push_back(lemma);
    return lemmas;
}

/// @return @p text as one field of a @p delimiter-separated row: quoted when it holds the delimiter, a double quote or a line break.
std::string cardField(const std::string& text, char delimiter)
{
    if (text.find_first_of(std::string{delimiter, '"', '\r', '\n'}) == std::string::npos)
        return text;
    std::string quoted{'"'};
    for (const char c : text) {
        if (c == '"')
            quoted += '"';
        quoted += c;
    }
    return quoted + '"';
}

/// @return One LF-terminated row of @p fields.
std::string cardRow(const std::vector<std::string>& fields, char delimiter)
{
    std::string row;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0)
            row += delimiter;
        row += cardField(fields[i], delimiter);
    }
    return row + '\n';
}

} // namespace

std::string exportLemmas(const std::vector<HistoryEntry>&       history,
                         ExportScope                            scope,
                         const std::unordered_set<std::string>& knownLemmas,
                         const std::unordered_set<std::string>& masteredLemmas)
{
    std::string text;
    for (const std::string& lemma : scopedLemmas(history, scope, knownLemmas, masteredLemmas)) {
        text += lemma;
        text += '\n';
    }
    return text;
}

std::string exportLemmasJson(const std::vector<HistoryEntry>&       history,
                             ExportScope                            scope,
                             const std::unordered_set<std::string>& knownLemmas,
                             const std::unordered_set<std::string>& masteredLemmas)
{
    nlohmann::json out = nlohmann::json::array();
    for (const std::string& lemma : scopedLemmas(history, scope, knownLemmas, masteredLemmas))
        out.push_back(lemma);
    return out.dump();
}

std::string exportCards(const std::vector<HistoryEntry>& history, ExportScope scope, const KnownStore& store, CardFormat format)
{
    const char  delimiter = format == CardFormat::Csv ? ',' : '\t';
    std::string text;
    if (format == CardFormat::Csv)
        text = "\xEF\xBB\xBF" + cardRow({"word", "ipa", "pos", "en", "zh", "example"}, delimiter);
    else
        text = "#separator:tab\n";

    for (const std::string& lemma : scopedLemmas(history, scope, store.known(), store.mastered())) {
        // A word explained only with multiple senses is stored under that context; its top-level fields are the first sense's.
        auto cached = store.cacheGet(lemma);
        if (!cached)
            cached = store.cacheGet(lemma, CacheContext{store.explanationLang(), true});
        const WordCache entry = cached.value_or(WordCache{});
        text += cardRow({lemma, entry.ipa, entry.pos, entry.en, entry.zh, entry.example.value_or("")}, delimiter);
    }
    return text;
}

void StatsStore::writeBack()
{
    nlohmann::json history = nlohmann::json::array();
    for (const HistoryEntry& entry : history_) {
        nlohmann::json row = {{"word", entry.lemma}, {"time", entry.minute}, {"verdict", entry.verdict}};
        if (!entry.language.empty())
            row["lang"] = entry.language;
        history.push_back(std::move(row));
    }
    doc_["history"] = std::move(history);

    doc_["daily"]       = ownDailyJson();
    doc_["dailyBudget"] = dailyBudget_;

    nlohmann::json remote = nlohmann::json::object();
    for (const auto& [device, days] : remote_) {
        remote[device] = nlohmann::json::object();
        for (const auto& [date, day] : days)
            remote[device][date] = dailyToJson(day);
    }
    doc_["remoteDaily"] = std::move(remote);
    mergeDaily();
}

nlohmann::json StatsStore::ownDailyJson() const
{
    nlohmann::json daily = nlohmann::json::object();
    for (const auto& [date, day] : daily_)
        daily[date] = dailyToJson(day);
    return daily;
}

void StatsStore::applyRemoteDaily(const nlohmann::json& byDevice, const std::string& localDevice)
{
    remote_.clear();
    if (byDevice.is_object()) {
        for (auto device = byDevice.begin(); device != byDevice.end(); ++device) {
            // This device's own bucket is held in daily_; a copy that came back from the cloud
            // would count the same lookups twice.
            if (!device.value().is_object() || device.key() == localDevice)
                continue;
            for (auto it = device.value().begin(); it != device.value().end(); ++it)
                if (it.key().size() == 10)
                    remote_[device.key()][it.key()] = dailyFromJson(it.value());
        }
    }
    writeBack();
}

void StatsStore::mergeDaily()
{
    merged_ = daily_;
    for (const auto& [device, days] : remote_)
        for (const auto& [date, day] : days)
            addInto(merged_[date], day);
}

}
