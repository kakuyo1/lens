/**
 * @file known_store.cpp
 * @brief Load, save, and explanation cache for the known-word store declared in known_store.h.
 */

#include "core/document/known_store.h"
#include "util/log.h"
#include "util/word_fields.h"

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace lens::core {
namespace {

/// @return The current time in ms since the epoch, the unit every mark time is kept in.
long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// @return A stored time, or 0 when the node is absent or not an integer.
long long timeOf(nlohmann::json const& node)
{
    return node.is_number_integer() ? node.get<long long>() : 0LL;
}

/// @return Every time in one "markTimes" object, keyed by lemma.
std::unordered_map<std::string, long long> readTimes(nlohmann::json const& times)
{
    std::unordered_map<std::string, long long> out;
    if (not times.is_object()) return out;
    for (auto it = times.begin(); it != times.end(); ++it)
        out[it.key()] = timeOf(it.value());
    return out;
}
/// Order is defined in UI-SPEC.md 4.4: B1-B2 / C1-C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / IELTS / TOEFL.
constexpr int kMaxLevel = 7;

/// @brief Read one answer field of a stored entry.
/// @return False when the entry must be dropped; a wrong type on an advisory row keeps it.
bool readAnswerField(WordCache& result, nlohmann::json const& entry, word::Field const& field)
{
    const auto key  = std::string{field.key};
    const auto held = entry.find(key);
    if (held == entry.end())
        return not field.required;
    switch (field.kind) {
        case word::ValueKind::Text: {
            if (not held->is_string())
                return field.wrongType == word::WrongType::Ignore;
            auto const value = held->get<std::string>();
            if (field.nonEmpty and value.empty())
                return false;
            switch (field.slot) {
                case word::Slot::Ipa:
                    result.ipa = value;
                    break;
                case word::Slot::Pos:
                    result.pos = value;
                    break;
                case word::Slot::En:
                    result.en = value;
                    break;
                case word::Slot::Zh:
                    result.zh = value;
                    break;
                case word::Slot::Translation:
                    result.translation = value;
                    break;
                case word::Slot::Etymology:
                    result.etymology = value;
                    break;
                case word::Slot::Example:
                    result.example = value;
                    break;
                case word::Slot::Signpost:
                    result.signpost = value;
                    break;
                case word::Slot::ImageQuery:
                    result.imageQuery = value;
                    break;
                default:
                    break; // no other slot is a text row of an answer
            }
            return true;
        }
        case word::ValueKind::TextList: {
            if (not held->is_array())
                return field.wrongType == word::WrongType::Ignore;
            std::vector<std::string> values;
            for (auto const& item : *held) {
                if (not item.is_string())
                    return field.wrongType == word::WrongType::Ignore;
                values.push_back(item.get<std::string>());
            }
            result.collocations = std::move(values);
            return true;
        }
        case word::ValueKind::Flag: {
            if (not held->is_boolean())
                return field.wrongType == word::WrongType::Ignore;
            result.imageable = held->get<bool>();
            return true;
        }
    }
    return true;
}

/// @brief Read one sense field of a stored entry, with the same wrong-type rule.
bool readSenseField(CachedSense& result, nlohmann::json const& sense, word::Field const& field)
{
    const auto key  = std::string{field.key};
    const auto held = sense.find(key);
    if (held == sense.end())
        return not field.required;
    switch (field.kind) {
        case word::ValueKind::Text: {
            if (not held->is_string())
                return field.wrongType == word::WrongType::Ignore;
            auto const value = held->get<std::string>();
            if (field.nonEmpty and value.empty())
                return false;
            switch (field.slot) {
                case word::Slot::En:
                    result.en = value;
                    break;
                case word::Slot::Zh:
                    result.zh = value;
                    break;
                case word::Slot::Translation:
                    result.translation = value;
                    break;
                case word::Slot::ImageQuery:
                    result.imageQuery = value;
                    break;
                default:
                    break; // no other slot is a text row of a sense
            }
            return true;
        }
        case word::ValueKind::Flag: {
            if (not held->is_boolean())
                return field.wrongType == word::WrongType::Ignore;
            result.imageable = held->get<bool>();
            return true;
        }
        case word::ValueKind::TextList:
            return true; // no sense row is a list
    }
    return true;
}

/// @return The answer text a stored field is written from; empty for a row that is not text.
std::string storedText(WordCache const& entry, word::Slot const slot)
{
    switch (slot) {
        case word::Slot::Ipa:
            return entry.ipa;
        case word::Slot::Pos:
            return entry.pos;
        case word::Slot::En:
            return entry.en;
        case word::Slot::Zh:
            return entry.zh;
        case word::Slot::Translation:
            return entry.translation;
        case word::Slot::Etymology:
            return entry.etymology;
        case word::Slot::Example:
            return entry.example.value_or(std::string{});
        case word::Slot::Signpost:
            return entry.signpost;
        case word::Slot::ImageQuery:
            return entry.imageQuery;
        default:
            return {};
    }
}

std::optional<WordCache> readCache(nlohmann::json const& entry, std::string const& language)
{
    if (not entry.is_object()) return std::nullopt;
    WordCache result;
    for (word::Field const& field : word::kFields) {
        if (field.scope != word::Scope::Answer)
            continue;
        if (not readAnswerField(result, entry, field))
            return std::nullopt;
    }
    // The pair is one decision, the same reading the wire parser applies.
    if (result.imageQuery.empty()) result.imageable = false;
    if (language != "en" and language != "zh" and result.translation.empty()) return std::nullopt;
    if (entry.contains("senses")) {
        if (not entry["senses"].is_array() or entry["senses"].empty()) return std::nullopt;
        for (auto const& sense : entry["senses"]) {
            if (not sense.is_object()) return std::nullopt;
            CachedSense parsed;
            for (word::Field const& field : word::kFields) {
                if (field.scope != word::Scope::Sense)
                    continue;
                if (not readSenseField(parsed, sense, field))
                    return std::nullopt;
            }
            if (parsed.imageQuery.empty()) parsed.imageable = false;
            if (language != "en" and language != "zh" and parsed.translation.empty()) return std::nullopt;
            if (result.senses.size() < 3)
                result.senses.push_back(std::move(parsed));
        }
        result.en          = result.senses.front().en;
        result.zh          = result.senses.front().zh;
        result.translation = result.senses.front().translation;
    }
    return result;
}

nlohmann::json writeCache(WordCache const& entry)
{
    nlohmann::json value = nlohmann::json::object();
    for (word::Field const& field : word::kFields) {
        if (field.scope != word::Scope::Answer)
            continue;
        // The picture advice is one pair: written below only when there is a query to run with.
        if (field.block == word::Block::Pictures)
            continue;
        switch (field.kind) {
            case word::ValueKind::Text: {
                if (field.triState) {
                    if (entry.example) value[std::string{field.key}] = *entry.example;
                    break;
                }
                auto const text = storedText(entry, field.slot);
                if (field.alwaysWrite or not text.empty())
                    value[std::string{field.key}] = text;
                break;
            }
            case word::ValueKind::TextList:
                if (not entry.collocations.empty())
                    value[std::string{field.key}] = entry.collocations;
                break;
            case word::ValueKind::Flag:
                break; // the pair below
        }
    }
    // Written only when there is a query to run with: a bare `imageable` without one would be advice
    // the duty can act on. Pre-field entries and non-imageable senses write neither key.
    if (not entry.imageQuery.empty()) {
        value["imageable"]  = entry.imageable;
        value["imageQuery"] = entry.imageQuery;
    }
    if (not entry.senses.empty()) {
        value["senses"] = nlohmann::json::array();
        for (auto const& sense : entry.senses) {
            nlohmann::json written = nlohmann::json::object();
            for (word::Field const& field : word::kFields) {
                if (field.scope != word::Scope::Sense or field.block == word::Block::Pictures)
                    continue;
                switch (field.slot) {
                    case word::Slot::En:
                        written["en"] = sense.en;
                        break;
                    case word::Slot::Zh:
                        written["zh"] = sense.zh;
                        break;
                    case word::Slot::Translation:
                        written["translation"] = sense.translation;
                        break;
                    default:
                        break;
                }
            }
            if (not sense.imageQuery.empty()) {
                written["imageable"]  = sense.imageable;
                written["imageQuery"] = sense.imageQuery;
            }
            value["senses"].push_back(std::move(written));
        }
    }
    return value;
}

} // namespace

KnownStore KnownStore::load(std::filesystem::path path)
{
    KnownStore store;
    store.path_ = std::move(path);
    LENS_TRACE("KnownStore::load: reading '{}'", store.path_.string());

    std::ifstream in(store.path_);
    if (!in) {
        store.doc_ = nlohmann::json::object(); // no file yet: first run
        LENS_INFO("settings not found at '{}'; starting from defaults", store.path_.string());
        return store;
    }

    store.doc_ = nlohmann::json::parse(in, nullptr, false);
    // The file is there but unreadable. Never silently reset it: the next save would
    // overwrite the reader's API key and word marks.
    if (store.doc_.is_discarded()) {
        LENS_ERROR("KnownStore::load: '{}' is not valid JSON; refusing to reset it",
                   store.path_.string());
        throw std::runtime_error("Cannot parse the settings JSON: " + store.path_.string());
    }
    if (!store.doc_.is_object()) {
        LENS_ERROR("KnownStore::load: '{}' does not hold a JSON object at the top level",
                   store.path_.string());
        throw std::runtime_error("The settings JSON is not an object at the top level: " +
                                 store.path_.string());
    }

    const nlohmann::json& doc = store.doc_;

    if (doc.contains("level") && doc["level"].is_number_integer())
        store.level_ = doc["level"].get<int>();
    if (store.level_ < 0 || store.level_ > kMaxLevel) {
        LENS_WARN("KnownStore::load: level {} out of range; falling back to {}", store.level_, kDefaultLevel);
        store.level_ = kDefaultLevel;
    }

    if (doc.contains("explanationLang") && doc["explanationLang"].is_string())
        store.lang_ = doc["explanationLang"].get<std::string>();

    if (doc.contains("known") && doc["known"].is_object()) {
        for (auto it = doc["known"].begin(); it != doc["known"].end(); ++it) {
            if (!it.value().is_boolean()) continue;
            const bool learned     = it.value().get<bool>();
            store.marks_[it.key()] = learned;
            if (learned) store.known_.insert(it.key());
        }
    }
    if (doc.contains("markTimes") && doc["markTimes"].is_object()) {
        store.markedAt_  = readTimes(doc["markTimes"].value("marked", nlohmann::json::object()));
        store.deletedAt_ = readTimes(doc["markTimes"].value("deleted", nlohmann::json::object()));
    }

    if (doc.contains("cache") && doc["cache"].is_object()) {
        for (auto lang = doc["cache"].begin(); lang != doc["cache"].end(); ++lang) {
            if (!lang.value().is_object()) continue;
            for (auto entry = lang.value().begin(); entry != lang.value().end(); ++entry) {
                const nlohmann::json& e = entry.value();
                if (!e.is_object()) continue;
                // Legacy flat entries remain single-sense entries. Multiple mode never falls
                // back to them; it has its own nested entry under the same lemma.
                if (auto cached = readCache(e, lang.key()))
                    store.cache_[CacheKey{lang.key(), entry.key(), false}] = std::move(*cached);
                if (e.contains("multiple"))
                    if (auto cached = readCache(e["multiple"], lang.key()); cached and not cached->senses.empty())
                        store.cache_[CacheKey{lang.key(), entry.key(), true}] = std::move(*cached);
            }
        }
    }

    LENS_INFO("settings loaded: level={} lang={} known={} cached={}", store.level_, store.lang_, store.known_.size(), store.cache_.size());
    return store;
}

bool KnownStore::isKnown(const std::string& lemma) const
{
    return known_.count(lemma) != 0;
}

void KnownStore::mark(const std::string& lemma, bool learned)
{
    marks_[lemma]    = learned;
    markedAt_[lemma] = nowMs();
    deletedAt_.erase(lemma); // a fresh mark outranks the tombstone it replaces
    if (learned)
        known_.insert(lemma);
    else
        known_.erase(lemma);
    LENS_TRACE("KnownStore::mark: '{}' -> {}", lemma, learned ? "known" : "new word");
}

const std::unordered_set<std::string>& KnownStore::known() const
{
    return known_;
}

nlohmann::json KnownStore::syncMarks() const
{
    nlohmann::json known = nlohmann::json::object();
    for (const auto& [lemma, learned] : marks_) {
        const auto at = markedAt_.find(lemma);
        known[lemma]  = {{"learned", learned}, {"at", at == markedAt_.end() ? 0LL : at->second}};
    }
    nlohmann::json deleted = nlohmann::json::object();
    for (const auto& [lemma, at] : deletedAt_)
        deleted[lemma] = at;
    return {{"known", std::move(known)}, {"deleted", std::move(deleted)}};
}

void KnownStore::applySyncMarks(const nlohmann::json& known, const nlohmann::json& deleted)
{
    marks_.clear();
    markedAt_.clear();
    deletedAt_.clear();
    known_.clear();
    if (known.is_object()) {
        for (auto it = known.begin(); it != known.end(); ++it) {
            const nlohmann::json& entry = it.value();
            if (!entry.is_object() || !entry.contains("learned") || !entry["learned"].is_boolean())
                continue;
            const bool learned  = entry["learned"].get<bool>();
            marks_[it.key()]    = learned;
            markedAt_[it.key()] = timeOf(entry.value("at", nlohmann::json()));
            if (learned) known_.insert(it.key());
        }
    }
    if (deleted.is_object()) {
        for (auto it = deleted.begin(); it != deleted.end(); ++it)
            deletedAt_[it.key()] = timeOf(it.value());
    }
}

int KnownStore::level() const
{
    return level_;
}

void KnownStore::setLevel(int level)
{
    if (level < 0 || level > kMaxLevel) {
        LENS_ERROR("KnownStore::setLevel: level {} is outside 0..{}", level, kMaxLevel);
        throw std::out_of_range("Level out of range: " + std::to_string(level));
    }
    level_ = level;
    LENS_TRACE("KnownStore::setLevel: {}", level);
}

std::string KnownStore::explanationLang() const
{
    return lang_;
}

void KnownStore::setExplanationLang(std::string lang)
{
    LENS_TRACE("KnownStore::setExplanationLang: '{}' -> '{}'", lang_, lang);
    lang_ = std::move(lang);
}

std::optional<WordCache> KnownStore::cacheGet(const std::string& lemma) const
{
    return cacheGet(lemma, {lang_, false});
}

std::optional<WordCache> KnownStore::cacheGet(std::string const& lemma, CacheContext const& context) const
{
    const auto it = cache_.find(CacheKey{context.language, lemma, context.multipleSenses});
    if (it == cache_.end()) {
        LENS_TRACE("KnownStore::cacheGet: miss for '{}' ({})", lemma, lang_);
        return std::nullopt;
    }
    LENS_TRACE("KnownStore::cacheGet: hit for '{}' ({})", lemma, lang_);
    return it->second;
}

void KnownStore::cachePut(const std::string& lemma, WordCache entry)
{
    cachePut(lemma, std::move(entry), {lang_, false});
}

void KnownStore::cachePut(std::string const& lemma, WordCache entry, CacheContext const& context)
{
    LENS_TRACE("KnownStore::cachePut: '{}' ({})", lemma, lang_);
    if (entry.senses.size() > (context.multipleSenses ? 3u : 1u))
        entry.senses.resize(context.multipleSenses ? 3u : 1u);
    cache_[CacheKey{context.language, lemma, context.multipleSenses}] = std::move(entry);
}

bool KnownStore::removeLemma(std::string const& lemma)
{
    const bool hadMark = marks_.erase(lemma) != 0;
    // A tombstone only when there was a mark to take back: it is what stops another device's
    // copy of the mark from bringing the word back.
    if (hadMark)
        deletedAt_[lemma] = nowMs();
    markedAt_.erase(lemma);
    bool removed = hadMark;
    removed      = known_.erase(lemma) != 0 || removed;

    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->first.lemma == lemma) {
            it      = cache_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed)
        LENS_INFO("removed local word state for '{}'", lemma);
    return removed;
}

