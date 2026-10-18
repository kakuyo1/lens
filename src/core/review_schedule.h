#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file review_schedule.h
 * @brief When a looked-up word falls due for a question, and how each answer moves it.
 *
 * Pure functions over epoch milliseconds: no Qt, no clock, no store. The app layer reads the
 * clock, turns the history's local minutes into epoch, and persists what comes back
 * (review_store.h). Two clocks meet in the duty and never here: the per-word curve in this
 * file, and the reader's check frequency, which only sets how often the pet looks for a due
 * word. Changing the frequency never changes a curve.
 */

namespace lens::core {

/// @brief One word's place on the review curve.
struct ReviewState {
    int       level = 0; ///< 0..kMaxReviewLevel; indexes the interval table.
    long long due   = 0; ///< Epoch ms at which the word falls due for a question.
    long long asked = 0; ///< Epoch ms it was last passed over unanswered; 0 when never.
};

/// @brief A word that may be asked, with the state the store holds or a fresh one.
struct ReviewCandidate {
    std::string lemma;
    ReviewState state;
};

/// @brief How often the pet checks for a due word. Affects the check only, never the curve.
///
/// The numbers are persisted in the settings document (review_store.h), so a level is appended
/// rather than inserted: renumbering would silently move every reader's stored choice to the
/// level next to it. The list the settings page draws is ordered by duration, not by value.
enum class ReviewFrequency : std::uint8_t { Off            = 0,
                                            Every15To30Min = 1,
                                            Every30To60Min = 2,
                                            Every1To2Hours = 3,
                                            Every5To15Min  = 4 };

/// @brief The last enumerator, which is what a stored or asked-for value is clamped to.
inline constexpr ReviewFrequency kLastReviewFrequency    = ReviewFrequency::Every5To15Min;
inline constexpr ReviewFrequency kDefaultReviewFrequency = ReviewFrequency::Every30To60Min;
inline constexpr int             kMaxReviewLevel         = 5;
/// @brief A word passed over unanswered is not asked again for this long.
inline constexpr long long kAskCooldownMs = 2LL * 60 * 60 * 1000;

/// @brief The random window between two checks for a frequency; empty when checks are off.
struct CheckWindow {
    long long minMs = 0;
    long long maxMs = 0;
};

/// @return The check window for @p frequency; std::nullopt when checking is off.
std::optional<CheckWindow> checkWindow(ReviewFrequency frequency);

/**
 * @brief The state of a word the first time it is asked: level 1, due one day after its last lookup.
 * @param lastLookupMs Epoch ms of the word's newest history entry.
 */
ReviewState freshState(long long lastLookupMs);

/**
 * @brief The state after an answer: one level up when remembered, one down when forgotten, floored at 0 and capped at the top.
 * @note The due time restarts from @p now at the new level's interval, so a forgotten word comes back in ten minutes.
 */
ReviewState afterAnswer(ReviewState state, bool remembered, long long now);

/// @return @p state with its asked stamp set to @p now; the level and due time stay as they were.
ReviewState afterIgnore(ReviewState state, long long now);

/// @return Whether the word has fallen due by @p now.
bool isDue(const ReviewState& state, long long now);

/**
 * @brief The word to ask about next: the earliest due one that is not still cooling down.
 * @param candidates Words to choose from; the caller has already dropped those without a cached explanation.
 * @return The lemma, or std::nullopt when nothing is due and free.
 */
std::optional<std::string> pickDue(const std::vector<ReviewCandidate>& candidates, long long now);

}
