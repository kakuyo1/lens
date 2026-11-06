#pragma once

#include <chrono>
#include <cstdint>

#include <QPoint>
#include <QRect>
#include <QSize>

/**
 * @file pet_taskbar.h
 * @brief One pet's trip to a fixed bottom taskbar: a small projectile leap toward the roomier side of the screen,
 *        a bounce on the taskbar's top edge, a slow steady walk almost to that side's end and back, and a hopped
 *        climb back to where the pet started.
 *
 * Pure geometry and timing over the work area. The controller owns the window, the action machine and the clock;
 * this class only says where the window goes and when the walk loop starts and stops. The work area's bottom edge
 * is the taskbar's top: the pet stands on the taskbar, never over it.
 */

namespace lens::app::pet {

/// @brief The leap, the bounce, the steady walk along a taskbar's top edge and the hopped rise back to the start.
class PetTaskbar {
public:
    /// @brief Downward acceleration of the leap, in screen pixels per second squared.
    static constexpr double kGravity{2600.0};
    /// @brief The leap never runs shorter or longer than these.
    static constexpr std::chrono::milliseconds kLeapMin{450};
    static constexpr std::chrono::milliseconds kLeapMax{1200};
    /// @brief The bounce after the first landing, and the pause after it before the walk.
    static constexpr std::chrono::milliseconds kBounce{380};
    static constexpr std::chrono::milliseconds kGroundPause{450};
    /// @brief How long the pet pauses at the far end before walking back.
    static constexpr std::chrono::milliseconds kTurnPause{500};
    /// @brief The steady, unhurried pace of the round walk, in screen pixels per second, and its leg bounds.
    static constexpr double                    kWalkPace{80.0};
    static constexpr std::chrono::milliseconds kWalkMin{1200};
    static constexpr std::chrono::milliseconds kWalkMax{30000};
    /// @brief One hop of the climb back, and the slow sideways pace that spreads across it.
    static constexpr std::chrono::milliseconds kHopTime{480};
    static constexpr double                    kReturnPace{60.0};
    /// @brief How far the leap travels sideways, as a share of the work area's width, before the room caps it.
    static constexpr int kLeapPercent{8};

    /**
     * @brief The strip a fixed bottom taskbar occupies.
     * @param screen The screen's whole rectangle.
     * @param workArea The screen's work area, which is the screen less the taskbar.
     * @param autoHidden Whether Windows hides the taskbar by itself.
     * @return The strip when a fixed taskbar is docked at the bottom; an empty rectangle when the taskbar is
     *         auto-hidden, absent, or docked on another edge.
     */
    static QRect stripOf(QRect screen, QRect workArea, bool autoHidden);

    /**
     * @brief Whether a window at `from` can leap toward the roomier side and still walk one window width there.
     * @param from The window's top-left.
     * @param size The window size.
     * @param workArea The work area of its screen.
     * @return False when the window is not inside the area, or the room beside it cannot hold the leap and the
     *         walk's shortest leg.
     */
    static bool hasRoom(QPoint from, QSize size, QRect workArea);

    /// @brief One tick's answer.
    struct Step {
        /// Where the window goes.
        QPoint position;
        /// Start the walk loop now.
        bool startWalk{false};
        /// Stop the walk loop now: the walking phase it belonged to is over.
        bool stopWalk{false};
        /// The trip is over and `position` is the spot it started from.
        bool finished{false};
    };

    /**
     * @brief Starts the trip from `from`, which the climb returns to.
     * @param from The window's top-left now.
     * @param size The window size.
     * @param workArea The work area of the screen; its bottom edge is the taskbar's top.
     * @return False when hasRoom() rejects the spot; nothing changes then.
     */
    bool begin(QPoint from, QSize size, QRect workArea);

    /// @brief Forgets the trip where it is, as when the reader takes the pet or the layout changes.
    void               stop();
    [[nodiscard]] bool active() const
    {
        return active_;
    }
    /// @return The top-left the trip started from, and the one the rise returns to.
    [[nodiscard]] QPoint origin() const
    {
        return from_;
    }

    /// @brief Moves the trip on.
    /// @param step Time since the last tick. Zero or negative leaves the trip where it is.
    Step tick(std::chrono::milliseconds step);

private:
    enum class Phase : std::uint8_t {
        Leaping,
        Bouncing,
        Paused,
        WalkingOut,
        Turning,
        WalkingBack,
        Rising,
    };

    /// @return How far the walk stops short of the area's edge, so the pet almost reaches the end.
    static int endMargin(QSize size);
    /// @return How high the leap's small arc rises above its straight path, in screen pixels.
    static int arcHeight(QSize size);
    /// @return How high the bounce lifts the pet, in screen pixels.
    static int bounceHeight(QSize size);
    /// @return How high one hop of the climb rises, in screen pixels.
    static int hopRise(QSize size);
    /// @return How long a leap with a vertical drop of `distance` pixels takes.
    static std::chrono::milliseconds leapTime(int distance);
    /// @return How long one steady walk leg of `distance` pixels takes at kWalkPace.
    static std::chrono::milliseconds walkTime(int distance);

    bool                      active_{false};
    Phase                     phase_{Phase::Leaping};
    QPoint                    from_{};
    QPoint                    position_{};
    int                       floor_{0};
    int                       top_{0};
    int                       landingX_{0};
    int                       targetX_{0};
    int                       arc_{0};
    int                       bounce_{0};
    int                       hopRise_{0};
    int                       hops_{0};
    int                       climbX_{0};
    bool                      climbStarted_{false};
    std::chrono::milliseconds leap_{0};
    std::chrono::milliseconds pause_{kGroundPause};
    std::chrono::milliseconds walk_{0};
    std::chrono::milliseconds climb_{0};
    std::chrono::milliseconds elapsed_{0};
};

} // namespace lens::app::pet
