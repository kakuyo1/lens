#pragma once

#include <chrono>

/**
 * @file pet_throw_mood.h
 * @brief The temper a run of landed throws builds: after two landings, a 70% chance of an angry landing, and how
 *        long the run survives without another throw (ADR 0092).
 *
 * Pure logic: PetThrow supplies the roll and owns the angry playback; this class only counts the run.
 */

namespace lens::app::pet {

class PetThrowMood {
public:
    /// @brief Landed throws needed before a landing may anger.
    static constexpr int kThrowsBeforeAngry{2};
    /// @brief The chance a qualifying landing angers.
    static constexpr double kAngryChance{0.7};
    /// @brief How long a run survives after its last landing; another throw inside it continues the run.
    static constexpr std::chrono::milliseconds kWindow{20'000};

    /// @param roll A source in [0, 1), drawn by PetThrow.
    /// @return Whether the pet is angry: true from the second landed throw in the run when `roll` is under
    ///         kAngryChance. A true result ends the run, so the next throw starts it again.
    bool landed(double roll);

    /// @brief Moves the window on; the run is forgotten when it expires.
    void advance(std::chrono::milliseconds step);

    /// @brief Forgets the run at once.
    void reset();

    int streak() const
    {
        return streak_;
    }

private:
    int                       streak_{0};
    std::chrono::milliseconds windowLeft_{0};
};

} // namespace lens::app::pet
