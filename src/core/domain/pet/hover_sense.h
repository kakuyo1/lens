#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

/**
 * @file hover_sense.h
 * @brief The desktop pet's proximity awareness, separate from its action state machine.
 *
 * The pet window is only as large as its sprite and receives no pointer outside it. The controller polls the cursor
 * instead and hands this class the distance, so hovering beside the pet works without a larger window or a transparent
 * click area. This class is pure logic: distances are in canvas pixels, and the caller decides when the pet is idle.
 *
 * The outer ring only drives the eyes. Holding in the near ring first earns a greeting and, after longer, the fuller
 * reaction. Both happen at most once per approach; a cooldown turns a quick return into a quiet hello. The near ring
 * is the same for every pet, while the gaze radius, the delays and the actions they trigger come from the pet's data.
 */

namespace lens::core::pet {

/// @brief Which ring the pointer is in, from the pet's ink outward.
enum class HoverZone : std::uint8_t {
    Outside,
    Gaze,
    Near,
};

/// @brief The per-pet delays, from animations.json. The defaults match docs/pet-package-format.md.
struct HoverTiming {
    std::chrono::milliseconds noticeDelay{350};
    std::chrono::milliseconds dwellDelay{2600};
    std::chrono::milliseconds cooldown{8000};
};

/// @brief One update's result. `notice` and `dwell` are true on the update that starts each action.
struct HoverUpdate {
    HoverZone zone;
    bool      notice;
    bool      dwell;
};

class HoverSense {
public:
    /// @name Canvas-pixel distances from the pet's ink. They scale with the pet on screen.
    /// @{
    static constexpr double kGazeRadius{26.0};
    static constexpr double kNearRadius{16.0};
    /// Extra pixels before a ring lets go, so a pointer on a boundary does not flicker between zones.
    static constexpr double kExitMargin{4.0};
    /// How long the pointer must be away before a fresh approach can be greeted; part of the pet's manner, not its data.
    static constexpr std::chrono::milliseconds kRearm{800};
    /// @}

    /// @param timing Per-pet delays from animations.json; the defaults apply when it has no hover block.
    /// @param gazeRadius The eyes' reach in canvas pixels, from the behavior block; the default is kGazeRadius.
    ///        It must stay beyond the near ring, kNearRadius + kExitMargin, or the rings would order wrong.
    explicit HoverSense(HoverTiming timing = {}, double gazeRadius = kGazeRadius);

    /// @brief Forgets the approach, the cooldown and the clock. Called when hover cannot be tracked, so a reading or a
    ///        drag never leaves a due reaction behind.
    void reset();

    /**
     * @brief Advances one tick.
     * @param step Time since the last update.
     * @param distance Canvas pixels from the pointer to the pet's ink; zero inside it.
     * @param idle Whether the pet may start an action. The eyes can follow even when this is false, but no event fires.
     * @return The ring held, and whether this update starts a greeting or the fuller reaction.
     */
    HoverUpdate update(std::chrono::milliseconds step, double distance, bool idle);

    /// @return The ring held after the last update.
    HoverZone zone() const
    {
        return zone_;
    }

    /// @return Whether the pointer is close enough for the eyes to follow.
    bool watching() const
    {
        return zone_ != HoverZone::Outside;
    }

    /// @return Whether the last greeting is still cooling down.
    bool cooling() const;

    /// @return Whether this approach has already been greeted.
    bool greeted() const
    {
        return approachNoticed_;
    }

    /// @return Whether this approach has already earned the fuller reaction.
    bool finished() const
    {
        return approachDwelled_;
    }

    /// @return Whether the cooldown turned this approach into a quiet hello.
    bool quiet() const
    {
        return approachQuiet_;
    }

private:
    /// @return The ring at `distance`, keeping the previous ring through the exit margin.
    HoverZone zoneAt(double distance, HoverZone previous) const;
    /// @brief Starts a fresh approach after the rearm delay.
    void clearApproach();

    HoverTiming timing_;
    /// The eyes' reach, from the behavior block. The near ring stays the shipped constant.
    double                                   gazeRadius_;
    std::chrono::milliseconds                now_{0};
    HoverZone                                zone_{HoverZone::Outside};
    std::chrono::milliseconds                outsideSince_{0};
    std::optional<std::chrono::milliseconds> nearSince_;
    std::optional<std::chrono::milliseconds> lastNoticeAt_;
    bool                                     approachNoticed_{false};
    bool                                     approachDwelled_{false};
    bool                                     approachQuiet_{false};
};

} // namespace lens::core::pet
