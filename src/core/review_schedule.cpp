/**
 * @file review_schedule.cpp
 * @brief The review curve and the check windows, for review_schedule.h.
 */

#include "core/review_schedule.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace lens::core {
namespace {

constexpr long long kMinuteMs = 60LL * 1000;
constexpr long long kHourMs   = 60 * kMinuteMs;
constexpr long long kDayMs    = 24 * kHourMs;

/// @brief How long a word waits after each level; index 0 is the level a forgotten word drops to.
constexpr std::array<long long, kMaxReviewLevel + 1> kIntervalsMs{
    10 * kMinuteMs, 1 * kDayMs, 3 * kDayMs, 7 * kDayMs, 21 * kDayMs, 60 * kDayMs};

}

std::optional<CheckWindow> checkWindow(ReviewFrequency const frequency)
{
    switch (frequency) {
        case ReviewFrequency::Every5To15Min:
            return CheckWindow{5 * kMinuteMs, 15 * kMinuteMs};
        case ReviewFrequency::Every15To30Min:
            return CheckWindow{15 * kMinuteMs, 30 * kMinuteMs};
        case ReviewFrequency::Every30To60Min:
            return CheckWindow{30 * kMinuteMs, 60 * kMinuteMs};
        case ReviewFrequency::Every1To2Hours:
            return CheckWindow{kHourMs, 2 * kHourMs};
        case ReviewFrequency::Off:
            break;
    }
    return std::nullopt;
}

ReviewState freshState(long long const lastLookupMs)
{
    return ReviewState{1, lastLookupMs + kIntervalsMs[1], 0};
}

ReviewState afterAnswer(ReviewState state, bool const remembered, long long const now)
{
    state.level = std::clamp(state.level + (remembered ? 1 : -1), 0, kMaxReviewLevel);
    state.due   = now + kIntervalsMs[static_cast<std::size_t>(state.level)];
    return state;
}

ReviewState afterIgnore(ReviewState state, long long const now)
{
    state.asked = now;
    return state;
}

bool isDue(ReviewState const& state, long long const now)
{
    return state.due <= now;
}

std::optional<std::string> pickDue(std::vector<ReviewCandidate> const& candidates, long long const now)
{
    ReviewCandidate const* best = nullptr;
    for (ReviewCandidate const& candidate : candidates) {
        if (not isDue(candidate.state, now) or now - candidate.state.asked < kAskCooldownMs)
            continue;
        if (best == nullptr or candidate.state.due < best->state.due)
            best = &candidate;
    }
    if (best == nullptr)
        return std::nullopt;
    return best->lemma;
}

}
