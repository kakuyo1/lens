#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/document/settings_section.h"

/**
 * @file known_store.h
 * @brief Local state that survives restarts: the reader's per-word verdicts, at what difficulty
 *        level, and the cached explanations.
 *
 * Everything lives in one JSON document -- settings.json in the reader's profile, the path
 * main.cpp works out -- that also carries the LLM API configuration. The file is loaded once
 * and rewritten on every change; keys this class does not recognise are passed through
 * untouched and never dropped.
 */

namespace lens::core {

/// @brief The reader's verdict on a word. None is the explicit "back to new" vote: display reads
/// absent and None the same, but the vote still rides sync so it can retire an older mark from
/// another device (docs/adr/0147). One word holds at most one of the two real verdicts, and
/// Mastered is the stronger one (docs/adr/0159).
enum class WordVerdict : uint8_t { None,
                                   Known,
                                   Mastered };

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
 * @brief Word verdicts, difficulty level, explanation language, and the explanation cache.
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

    /// @return Whether the reader has marked this lemma as mastered -- the stronger verdict that
    /// takes the word out of the scan's dashes, the review pool and the chain dots.
    bool isMasteredVerdict(const std::string& lemma) const;

    /// @brief Record a reader's verdict on a word.
    /// @param lemma   Dictionary form of the word.
    /// @param verdict Known or Mastered; None is the explicit "back to new" vote, not a mere
    ///                absence -- it rides sync so it can retire another device's older mark.
    void mark(const std::string& lemma, WordVerdict verdict);

    /// @brief Re-key every mark through @p normalize, so marks written under their surface form
    ///        join the base form the store keys by (docs/adr/0162).
    /// @param normalize Maps a stored key to its base form; identity for most keys.
    /// @return Whether anything moved; the caller saves only then. A base the reader deleted
    ///         stays deleted, a collision keeps the stronger verdict, and the old key takes a
    ///         tombstone so a sync cannot bring it back on top of the base one.
    bool rebaseMarks(std::function<std::string(std::string const&)> const& normalize);

    /// @return The words marked Known -- not the mastered ones, which mastered() holds -- to hand
    ///         to FilterCore::filterWords and the export's known bucket.
    const std::unordered_set<std::string>& known() const;

    /// @return The words the reader marked Mastered, beside the level's own band.
    const std::unordered_set<std::string>& mastered() const;

    /**
     * @brief The marks in the shape the sync merge reads (docs/adr/0042).
     *
     * @return `{"known": {lemma: {"learned": bool, "mastered": bool, "at": ms}}, "deleted":
     *         {lemma: ms}}`. A mark that was never stamped reads as at 0; a reader without the
     *         mastered field reads the pair as known, which is the old clients' downgrade read.
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

    /// @return The path the document was loaded from, which save() rewrites.
    const std::filesystem::path& path() const
    {
        return path_;
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
        return section(name, [this] { save(); });
    }

    /**
     * @brief The same section, with @p commit deciding when the write reaches the disk.
     *
     * A gesture that writes a dozen stops a second lands in the document at once, so every reader sees the value, and
     * commits once the gesture settles (docs/adr/0173). A section that saves nothing at all is the empty commit.
     */
    SettingsSection section(std::string_view name, SettingsCommit commit)
    {
        std::vector<std::string> path;
        if (!name.empty())
            path.emplace_back(name);
        return SettingsSection{doc_, std::move(path), std::move(commit)};
    }

    /// @brief Write the whole document back, preserving every unrecognised key.
    /// @throws std::runtime_error If the file cannot be written.
    void save() const;

private:
    /// @brief Make the derived views say @p verdict for @p lemma; the one place either set changes.
    /// @return Whether either set changed.
    bool indexVerdict(const std::string& lemma, WordVerdict verdict);

    std::filesystem::path path_;
    nlohmann::json        doc_; ///< The document as loaded; the base for save().
    /// lemma -> the reader's verdict: None is the explicit clear vote, Known the lighter "still
    /// learning" one, Mastered the retire-from-circulation one (docs/adr/0159). Absent means
    /// never marked; display reads None and absent the same.
    std::unordered_map<std::string, WordVerdict> marks_;
    /// lemma -> when its mark was last set, in ms since the epoch; written to "markTimes" as
    /// "marked". Absent for a mark that predates the field, which reads as 0.
    std::unordered_map<std::string, long long> markedAt_;
    /// lemma -> when its mark was removed, in ms; a tombstone for the sync merge, written to
    /// "markTimes" as "deleted". Cleared again when the word is marked afresh.
    std::unordered_map<std::string, long long> deletedAt_;
    /// Derived views of marks_: known_ holds the Known entries, mastered_ the Mastered ones.
    std::unordered_set<std::string> known_;
    std::unordered_set<std::string> mastered_;
    /// Keyed by explanation language + sense mode + lemma.
    std::map<CacheKey, WordCache> cache_;
    int                           level_ = kDefaultLevel;
    std::string                   lang_{kDefaultExplanationLang};
};

}
