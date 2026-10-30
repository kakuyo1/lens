#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <QPoint>

#include "app/pet/pet_wander.h"
#include "core/domain/pet/pet_state.h"

/**
 * @file pet_stroll.h
 * @brief One idle stroll: the wait, the steps, the pauses, and the roll that ends a step at an edge in a hide
 *        (ADR 0076, 0095).
 *
 * Pure logic: no window, no clock, no QObject. The controller hands in time, the pointer's presence and the window's
 * spot, plays the walk loop and the hide this module asks for, and reads the geometry -- the home ellipse, the step
 * path, the edge -- from PetWander.
 */

namespace lens::app::pet {

class PetStroll {
public:
    /// @brief The adapter's facts at one tick.
    struct Facts {
        /// Whether the pointer is on the pet, which borrows the pet's idle time for its hover.
        bool pointerOnPet{false};
    };

    /// @brief What one tick asks the adapter to do.
    struct Step {
        /// Where the window goes.
        std::optional<QPoint> position;
        /// Start the walk loop now.
        bool startWalk{false};
        /// Stop the walk loop now: the step it belonged to is over.
        bool stopWalk{false};
        /// The step ended on this edge and the roll said hide: the adapter starts the hide from here.
        std::optional<EdgeSide> hide;
        /// The stroll ended: the pet is back at rest, so the adapter records the return and looks around once.
        bool finished{false};
    };

    /// @param roll A source in [0, 1), drawn for the wait, the step count, the pauses and the hide roll.
    explicit PetStroll(core::pet::RandomSource roll);

    /**
     * @brief Takes the pet's movement facts and draws the first wait.
     * @param behavior The stroll wait and step count.
     * @param gait The path shape, which sets the pause between two steps.
     * @param walkCycle One walk loop's playback, in milliseconds.
     * @param canHide Whether the pet has peek art, so a step may end in a hide.
     */
    void load(core::pet::BehaviorSpec behavior, Gait gait, std::chrono::milliseconds walkCycle, bool canHide);

    /// @brief Draws a fresh wait before the next stroll.
    void wait();
    /// @brief Ends a running stroll without drawing a new wait, as a gate the adapter closed does.
    void stop();
    /// @brief The reader pressed the pet: ends the stroll and draws one wait that the pointer then holds off.
    void hover();

    /**
     * @brief Moves the stroll on.
     * @param step Time since the last tick.
     * @param facts Whether the pointer is on the pet.
     * @param wander The pet's geometry, already set for this frame.
     * @param window The window's top-left now, which a new step starts from.
     */
    Step tick(std::chrono::milliseconds step, Facts const& facts, PetWander& wander, QPoint window);

    /// @brief The adapter could not start the walk the last step asked for; the stroll ends.
    Step walkFailed();

    /// @return Whether a step is running.
    [[nodiscard]] bool walking() const
    {
        return phase_ == Phase::Walking;
    }
    /// @return The time left before the next stroll; zero while one runs.
    [[nodiscard]] std::chrono::milliseconds waitLeft() const
    {
        return wait_;
    }

private:
    enum class Phase : std::uint8_t {
        Waiting,
        Walking,
        Pausing,
    };

    void                      drawWait();
    Step                      beginStep(PetWander& wander, QPoint window);
    Step                      finish();
    std::chrono::milliseconds nextPause() const;

    core::pet::RandomSource   roll_;
    core::pet::BehaviorSpec   behavior_{};
    Gait                      gait_{Gait::Glide};
    std::chrono::milliseconds walkCycle_{0};
    bool                      canHide_{false};
    Phase                     phase_{Phase::Waiting};
    std::optional<WanderStep> step_;
    std::chrono::milliseconds elapsed_{0};
    std::chrono::milliseconds pauseLeft_{0};
    std::chrono::milliseconds wait_{0};
    /// Steps still to take in this stroll, including the one running.
    int stepsLeft_{0};
    /// Whether the pointer is already holding off a wait it borrowed.
    bool hovering_{false};
};

} // namespace lens::app::pet
