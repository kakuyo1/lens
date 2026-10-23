/**
 * @file pet_flight.cpp
 * @brief Implementation of PetFlight: the arc, the walls and the landing.
 */

#include "app/pet/pet_flight.h"

#include <algorithm>
#include <cmath>

namespace lens::app::pet {

namespace {

/// The longest integration step. At the launch cap this keeps one substep under 21 screen pixels, so a bounce is
/// never found after the window has already crossed the wall it should have hit.
constexpr double kSubstepSeconds = 0.008;
/// A bounce slower than this is not worth a direction change; the pet stops against the wall instead of jittering.
constexpr double kWallRest{25.0};

} // namespace

bool PetFlight::isFling(QPointF velocity)
{
    return std::isfinite(velocity.x()) and std::isfinite(velocity.y()) and
           std::hypot(velocity.x(), velocity.y()) >= kMinSpeed;
}

PetFlight::PetFlight(QPoint from, QPointF velocity, QSize size, QRect area)
    : size_{std::max(0, size.width()), std::max(0, size.height())}, area_{area}
{
    if (std::isfinite(velocity.x()) and std::isfinite(velocity.y())) {
        auto const speed = std::hypot(velocity.x(), velocity.y());
        auto const scale = speed > kMaxSpeed ? kMaxSpeed / speed : 1.0;
        vx_              = velocity.x() * scale;
        vy_              = velocity.y() * scale;
    }
    x_ = std::clamp(static_cast<double>(from.x()), static_cast<double>(area_.left()), std::max(static_cast<double>(area_.left()), rightmostX()));
    y_ = std::clamp(static_cast<double>(from.y()), static_cast<double>(area_.top()), std::max(static_cast<double>(area_.top()), floorY()));
}

QPoint PetFlight::position() const
{
    return rounded();
}

QPointF PetFlight::velocity() const
{
    return QPointF{vx_, vy_};
}

FlightStep PetFlight::advance(std::chrono::milliseconds step)
{
    if (landed_ or step.count() <= 0) return {rounded(), landed_};

    elapsed_ += step;
    auto const seconds  = static_cast<double>(step.count()) / 1000.0;
    auto const substeps = std::max(1, static_cast<int>(std::ceil(seconds / kSubstepSeconds)));
    auto const dt       = seconds / static_cast<double>(substeps);
    for (auto i = 0; i < substeps and not landed_; ++i)
        integrate(dt);

    if (elapsed_ >= kMaxDuration and not landed_) {
        // A throw that has not settled is put down on the floor rather than left floating; friction has already
        // drained every realistic arc long before this.
        x_      = std::clamp(x_, static_cast<double>(area_.left()), std::max(static_cast<double>(area_.left()), rightmostX()));
        y_      = std::max(static_cast<double>(area_.top()), floorY());
        vx_     = 0.0;
        vy_     = 0.0;
        landed_ = true;
    }
    return {rounded(), landed_};
}

void PetFlight::integrate(double dt)
{
    auto const drag = std::exp(-kAirDrag * dt);
    vy_ += kGravity * dt;
    vx_ *= drag;
    vy_ *= drag;
    x_ += vx_ * dt;
    y_ += vy_ * dt;

    auto const left  = static_cast<double>(area_.left());
    auto const right = rightmostX();
    auto const top   = static_cast<double>(area_.top());
    auto const floor = floorY();

    if (x_ < left) {
        x_ = left;
        if (vx_ < 0.0) vx_ = std::abs(vx_) < kWallRest ? 0.0 : -vx_ * kRestitution;
    } else if (x_ > right) {
        x_ = right;
        if (vx_ > 0.0) vx_ = std::abs(vx_) < kWallRest ? 0.0 : -vx_ * kRestitution;
    }

    if (y_ < top) {
        y_ = top;
        if (vy_ < 0.0) vy_ = std::abs(vy_) < kWallRest ? 0.0 : -vy_ * kRestitution;
    } else if (y_ > floor) {
        y_ = floor;
        // A hard floor contact bounces; a soft one is the start of the slide. Both lose horizontal speed to friction.
        vy_                 = vy_ > kSettleSpeed ? -vy_ * kRestitution : 0.0;
        auto const friction = kGroundFriction * dt;
        vx_                 = std::abs(vx_) <= friction ? 0.0 : vx_ - std::copysign(friction, vx_);
        if (std::abs(vx_) <= kSettleSpeed and std::abs(vy_) <= kSettleSpeed) {
            vx_     = 0.0;
            vy_     = 0.0;
            landed_ = true;
        }
    }
}

QPoint PetFlight::rounded() const
{
    auto const minX = area_.left();
    auto const minY = area_.top();
    auto const maxX = std::max(minX, area_.right() - size_.width() + 1);
    auto const maxY = std::max(minY, area_.bottom() - size_.height() + 1);
    return QPoint{std::clamp(static_cast<int>(std::lround(x_)), minX, maxX),
                  std::clamp(static_cast<int>(std::lround(y_)), minY, maxY)};
}

double PetFlight::rightmostX() const
{
    return static_cast<double>(area_.right() - size_.width() + 1);
}

double PetFlight::floorY() const
{
    return static_cast<double>(area_.bottom() - size_.height() + 1);
}

} // namespace lens::app::pet
