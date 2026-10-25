#pragma once

#include <compare>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/document/settings_section.h"

/**
 * @file known_store.h
 * @brief Local state that survives restarts: which words the reader knows, how they were
 *        marked, at what difficulty level, and the cached explanations.
 *
 * Everything lives in one JSON document -- settings.json in the reader's profile, the path
 * main.cpp works out -- that also carries the LLM API configuration. The file is loaded once
 * and rewritten on every change; keys this class does not recognise are passed through
 * untouched and never dropped.
 */

namespace lens::core {

/// @brief One cached sense, with an optional additional-language translation.
/// @note `imageable` and `imageQuery` are the picture advice, carried with the sense the way the
///       response carries it. Both are optional and advisory: an entry written before the fields
///       existed has them false/empty, which is exactly the "no picture entry" reading.
struct CachedSense {
    std::string en, zh, translation;
    bool        imageable = false;
    std::string imageQuery;
};

/// @brief Language and sense mode captured when an explanation request began.
struct CacheContext {
    std::string language;
    bool        multipleSenses = false;

    bool operator==(CacheContext const&) const = default;
};

/// @brief The cache's key as a value: the context an entry was bought under, and the word it
///        explains (docs/adr/0019). The entries are keyed by this, so a language, a sense mode and
///        a lemma are read off the key rather than unpacked from a packed string.
struct CacheKey {
    std::string language;
    std::string lemma;
    bool        multipleSenses = false;

    auto operator<=>(CacheKey const&) const = default;
};

/// @brief A word's first definition and its ordered senses, in one cache context.
/// @note Persisted IPA must be present but may be empty; older entries without it miss.
/// @note Etymology is the word's origin in one sentence, in the explanation language. Not part of
///       the key: the setting that asks for it is not a context the entry is stored under, so a
///       word explained with the setting on and one explained with it off are the same entry.
/// @note Example follows the same rule, with one difference: it is optional, not absent-or-empty.
///       `std::nullopt` means the entry was bought without asking for an example, so a request with
///       the example setting on has to buy it again (docs/adr/0033). An empty string means the model
///       was asked and had none to give, which is a settled answer and is not asked for again.
struct WordCache {
    std::string ipa, en, zh;
    std::string translation;
    /// The most common sense's part of speech, one lowercase English word. Optional like the
    /// origin: absent from every entry written before the field existed, and not a reason to drop one.
    std::string                pos;
    std::string                etymology;
    std::optional<std::string> example;
    /// Optional, like the origin: an entry bought before the chain existed has none, and offers no chain step.
    std::string              signpost;
    std::vector<std::string> collocations;
    std::vector<CachedSense> senses;
    /// The picture advice in its singular shape, alongside the per-sense copy: a word answered without
    /// `senses` carries the pair here. Advisory like the origin, and outside the cache key, so turning
    /// pictures on never buys the same definition twice.
    bool        imageable = false;
    std::string imageQuery;
};

/**
 * @brief Word marks, difficulty level, explanation language, and the explanation cache.
 *
 * @note Deliberately Qt-free: it parses JSON with nlohmann/json, so the core test targets can link it
 *       into an offline self-check with no Qt and no network.
 */
class KnownStore {
public:
    /// @brief The difficulty a document that names none starts at (CET-4).
    static constexpr int kDefaultLevel = 2;
    /// @brief The explanation language a document that names none starts at.
    static constexpr std::string_view kDefaultExplanationLang = "en";

    /**
     * @brief Read the store from disk.
     *
     * @param path Path to the shared settings document.
     * @return A store holding whatever the document contained.
     * @throws std::runtime_error If the file exists but cannot be parsed. A document that
     *         cannot be read is never silently reset: the first save afterwards would
     *         overwrite the key and the word marks. A missing file is a first run instead.
     */
    static KnownStore load(std::filesystem::path path);

    /// @return Whether the reader has marked this lemma as known.
    bool isKnown(const std::string& lemma) const;

    /// @brief Record a reader's verdict on a word.
    /// @param lemma   Dictionary form of the word.
    /// @param learned true for "already known" (never pop again), false for "new word"
    ///                (kept in the list and popped first next time).
    void mark(const std::string& lemma, bool learned);

    /// @return The known set to hand to FilterCore::filterWords, derived from the marks.
    const std::unordered_set<std::string>& known() const;

