/**
 * @file pet_taskbar.cpp
 * @brief Implementation of PetTaskbar: the strip test, the projectile leap, the bounce, the round walk and the
 *        slow rise.
 */

#include "app/pet/pet_taskbar.h"

#include <algorithm>
#include <cmath>

namespace lens::app::pet {

using namespace std::chrono_literals;

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

QRect PetTaskbar::stripOf(QRect screen, QRect workArea, bool autoHidden)
{
    if (autoHidden or not screen.isValid() or not workArea.isValid()) return {};

    auto const left   = workArea.left() - screen.left();
    auto const top    = workArea.top() - screen.top();
    auto const right  = screen.right() - workArea.right();
    auto const bottom = screen.bottom() - workArea.bottom();
    // A fixed bottom taskbar is the only bar this trip can stand on: the other three edges leave no shelf to
    // walk along, and a work area inset on any of them as well is some other bar rather than the taskbar.
    if (left != 0 or top != 0 or right != 0 or bottom <= 0) return {};
    return QRect{screen.left(), workArea.bottom() + 1, screen.width(), bottom};
}

bool PetTaskbar::hasRoom(QPoint from, QSize size, QRect workArea)
{
    if (not workArea.isValid() or size.width() <= 0 or size.height() <= 0) return false;
    if (size.width() > workArea.width() or size.height() > workArea.height()) return false;

    auto const left    = workArea.left();
    auto const right   = workArea.right() - size.width() + 1;
    auto const highest = workArea.top();
    auto const floor   = workArea.bottom() - size.height() + 1;
    if (from.x() < left or from.x() > right or from.y() < highest or from.y() > floor) return false;

    // The roomier side must hold the leap and at least one window width of walking after it.
    auto const room = std::max(from.x() - left, right - from.x());
    return room - endMargin(size) >= size.width() * 2;
}

bool PetTaskbar::begin(QPoint from, QSize size, QRect workArea)
{
    stop();
    if (not hasRoom(from, size, workArea)) return false;

    from_     = from;
    position_ = from;
    floor_    = workArea.bottom() - size.height() + 1;
    top_      = workArea.top();

    auto const left      = workArea.left();
    auto const right     = workArea.right() - size.width() + 1;
    auto const leftRoom  = from.x() - left;
    auto const rightRoom = right - from.x();
    auto const room      = std::max(leftRoom, rightRoom);
    auto const margin    = endMargin(size);
    auto const toLeft    = leftRoom >= rightRoom;
    auto const maxLeap   = room - margin - size.width();
    auto const desired   = std::max(size.width(), workArea.width() * kLeapPercent / 100);
    auto const leap      = std::min(desired, maxLeap);

    landingX_ = toLeft ? from.x() - leap : from.x() + leap;
    targetX_  = toLeft ? left + margin : right - margin;

    auto const drop = std::max(0, floor_ - from.y());
    arc_            = arcHeight(size);
    bounce_         = bounceHeight(size);
    leap_           = leapTime(drop);
    walk_           = walkTime(std::abs(targetX_ - landingX_));

    // The climb back is a run of small hops; its sideways drift is spread across them at a slow pace.
    hopRise_         = hopRise(size);
    hops_            = drop > 0 ? (drop + hopRise_ - 1) / hopRise_ : 0;
    climbX_          = from.x() - landingX_;
    climbStarted_    = false;
    auto const hops  = std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(hops_) * kHopTime.count()};
    auto const drift = std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(
        std::llround(std::abs(climbX_) / kReturnPace * 1000.0))};
    climb_           = std::max(hops, drift);

    elapsed_ = 0ms;
    phase_   = Phase::Leaping;
    active_  = true;
    return true;
}

void PetTaskbar::stop()
{
    active_ = false;
}

