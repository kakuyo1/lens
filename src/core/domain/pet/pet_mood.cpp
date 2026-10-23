/**
 * @file pet_mood.cpp
 * @brief The mood's fixed tables: the day stretches and the weights they and the two contexts give.
 */

#include "core/domain/pet/pet_mood.h"

#include <algorithm>

namespace lens::core::pet {

DayPeriod dayPeriodOf(int minuteOfDay)
{
    auto const minute = std::clamp(minuteOfDay, 0, 24 * 60 - 1);
    if (minute < 5 * 60) return DayPeriod::Night;
    if (minute < 11 * 60) return DayPeriod::Morning;
    if (minute < 17 * 60) return DayPeriod::Afternoon;
    if (minute < 23 * 60) return DayPeriod::Evening;
    return DayPeriod::Night;
}

double sceneWeight(SceneMood const& weights, PetMood const& mood)
{
    auto weight = 1.0;
    switch (dayPeriodOf(mood.minuteOfDay)) {
        case DayPeriod::Morning: weight = weights.morning; break;
        case DayPeriod::Afternoon: weight = weights.afternoon; break;
        case DayPeriod::Evening: weight = weights.evening; break;
        case DayPeriod::Night: weight = weights.night; break;
    }
    if (mood.justFinishedReading) weight *= weights.afterReading;
    if (mood.justReturned) weight *= weights.afterReturn;
    return weight;
}

double moodSceneFactor(PetMood const& mood)
{
    auto factor = 1.0;
    switch (dayPeriodOf(mood.minuteOfDay)) {
        case DayPeriod::Morning:
        case DayPeriod::Afternoon: factor = 1.0; break;
        case DayPeriod::Evening: factor = 1.25; break;
        case DayPeriod::Night: factor = 0.5; break;
    }
    if (mood.justFinishedReading) factor *= 1.5;
    if (mood.justReturned) factor *= 0.5;
    return factor;
}

double moodNapFactor(PetMood const& mood)
{
    auto factor = 1.0;
    switch (dayPeriodOf(mood.minuteOfDay)) {
        case DayPeriod::Morning: factor = 0.5; break;
        case DayPeriod::Afternoon: factor = 2.0; break;
        case DayPeriod::Evening: factor = 1.0; break;
        case DayPeriod::Night: factor = 2.0; break;
    }
    if (mood.justFinishedReading) factor *= 0.5;
    if (mood.justReturned) factor *= 2.0;
    return factor;
}

} // namespace lens::core::pet