    /**
     * @brief The marks in the shape the sync merge reads (docs/adr/0042).
     *
     * @return `{"known": {lemma: {"learned": bool, "at": ms}}, "deleted": {lemma: ms}}`. A mark
     *         that was never stamped reads as at 0.
     */
    nlohmann::json syncMarks() const;

    /**
     * @brief Replace every mark with the merged ones, in the shape syncMarks() returns.
     *
     * The caller saves afterwards. A lemma in @p deleted is removed from the marks; the merge
     * never puts one lemma in both.
     */
    void applySyncMarks(const nlohmann::json& known, const nlohmann::json& deleted);

    /// @return Difficulty level, 0..7. The order is defined in GLOSSARY.md and UI-SPEC.md 4.4.
    int level() const;
    /// @throws std::out_of_range If the level is outside 0..7.
    void setLevel(int);

    /// @return Explanation language code from the service catalog.
    std::string explanationLang() const;
    void        setExplanationLang(std::string);

    /// @brief Look up a cached explanation for the current explanation language.
    /// @return The cached entry, or std::nullopt when nothing is cached.
    std::optional<WordCache> cacheGet(const std::string& lemma) const;
    void                     cachePut(const std::string& lemma, WordCache);

    /// @brief Look up or store an entry with the context captured when the request began.
    std::optional<WordCache> cacheGet(std::string const& lemma, CacheContext const& context) const;
    void                     cachePut(std::string const& lemma, WordCache entry, CacheContext const& context);

    /// @brief Remove a lemma's mark and every cached explanation context.
    /// @return True when a mark or cache entry was removed.
    bool removeLemma(std::string const& lemma);
    /// @brief Record a tombstone so a deleted word does not come back through sync, even when it
    ///        had no known mark of its own. @return True when the tombstone was added.
    bool markDeleted(std::string const& lemma);

    /**
     * @brief The document this store owns, for the section objects that persist beside it.
     *
     * StatsStore keeps its keys in the same document. The file has one owner (this class): two
     * copies loaded separately would each save their own stale view, and the later save would
     * silently drop the earlier one's changes -- a word mark or the API key. The others write
     * into this document; save() persists everything at once.
     *
     * @return The whole settings document, as loaded.
     */
    nlohmann::json& document()
    {
        return doc_;
    }

    /// @return The same document, read-only.
    const nlohmann::json& document() const
    {
        return doc_;
    }

    /**
     * @brief The document as a section, which is how a key is read and written.
     *
     * @param name Object name to bind to; empty for the document itself, whose keys are the
     *             top-level scalars.
     * @return A section over this document; every write through it saves.
     * @note Nine keys are re-stamped by save() or by StatsStore from their owner's own state, so
     *       writing one of them through this handle is silently undone. docs/adr/0024 names them;
     *       `markTimes` and `remoteDaily` were added by docs/adr/0042.
     */
    SettingsSection section(std::string_view name = {})
    {
        std::vector<std::string> path;
        if (!name.empty())
            path.emplace_back(name);
        return SettingsSection{doc_, std::move(path), [this] { save(); }};
    }

    /// @brief Write the whole document back, preserving every unrecognised key.
    /// @throws std::runtime_error If the file cannot be written.
    void save() const;

private:
    std::filesystem::path path_;
    nlohmann::json        doc_; ///< The document as loaded; the base for save().
    /// lemma -> known(true) / new word(false). Absent means never marked.
    std::unordered_map<std::string, bool> marks_;
    /// lemma -> when its mark was last set, in ms since the epoch; written to "markTimes" as
    /// "marked". Absent for a mark that predates the field, which reads as 0.
    std::unordered_map<std::string, long long> markedAt_;
    /// lemma -> when its mark was removed, in ms; a tombstone for the sync merge, written to
    /// "markTimes" as "deleted". Cleared again when the word is marked afresh.
    std::unordered_map<std::string, long long> deletedAt_;
    /// Derived view of marks_ holding only the true entries; feeds filterWords.
    std::unordered_set<std::string> known_;
    /// Keyed by explanation language + sense mode + lemma.
    std::map<CacheKey, WordCache> cache_;
    int                           level_ = kDefaultLevel;
    std::string                   lang_{kDefaultExplanationLang};
};

}
