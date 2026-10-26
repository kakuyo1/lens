#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include "app/pet/pet_flight.h"
#include "app/pet/pet_throw_mood.h"
#include "core/domain/pet/pet_state.h"

/**
 * @file pet_throw.h
 * @brief One throw from the reader's release to the idle pet: the arc, the landed-throw run and the angry repeat
 *        (ADR 0090, 0091, 0092).
 *
 * Pure logic: no window, no clock, no QObject. The controller owns the clock and the window, hands this module each
 * tick, the reader's interruptions, and whether a role can start, and plays what the module answers. PetFlight owns
 * the arc; PetThrowMood counts the run.
 */

namespace lens::app::pet {

/// @brief Why a throw or a landed-throw run ended. The kind decides what survives it (ADR 0092).
enum class ThrowStop : std::uint8_t {
    /// The reader caught the pet in the air: the throw is not a landing and the run starts over.
    Caught,
    /// A release too slow to be a throw: the run starts over.
    Dropped,
    /// A click: the run starts over.
    Clicked,
    /// The window changed size, so the arc no longer fits: the run starts over.
    Resized,
    /// Another pet was loaded, or the reader chose one: the run starts over.
    PetChanged,
    /// The pet was turned on or off: the run starts over.
    Toggled,
    /// A preview or a scene took the body: the run starts over.
    Staged,
    /// The screens changed under the pet: the run starts over.
    ScreenChanged,
    /// The window went away: the run starts over.
    WindowGone,
    /// Reduced motion was turned on: the run starts over.
    MotionReduced,
    /// The reader took hold of a pet already on the ground: the run survives, the plays it still owes do not.
    Held,
    /// An outside learning event: the completed landings survive, the plays still owed do not (ADR 0092).
    Learning,
};

/// @brief One tick's answer: where the window goes, whether the throw just landed, and which role plays now.
struct ThrowStep {
    /// The window's next top-left. Absent while the arc is not flying, which is when the window stays put.
    std::optional<QPoint> position;
    /// The role to start now. A landing asks for Role::Dizzy, which the adapter may replace with its own fallback.
    std::optional<core::pet::Role> role;
    /// Whether this tick completed the landing. The landing spot becomes home and the pickup pose ends.
    bool landed{false};
    /// How long the arc ran, for the landing log.
    std::chrono::milliseconds elapsed{0};
};

class PetThrow {
public:
    /// @brief How many times the angry action plays after a landing that angers.
    static constexpr int kAngryRepeats{3};

    /// @param roll A source in [0, 1), drawn once per completed landing.
    explicit PetThrow(core::pet::RandomSource roll);

    /**
     * @brief Starts a throw, or reports that the release was too slow to be one.
     * @param at The window's top-left at release.
     * @param velocity Release velocity in screen pixels per second.
     * @param size The window size.
     * @param area The work area of the screen the pet was released on.
     * @return False when the release is a drop. A play still owed from the last landing is dropped; the run continues.
     */
    bool begin(QPoint at, QPointF velocity, QSize size, QRect area);

    /**
     * @brief Ends the arc where it is and ends or keeps the run, by kind.
     * @param why What interrupted it. A throw that never landed does not count towards the run.
     */
    void stop(ThrowStop why);

    /// @return Whether the arc is still flying.
    [[nodiscard]] bool flying() const
    {
        return flight_.has_value();
    }
    /// @return Whether a landing's angry repeat is still owed. It owns the pet's body as the arc does.
    [[nodiscard]] bool repeatsOwed() const
    {
        return repeatsLeft_ > 0;
    }
    /// @return Whether an arc, an owed repeat or a run of landings is still there to interrupt. The run is what a
    ///         click or a settings change starts over; it outlives the body's own motion.
    [[nodiscard]] bool active() const
    {
        return flying() or repeatsOwed() or mood_.streak() > 0;
    }
    /// @return The completed landings in the run; zero outside one, and after a landing that angers.
    [[nodiscard]] int streak() const
    {
        return mood_.streak();
    }

    /**
     * @brief Moves the arc and the run on.
     * @param step Time since the last tick.
     * @param idle Whether a role can start now: the state machine is idle and no drag, hide, ceremony or preview holds
     *        the body.
     * @return Where the window goes, whether the throw just landed, and the role to play now. A landing's role comes
     *         before its angry repeat, which the following idle ticks ask for one at a time.
     */
    ThrowStep tick(std::chrono::milliseconds step, bool idle);

    /**
     * @brief Answers the landing step's role request.
     * @param dizzy Whether the pet's own dizzy action played. Only a dizzy landing can anger (ADR 0091).
     */
    void landingReacted(bool dizzy);

    /// @brief Drops the angry repeat still owed, as when the pet's data has no angry action.
    void dropRepeats();

private:
    core::pet::RandomSource  roll_;
    std::optional<PetFlight> flight_;
    PetThrowMood             mood_;
    int                      repeatsLeft_{0};
    /// A landing whose role request has not been answered, so its angry repeat is not settled yet.
    bool awaitingLanding_{false};
    /// Whether the landing just answered rolled for anger, before its role decides if that anger can play.
    bool angryLanding_{false};
};

} // namespace lens::app::pet
