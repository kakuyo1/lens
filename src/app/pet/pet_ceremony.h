#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_wander.h"
#include "core/domain/pet/pet_state.h"

/**
 * @file pet_ceremony.h
 * @brief One ceremony: the entrance from a screen edge, the goodbye back out, or a switch between two pets
 *        (ADR 0076, 0089).
 *
 * Pure logic: no window, no clock, no QObject. The controller hands in the window and the pet as values, plays the
 * walk loop and the one-shot roles this module names, and writes the positions it answers. The spot the pet belongs
 * to is the reader's home, so it travels in as a fact rather than being decided here (ADR 0100).
 */

namespace lens::app::pet {

class PetCeremony {
public:
    /// @brief Beats between the phases: a moment to stand after the look, a breath before the heart, and one after it
    ///        before the pet walks out.
    static constexpr std::chrono::milliseconds kSettlePause{140};
    static constexpr std::chrono::milliseconds kLookToHeart{120};
    static constexpr std::chrono::milliseconds kByeToExit{240};
    /// @brief How long a switch keeps the window off screen while the new pet's QML window takes its size.
    static constexpr std::chrono::milliseconds kSwapSettle{60};

    /// @brief The one-shot roles a ceremony plays and how long each runs. Empty when the pet has no art for it.
    struct Roles {
        std::optional<std::chrono::milliseconds> lookAround;
        std::optional<std::chrono::milliseconds> encourage;
    };

    /// @brief The window and the pet at one tick. `home` is where the pet belongs when the ceremony ends.
    struct Stage {
        QPoint position;
        QSize  size;
        QRect  area;
        QPoint home;
        Gait   gait{Gait::Glide};
        /// Whether the pet has the walk loop a moving phase plays.
        bool  canWalk{false};
        Roles roles;
    };

    /// @brief What the adapter does with one tick's answer. The fields are independent, so one tick can end a walk,
    ///        start a role and move the window together.
    struct Step {
        /// Where the window goes.
        std::optional<QPoint> position;
        /// The one-shot role to start now.
        std::optional<core::pet::Role> role;
        /// Start the pet's walk loop now.
        bool startWalk{false};
        /// Stop the walk loop now: the move it belonged to is over.
        bool stopWalk{false};
        /// Load the pending pet now: the old one is off screen and the swap begins.
        bool applyPending{false};
        /// The window has its new size: record `home`, the spot the clamp just settled on.
        std::optional<QPoint> home;
        /// The ceremony is over, with the pet on screen at home.
        bool settled{false};
        /// The ceremony is over, with the pet off screen.
        bool hidden{false};
    };

    /// @brief Starts an entrance: from `fromHere`, or from the off-screen base of the edge nearest home.
    Step beginEntrance(Stage const& stage, bool fromHere);
    /// @brief Starts a goodbye: to the nearest edge, a look, a heart, then off screen.
    Step beginGoodbye(Stage const& stage);
    /// @brief Starts a switch: the old pet leaves, and the new one enters from the same edge once it is loaded.
    Step beginSwitch(Stage const& stage);

    /// @brief Moves the ceremony on. `stage` is read fresh, so the swap's new size and home take effect at once.
    Step tick(std::chrono::milliseconds step, Stage const& stage);

    /// @brief The adapter could not start the walk the last step asked for; the phase carries on without it.
    Step walkFailed();
    /// @brief The adapter could not play the role the last step asked for; the phase carries on to the next one.
    Step roleFailed();

    /// @brief Forgets the ceremony where it is, as when the pet is dragged or turned off mid-way.
    void stop();

    [[nodiscard]] bool active() const
    {
        return phase_ != Phase::Inactive;
    }
    /// @return Whether the ceremony is taking the pet off screen, so an outside event must not call it back.
    [[nodiscard]] bool leaving() const;
    /// @return The edge the ceremony chose, for the log.
    [[nodiscard]] EdgeSide side() const
    {
        return side_;
    }

private:
    enum class Phase : std::uint8_t {
        Inactive,
        Entering,
        EnterLook,
        Leaving,
        LeaveLook,
        LeaveBye,
        Exiting,
        SwitchOut,
        Swap,
    };

    /// @brief Takes the stage of the call that begins a ceremony, and puts the window where it now is.
    void start(Stage const& stage);
    /// @brief Moves the window this module believes it has.
    void move(QPoint at);
    /// @brief Plays the entrance's look, or settles when the pet has none.
    void enterLook(Step& answer);
    /// @brief Plays the goodbye's look, or moves on to the heart.
    void enterLeaveLook(Step& answer);
    /// @brief Plays the goodbye's heart, or moves on to the exit walk.
    void enterLeaveBye(Step& answer);
    /// @brief Walks from where the goodbye stands to just outside its edge, or hides now.
    void beginExit(Step& answer);
    /// @brief Brings the new pet in after a swap, from the off-screen base to the clamped home.
    void beginEntranceAfterSwap(Step& answer);

    Phase                     phase_{Phase::Inactive};
    EdgeSide                  side_{EdgeSide::Left};
    QPoint                    at_;
    QPoint                    home_;
    WanderStep                walk_;
    std::chrono::milliseconds elapsed_{0};
    std::chrono::milliseconds duration_{0};
    Stage                     stage_;
    /// Whether the swap already told the adapter to load the pending pet.
    bool swapLoaded_{false};
};

} // namespace lens::app::pet