PetTaskbar::Step PetTaskbar::tick(std::chrono::milliseconds step)
{
    Step answer;
    if (not active_) return answer;
    if (step <= 0ms) {
        answer.position = position_;
        return answer;
    }

    elapsed_ += step;
    auto const progressOf = [this](std::chrono::milliseconds duration) {
        return duration.count() > 0 ? std::clamp(static_cast<double>(elapsed_.count()) / static_cast<double>(duration.count()), 0.0, 1.0) : 1.0;
    };

    switch (phase_) {
        case Phase::Leaping: {
            // A projectile: the horizontal share is steady, the drop accelerates, and a small upward bump on the
            // way out makes it read as a leap rather than a straight fall.
            auto const t    = progressOf(leap_);
            auto const x    = static_cast<double>(from_.x()) + static_cast<double>(landingX_ - from_.x()) * t;
            auto const base = static_cast<double>(from_.y()) + static_cast<double>(floor_ - from_.y()) * t * t;
            auto const lift = static_cast<double>(arc_) * 4.0 * t * (1.0 - t);
            position_       = QPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(base - lift))};
            position_.setY(std::max(position_.y(), top_));
            if (t >= 1.0) {
                position_ = QPoint{landingX_, floor_};
                phase_    = Phase::Bouncing;
                elapsed_  = 0ms;
            }
            break;
        }

        case Phase::Bouncing: {
            auto const t = progressOf(kBounce);
            position_    = QPoint{landingX_, floor_ - static_cast<int>(std::lround(static_cast<double>(bounce_) * std::sin(kPi * t)))};
            position_.setY(std::max(position_.y(), top_));
            if (t >= 1.0) {
                position_ = QPoint{landingX_, floor_};
                phase_    = Phase::Paused;
                elapsed_  = 0ms;
            }
            break;
        }

        case Phase::Paused:
            position_ = QPoint{landingX_, floor_};
            if (elapsed_ >= pause_) {
                phase_           = Phase::WalkingOut;
                elapsed_         = 0ms;
                answer.startWalk = true;
            }
            break;

        case Phase::WalkingOut: {
            // A steady, unhurried glide: the window moves the same distance every tick, with no gait arc and no
            // easing, so the walk stays out of the reader's way.
            auto const progress = progressOf(walk_);
            auto const x        = static_cast<double>(landingX_) + static_cast<double>(targetX_ - landingX_) * progress;
            position_           = QPoint{static_cast<int>(std::lround(x)), floor_};
            if (progress >= 1.0) {
                position_       = QPoint{targetX_, floor_};
                phase_          = Phase::Turning;
                elapsed_        = 0ms;
                answer.stopWalk = true;
            }
            break;
        }

        case Phase::Turning:
            position_ = QPoint{targetX_, floor_};
            if (elapsed_ >= kTurnPause) {
                phase_           = Phase::WalkingBack;
                elapsed_         = 0ms;
                answer.startWalk = true;
            }
            break;

        case Phase::WalkingBack: {
            auto const progress = progressOf(walk_);
            auto const x        = static_cast<double>(targetX_) + static_cast<double>(landingX_ - targetX_) * progress;
            position_           = QPoint{static_cast<int>(std::lround(x)), floor_};
            if (progress >= 1.0) {
                position_       = QPoint{landingX_, floor_};
                phase_          = Phase::Rising;
                elapsed_        = 0ms;
                answer.stopWalk = true;
            }
            break;
        }

        case Phase::Rising: {
            // The climb back is a run of small hops, each with a little overshoot, played on the walk loop. The
            // sideways return to the start is spread evenly across the whole climb.
            auto const progress = progressOf(climb_);
            auto const x        = static_cast<double>(landingX_) + static_cast<double>(climbX_) * progress;

            if (not climbStarted_ and climb_ > 0ms) {
                climbStarted_    = true;
                answer.startWalk = true;
            }

            auto y = from_.y();
            if (hops_ > 0) {
                auto const at     = progress * static_cast<double>(hops_);
                auto const hop    = std::min(static_cast<int>(at), hops_ - 1);
                auto const local  = at - static_cast<double>(hop);
                auto const startY = floor_ - hop * hopRise_;
                auto const endY   = std::max(from_.y(), floor_ - (hop + 1) * hopRise_);
                auto const rise   = startY - endY;
                auto const arc    = std::max(6, static_cast<int>(std::lround(static_cast<double>(rise) * 0.55)));
                y                 = startY - static_cast<int>(std::lround(static_cast<double>(rise) * local + static_cast<double>(arc) * std::sin(kPi * local)));
            }

            position_ = QPoint{static_cast<int>(std::lround(x)), std::max(y, top_)};
            if (progress >= 1.0) {
                position_       = from_;
                active_         = false;
                answer.stopWalk = true;
                answer.finished = true;
            }
            break;
        }
    }

    answer.position = position_;
    return answer;
}

int PetTaskbar::endMargin(QSize size)
{
    return std::max(6, size.width() / 20);
}

int PetTaskbar::arcHeight(QSize size)
{
    return std::clamp(size.height() / 8, 12, 32);
}

int PetTaskbar::bounceHeight(QSize size)
{
    return std::clamp(size.height() / 6, 14, 40);
}

int PetTaskbar::hopRise(QSize size)
{
    return std::clamp(size.height() * 7 / 10, 44, 110);
}

std::chrono::milliseconds PetTaskbar::leapTime(int distance)
{
    if (distance <= 0) return kLeapMin;
    auto const seconds = std::sqrt(2.0 * static_cast<double>(distance) / kGravity);
    auto const travel  = std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(std::llround(seconds * 1000.0))};
    return std::clamp(travel, kLeapMin, kLeapMax);
}

std::chrono::milliseconds PetTaskbar::walkTime(int distance)
{
    if (distance <= 0) return 0ms;
    auto const travel = std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(std::llround(static_cast<double>(distance) / kWalkPace * 1000.0))};
    return std::clamp(travel, kWalkMin, kWalkMax);
}

} // namespace lens::app::pet
