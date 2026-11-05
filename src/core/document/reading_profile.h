#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/document/stats_store.h"

/**
 * @file reading_profile.h
 * @brief The weekly reading profile: what one window of lookups looks like, counted from the local
 *        history, and the summary text the interpretation request carries.
 *
 * Qt-free like StatsStore: the window is two date strings, the known set and the cached part of
 * speech are passed in, so the counting is assertable offline. The app's duty draws the window from
 * QDate and reads the cache. docs/adr/0150 names what the profile counts and what leaves the machine.
 */

namespace lens::core {

/// @brief How many lemmas the profile names as most looked up.
inline constexpr std::size_t kProfileTopWords = 5;

/// @brief One lemma of the window and how many times the reader looked it up in that window.
struct ProfileWord {
    std::string lemma;
    int         lookups = 0;
};

/// @brief What one window of lookups looks like. Every count is over the window's pops, and every
///        distinct-word count is over the lemmas those pops named.
struct ReadingProfile {
    int                                      pops     = 0;  ///< Lookups in the window.
    int                                      words    = 0;  ///< Distinct lemmas looked up.
    int                                      fresh    = 0;  ///< Distinct lemmas the known set does not hold.
    int                                      repeated = 0;  ///< Distinct lemmas looked up two or more times.
    int                                      uncached = 0;  ///< Distinct lemmas with no cached part of speech.
    std::vector<std::pair<std::string, int>> partsOfSpeech; ///< Distinct lemmas per part of speech, most first.
    std::array<int, 24>                      hours{};       ///< Lookups per local hour, 0 through 23.
    std::vector<ProfileWord>                 top;           ///< Up to kProfileTopWords, most looked up first.
    std::vector<std::string>                 lemmas;        ///< Every distinct lemma the window looked up, most looked up first.
};

/**
 * @brief Round a count's share of a whole to a whole percent.
 * @return The percent, or 0 when @p whole is zero.
 */
inline int sharePercent(int part, int whole)
{
    return whole > 0 ? (part * 100 + whole / 2) / whole : 0;
}

/**
 * @brief Count one window of the history into a reading profile.
 *
 * A pop belongs to the window when its local date lies between @p fromDate and @p toDate, both
 * inclusive, as "YYYY-MM-DD" strings. The distinct-word counts follow the lemma, not the pop: a word
 * looked up three times is one word, repeated, and one entry in the part-of-speech mix.
 *
 * @param history      The history, newest first, as StatsStore keeps it.
 * @param known        The lemmas the reader has marked known; the set KnownStore derives.
 * @param partOfSpeech The cached part of speech for a lemma, or empty when none is cached.
 * @param fromDate     First day of the window, "YYYY-MM-DD".
 * @param toDate       Last day of the window, "YYYY-MM-DD".
 */
ReadingProfile buildReadingProfile(const std::vector<HistoryEntry>&                      history,
                                   const std::unordered_set<std::string>&                known,
                                   const std::function<std::string(const std::string&)>& partOfSpeech,
                                   const std::string&                                    fromDate,
                                   const std::string&                                    toDate);

/**
 * @brief The profile as the plain text the interpretation request sends.
 *
 * Counts, shares, the part-of-speech mix, the lookups by hour, and the top lemmas, as one paragraph.
 * Nothing else leaves the machine: no sentence, no selection, no definition, and no pop's minute.
 */
std::string readingProfileSummary(const ReadingProfile& profile, const std::string& fromDate, const std::string& toDate);

}
