#pragma once

#include <cstdint>

/**
 * @file pet_mood.h
 * @brief The moment the pet lives in: the stretch of the local day and the two after-contexts, and the pure
 *        weights they give the idle turn and each scene (ADR 0093).
 *
 * Pure logic: no clock and no Qt. The controller fills PetMood from the local time and its own context and
 * injects it, so a test pins the moment by handing down a fixed value. Everything here multiplies; nothing
 * switches, so no scene or nap is ever due at a given minute.
 */

namespace lens::core::pet {

/// @brief The four stretches of the local day the mood tells apart. The boundaries are fixed, so a scene's
///        weight for a stretch means the same stretch every day and on every machine.
enum class DayPeriod : std::uint8_t {
    Morning,
    Afternoon,
    Evening,
    Night,
};

/// @brief What the app knows about the moment the idle turn runs in.
struct PetMood {
    /// Minutes since local midnight, 0..1439. Only dayPeriodOf() reads it; the default stands for midday,
    /// and a PetMood is only ever read once the app has injected one.
    int minuteOfDay{12 * 60};
    /// A long reading just ended; for a short while the pet answers it instead of the clock.
    bool justFinishedReading{false};
    /// The pet just walked home from a stroll; for a short while it favours settling back down.
    bool justReturned{false};
};

/// @brief The stretch of day `minuteOfDay` falls in: morning 5:00 to 11:00, afternoon 11:00 to 17:00,
///        evening 17:00 to 23:00, night the rest. A value outside the day clamps in.
DayPeriod dayPeriodOf(int minuteOfDay);

/// @brief The optional mood table a scene carries in scenes.json. Every weight defaults to 1, the equal
///        lottery; an entry multiplies the chance, it never guarantees the scene.
struct SceneMood {
    double morning{1.0};
    double afternoon{1.0};
    double evening{1.0};
    double night{1.0};
    /// While the after-reading context is up: scenes about getting back to work weigh more.
    double afterReading{1.0};
    /// While the after-return context is up: scenes about settling back down weigh more.
    double afterReturn{1.0};
};

/// @brief The lottery weight of one scene under `mood`: the day stretch's weight times the context weights.
double sceneWeight(SceneMood const& weights, PetMood const& mood);

/// @brief What the mood multiplies the idle turn's scene chance by. Evening is play time, night is not;
///        just after a long reading the pet has something to answer, just home it settles first.
double moodSceneFactor(PetMood const& mood);

/// @brief What the mood multiplies the idle turn's nap chance by. Early afternoon and deep night are sleep
///        stretches, morning is not; just home it naps, just after a long reading it is still up.
double moodNapFactor(PetMood const& mood);

} // namespace lens::core::pet
