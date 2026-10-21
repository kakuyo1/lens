/**
 * @file pet_wander.cpp
 * @brief Implementation of PetWander: the ellipse clamp, the per-gait step path, and the hidden-edge geometry.
 */

#include "app/pet/pet_wander.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/pet/pet_placement.h"

namespace lens::app::pet {

namespace {

constexpr double kStepAngleTurn = 6.28318530717958647692;

/// The vertical radius as a share of the horizontal one. Float drifts the most, pounce stays almost on the ground.
double verticalRatioFor(Gait gait)
{
    switch (gait) {
        case Gait::Float:
            return 0.32;
        case Gait::Hop:
            return 0.12;
        case Gait::Trot:
            return 0.10;
        case Gait::Pounce:
            return 0.06;
        case Gait::Glide:
            return 0.18;
    }
    return 0.18;
}

} // namespace

Gait gaitOf(core::pet::MovementStyle style)
{
    switch (style) {
        case core::pet::MovementStyle::Glide:
            return Gait::Glide;
        case core::pet::MovementStyle::Float:
            return Gait::Float;
        case core::pet::MovementStyle::Hop:
            return Gait::Hop;
        case core::pet::MovementStyle::Trot:
            return Gait::Trot;
        case core::pet::MovementStyle::Pounce:
            return Gait::Pounce;
    }
    return Gait::Glide;
}

PetWander::PetWander(Gait gait, int rangePercent, core::pet::RandomSource random)
    : gait_{gait}, rangePercent_{rangePercent}, random_{std::move(random)}
{
}

void PetWander::setHome(QPoint home)
{
    home_ = home;
}

void PetWander::setBounds(QSize size, QRect area)
{
    size_ = size;
    area_ = area;
}

std::optional<WanderStep> PetWander::nextStep(QPoint from)
{
    // A step shorter than ten pixels reads as a twitch rather than a walk. Ten tries is enough on any area that
    // can hold the window; a window that fills its screen has no room at all.
    for (auto attempt = 0; attempt < 10; ++attempt) {
        auto const angle     = random_() * kStepAngleTurn;
        auto const radius    = size_.width() * rangePercent_ / 100.0;
        auto const distance  = radius * (0.35 + random_() * 0.4);
        auto const candidate = QPoint{
            from.x() + static_cast<int>(std::lround(std::cos(angle) * distance)),
            from.y() + static_cast<int>(std::lround(std::sin(angle) * distance * 0.55)),
        };
        auto const target = clamped(candidate);
        if (std::hypot(static_cast<double>(target.x() - from.x()), static_cast<double>(target.y() - from.y())) < 10.0)
            continue;
        return WanderStep{from, target};
    }
    return std::nullopt;
}

QPoint PetWander::positionAt(WanderStep const& step, double progress) const
{
    auto const t     = std::clamp(progress, 0.0, 1.0);
    auto const eased = gait_ == Gait::Pounce or gait_ == Gait::Hop
                           ? 1.0 - std::pow(1.0 - t, 3.0)
                       : t < 0.5 ? 2.0 * t * t
                                 : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;

    auto       x   = static_cast<double>(step.from.x()) + (step.to.x() - step.from.x()) * eased;
    auto       y   = static_cast<double>(step.from.y()) + (step.to.y() - step.from.y()) * eased;
    auto const arc = std::sin(t * 3.14159265358979323846);
    switch (gait_) {
        case Gait::Float:
            y += std::sin(t * kStepAngleTurn) * arcHeight(5.0);
            break;
        case Gait::Hop:
            y -= arc * arcHeight(3.0);
            break;
        case Gait::Trot:
            y -= arc * arcHeight(2.0);
            break;
        case Gait::Pounce:
            y -= arc * arcHeight(4.0);
            break;
        case Gait::Glide:
            break;
    }
    return clamped(QPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))});
}

std::optional<EdgeSide> PetWander::edgeAt(QPoint at) const
{
    if (at.x() <= area_.left() + 1) return EdgeSide::Left;
    auto const rightmost = area_.right() - size_.width() + 1;
    if (at.x() >= rightmost - 1) return EdgeSide::Right;
    return std::nullopt;
}

int PetWander::hiddenX(QRect area, QSize size, EdgeSide side, double inkLeft, double inkRight)
{
    auto const visible = std::max(0.0, inkRight - inkLeft) * kHideVisible;
    if (side == EdgeSide::Left) {
        // The ink's right edge lands exactly `visible` pixels in from the area's left edge.
        return area.left() + static_cast<int>(std::lround(visible - inkRight));
    }
    // The ink's left edge lands `visible` pixels in from the area's right edge.
    auto const rightmost = area.right() - size.width() + 1;
    return rightmost + static_cast<int>(std::lround(size.width() - inkLeft - visible));
}

int PetWander::peekOffset(std::chrono::milliseconds clock)
{
    auto const phase = static_cast<double>(clock.count()) / static_cast<double>(kPeekCycle.count()) * kStepAngleTurn;
    return static_cast<int>(std::lround((0.5 - 0.5 * std::cos(phase)) * kPeekTravel));
}

QPoint PetWander::slideAt(QPoint from, QPoint base, std::chrono::milliseconds elapsed)
{
    auto const t     = std::clamp(static_cast<double>(elapsed.count()) / static_cast<double>(kHideSlide.count()), 0.0, 1.0);
    auto const eased = 1.0 - std::pow(1.0 - t, 3.0);
    auto const x     = from.x() + (base.x() - from.x()) * eased;
    auto const y     = from.y() + (base.y() - from.y()) * eased;
    return QPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

bool PetWander::draggedOut(QPoint at, QSize size, QRect area)
{
    if (size.width() <= 0) return false;
    auto const left  = std::max(at.x(), area.left());
    auto const right = std::min(at.x() + size.width(), area.right() + 1);
    return right - left >= static_cast<double>(size.width()) * kDragOutFraction;
}

QPoint PetWander::clamped(QPoint at) const
{
    auto const inArea = clampedWithin(at, size_, area_);
    if (size_.width() <= 0) return inArea;

    auto const radius   = std::max(1.0, size_.width() * rangePercent_ / 100.0);
    auto const vertical = std::max(1.0, radius * verticalRatio());
    auto const dx       = (inArea.x() - home_.x()) / radius;
    auto const dy       = (inArea.y() - home_.y()) / vertical;
    auto const length   = std::hypot(dx, dy);
    if (length <= 1.0) return inArea;

    auto const pulled = QPoint{
        home_.x() + static_cast<int>(std::lround((inArea.x() - home_.x()) / length)),
        home_.y() + static_cast<int>(std::lround((inArea.y() - home_.y()) / length)),
    };
    return clampedWithin(pulled, size_, area_);
}

double PetWander::verticalRatio() const
{
    return verticalRatioFor(gait_);
}

double PetWander::arcHeight(double reference) const
{
    return reference * size_.width() / static_cast<double>(kReferenceWidth);
}

} // namespace lens::app::pet
