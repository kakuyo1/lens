#pragma once

#include <chrono>

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

/**
 * @file pet_flight.h
 * @brief The geometry behind a thrown pet: gravity, air drag, bounces off the work area, and the landing
 *        (docs/adr/0090).
 *
 * Pure geometry over one work area. The controller owns the clock and the window; this class only says where the
 * window goes and when the throw is over. The throw stays on the screen it started on: the work area's four edges
 * are walls, so a landing never strands the pet on another monitor and the walk home stays within one screen.
 */

namespace lens::app::pet {

/// @brief One step of a throw: the window's top-left, and whether the pet has come to rest on the floor.
struct FlightStep {
    QPoint position;
    bool   landed{false};
};

/// @brief A window thrown across its screen, in screen pixels.
class PetFlight {
public:
    /// @brief Release speeds below this are a drop, not a throw, in screen pixels per second. High enough that
    ///        repositioning the pet with a brisk drag ends as a drop unless it is still moving when released.
    static constexpr double kMinSpeed{900.0};
    /// @brief No throw starts faster than this; a hard flick is clamped rather than amplified.
    static constexpr double kMaxSpeed{2600.0};
    /// @brief Downward acceleration, in screen pixels per second squared.
    static constexpr double kGravity{2600.0};
    /// @brief Exponential air drag per second: the arc loses speed without a fake terminal velocity.
    static constexpr double kAirDrag{0.35};
    /// @brief The share of the impact speed kept by a bounce off a wall, the ceiling or the floor.
    static constexpr double kRestitution{0.45};
    /// @brief Deceleration while the window slides along the floor, in screen pixels per second squared.
    static constexpr double kGroundFriction{1400.0};
    /// @brief The speed below which a grounded window is at rest; both axes must fall under it.
    static constexpr double kSettleSpeed{140.0};
    /// @brief A throw never runs longer than this, even if the numbers conspire; the pet is put down.
    static constexpr std::chrono::milliseconds kMaxDuration{6000};

    /// @param velocity Release velocity in screen pixels per second; non-finite values are not a throw.
    /// @return Whether the release is fast enough to be a throw rather than a drop.
    static bool isFling(QPointF velocity);

    /**
     * @brief Starts a throw from `from`.
     * @param from The window's top-left at release; clamped into `area`.
     * @param velocity The release velocity in screen pixels per second; clamped to kMaxSpeed.
     * @param size The window size, which the work area's inner bounds are measured by.
     * @param area The work area of the screen the pet was released on.
     */
    PetFlight(QPoint from, QPointF velocity, QSize size, QRect area);

    /// @return The window's top-left at the last step.
    QPoint position() const;
    /// @return The velocity after the last step, in screen pixels per second.
    QPointF velocity() const;
    bool    landed() const
    {
        return landed_;
    }
    std::chrono::milliseconds elapsed() const
    {
        return elapsed_;
    }

    /**
     * @brief Advances the throw.
     * @param step Time since the last call. Zero or negative leaves the throw where it is.
     * @return The window's next top-left and whether it has landed; once landed, further steps do nothing.
     */
    FlightStep advance(std::chrono::milliseconds step);

private:
    /// @brief Moves the window by one integration step and settles what it hit.
    /// @param dt The step in seconds, short enough that the window cannot cross a whole screen between collisions.
    void integrate(double dt);

    /// @return The top-left clamped into the work area, rounded as the window reports its position.
    QPoint rounded() const;
    /// @return The rightmost top-left that keeps the window inside the area.
    double rightmostX() const;
    /// @return The lowest top-left that keeps the window inside the area.
    double floorY() const;

    QSize                     size_;
    QRect                     area_;
    double                    x_{0.0};
    double                    y_{0.0};
    double                    vx_{0.0};
    double                    vy_{0.0};
    std::chrono::milliseconds elapsed_{0};
    bool                      landed_{false};
};

} // namespace lens::app::pet