bool KnownStore::markDeleted(std::string const& lemma)
{
    // A tombstone only when there is something to keep out: it is what stops another device's
    // copy of the word from bringing it back through sync.
    if (lemma.empty())
        return false;
    if (deletedAt_.count(lemma) != 0)
        return false;
    deletedAt_[lemma] = nowMs();
    LENS_INFO("marked '{}' as deleted for sync", lemma);
    return true;
}

void KnownStore::save() const
{
    // Start from the document as loaded and overwrite only our own keys; everything else
    // (API-KEY, URL, settings added later) is carried through untouched.
    nlohmann::json doc     = doc_.is_object() ? doc_ : nlohmann::json::object();
    doc["level"]           = level_;
    doc["explanationLang"] = lang_;

    nlohmann::json known = nlohmann::json::object();
    for (const auto& [lemma, learned] : marks_)
        known[lemma] = learned; // true: known
    doc["known"] = std::move(known);

    nlohmann::json times = {{"marked", nlohmann::json::object()}, {"deleted", nlohmann::json::object()}};
    for (const auto& [lemma, at] : markedAt_)
        times["marked"][lemma] = at;
    for (const auto& [lemma, at] : deletedAt_)
        times["deleted"][lemma] = at;
    doc["markTimes"] = std::move(times);

    nlohmann::json cache = nlohmann::json::object();
    for (const auto& [key, entry] : cache_) {
        auto& word = cache[key.language][key.lemma];
        if (not word.is_object()) word = nlohmann::json::object();
        if (key.multipleSenses)
            word["multiple"] = writeCache(entry);
        else
            word.update(writeCache(entry));
    }
    doc["cache"] = std::move(cache);

    std::ofstream out(path_, std::ios::trunc);
    if (!out) {
        LENS_ERROR("KnownStore::save: cannot write '{}'", path_.string());
        throw std::runtime_error("Cannot write the settings file: " + path_.string());
    }
    out << doc.dump(2) << '\n';
    LENS_INFO("settings saved: level={} lang={} marked={} cached={}", level_, lang_, marks_.size(), cache_.size());
}

}
