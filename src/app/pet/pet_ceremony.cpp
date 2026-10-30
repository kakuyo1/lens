/**
 * @file pet_ceremony.cpp
 * @brief Implementation of PetCeremony: the entrance, the goodbye and the switch, phase by phase.
 */

#include "app/pet/pet_ceremony.h"

#include <algorithm>

#include "app/pet/pet_placement.h"

namespace lens::app::pet {

using namespace std::chrono_literals;

namespace {

/// @return The y a window of `size` keeps on `area`: its own row while it fits, the area's top otherwise.
int clampedY(QRect area, QSize size, int y)
{
    auto const high = area.bottom() - size.height() + 1;
    return high < area.top() ? area.top() : std::clamp(y, area.top(), high);
}

} // namespace

void PetCeremony::start(Stage const& stage)
{
    stage_    = stage;
    at_       = stage.position;
    elapsed_  = 0ms;
    duration_ = 0ms;
}

void PetCeremony::move(QPoint at)
{
    at_ = at;
}

PetCeremony::Step PetCeremony::beginEntrance(Stage const& stage, bool fromHere)
{
    stop();
    start(stage);
    Step answer;
    if (not stage.area.isValid() or stage.size.width() <= 0) {
        move(stage.home);
        answer.position = at_;
        answer.settled  = true;
        phase_          = Phase::Inactive;
        return answer;
    }

    home_           = clampedWithin(stage.home, stage.size, stage.area);
    side_           = PetWander::nearestEdge(stage.area, stage.size, home_);
    auto const from = fromHere ? stage.position : PetWander::offscreenBase(stage.area, stage.size, home_, side_);
    if (stage.canWalk) {
        auto const walk     = WanderStep{from, home_};
        auto const duration = PetWander::walkDuration(walk);
        if (duration.count() > 0) {
            walk_     = walk;
            phase_    = Phase::Entering;
            duration_ = duration;
            move(from);
            answer.position  = at_;
            answer.startWalk = true;
            return answer;
        }
    }

    move(home_);
    answer.position = at_;
    enterLook(answer);
    return answer;
}

PetCeremony::Step PetCeremony::beginGoodbye(Stage const& stage)
{
    stop();
    start(stage);
    Step answer;
    if (not stage.canWalk or not stage.area.isValid() or stage.size.width() <= 0 or
        not QRect{stage.position, stage.size}.intersects(stage.area)) {
        phase_        = Phase::Inactive;
        answer.hidden = true;
        return answer;
    }

    side_           = PetWander::nearestEdge(stage.area, stage.size, stage.position);
    auto const edge = QPoint{side_ == EdgeSide::Left ? stage.area.left() : stage.area.right() - stage.size.width() + 1,
                             clampedY(stage.area, stage.size, stage.position.y())};
    auto const walk     = WanderStep{stage.position, edge};
    auto const duration = PetWander::walkDuration(walk);
    if (duration.count() == 0) {
        move(edge);
        answer.position = at_;
        enterLeaveLook(answer);
        return answer;
    }

    walk_            = walk;
    phase_           = Phase::Leaving;
    duration_        = duration;
    answer.startWalk = true;
    return answer;
}

PetCeremony::Step PetCeremony::beginSwitch(Stage const& stage)
{
    stop();
    start(stage);
    Step answer;
    home_ = stage.home;
    if (not stage.canWalk or not stage.area.isValid() or stage.size.width() <= 0) {
        phase_              = Phase::Inactive;
        answer.applyPending = true;
        answer.settled      = true;
        return answer;
    }

    side_               = PetWander::nearestEdge(stage.area, stage.size, stage.position);
    auto const out      = PetWander::offscreenBase(stage.area, stage.size, stage.position, side_);
    auto const walk     = WanderStep{stage.position, out};
    auto const duration = PetWander::walkDuration(walk);
    if (duration.count() == 0) {
        move(out);
        answer.position = at_;
        phase_          = Phase::Swap;
        swapLoaded_     = false;
        return answer;
    }

    walk_            = walk;
    phase_           = Phase::SwitchOut;
    duration_        = duration;
    answer.startWalk = true;
    return answer;
}

PetCeremony::Step PetCeremony::tick(std::chrono::milliseconds step, Stage const& stage)
{
    Step answer;
    if (phase_ == Phase::Inactive) return answer;
    stage_ = stage;

    switch (phase_) {
        case Phase::Inactive:
            return answer;

        case Phase::Entering:
        case Phase::Leaving:
        case Phase::Exiting:
        case Phase::SwitchOut: {
            elapsed_ += step;
            auto const done   = duration_.count() == 0 or elapsed_ >= duration_;
            auto const amount = done ? 1.0
                                     : static_cast<double>(elapsed_.count()) / static_cast<double>(duration_.count());
            move(PetWander::walkAt(walk_, amount, stage.gait, stage.size.width()));
            answer.position = at_;
            if (not done) return answer;

            move(walk_.to);
            answer.position = at_;
            answer.stopWalk = true;
            switch (phase_) {
                case Phase::Entering:
                    enterLook(answer);
                    return answer;
                case Phase::Leaving:
                    enterLeaveLook(answer);
                    return answer;
                case Phase::Exiting:
                    phase_        = Phase::Inactive;
                    answer.hidden = true;
                    return answer;
                case Phase::SwitchOut:
                    phase_      = Phase::Swap;
                    elapsed_    = 0ms;
                    swapLoaded_ = false;
                    return answer;
                default:
                    return answer;
            }
        }

        case Phase::EnterLook:
            elapsed_ += step;
            if (elapsed_ >= duration_) {
                phase_         = Phase::Inactive;
                answer.settled = true;
            }
            return answer;

        case Phase::LeaveLook:
            elapsed_ += step;
            if (elapsed_ >= duration_) enterLeaveBye(answer);
            return answer;

        case Phase::LeaveBye:
            elapsed_ += step;
            if (elapsed_ >= duration_) beginExit(answer);
            return answer;

        case Phase::Swap: {
            elapsed_ += step;
            if (not swapLoaded_) {
                swapLoaded_         = true;
                elapsed_            = 0ms;
                answer.applyPending = true;
                return answer;
            }
            if (elapsed_ < kSwapSettle) return answer;

            // The window has taken its new size: clamp the reader's spot again and record it before the new pet walks in.
            home_       = stage.area.isValid() ? clampedWithin(stage.home, stage.size, stage.area) : stage.home;
            answer.home = home_;
            beginEntranceAfterSwap(answer);
            return answer;
        }
    }
    return answer;
}

PetCeremony::Step PetCeremony::walkFailed()
{
    Step answer;
    switch (phase_) {
        case Phase::Entering:
            move(home_);
            answer.position = at_;
            enterLook(answer);
            return answer;
        case Phase::Leaving:
            enterLeaveLook(answer);
            return answer;
        case Phase::Exiting:
            phase_        = Phase::Inactive;
            answer.hidden = true;
            return answer;
        case Phase::SwitchOut:
            phase_      = Phase::Swap;
            elapsed_    = 0ms;
            swapLoaded_ = false;
            return answer;
        default:
            return answer;
    }
}

PetCeremony::Step PetCeremony::roleFailed()
{
    Step answer;
    switch (phase_) {
        case Phase::EnterLook:
            phase_         = Phase::Inactive;
            answer.settled = true;
            return answer;
        case Phase::LeaveLook:
            enterLeaveBye(answer);
            return answer;
        case Phase::LeaveBye:
            beginExit(answer);
            return answer;
        default:
            return answer;
    }
}

void PetCeremony::stop()
{
    phase_ = Phase::Inactive;
}

bool PetCeremony::leaving() const
{
    return phase_ == Phase::Leaving or phase_ == Phase::LeaveLook or phase_ == Phase::LeaveBye or phase_ == Phase::Exiting;
}

void PetCeremony::enterLook(Step& answer)
{
    if (not stage_.roles.lookAround) {
        phase_         = Phase::Inactive;
        answer.settled = true;
        return;
    }
    phase_      = Phase::EnterLook;
    elapsed_    = 0ms;
    duration_   = *stage_.roles.lookAround + kSettlePause;
    answer.role = core::pet::Role::LookAround;
}

void PetCeremony::enterLeaveLook(Step& answer)
{
    if (not stage_.roles.lookAround) {
        enterLeaveBye(answer);
        return;
    }
    phase_      = Phase::LeaveLook;
    elapsed_    = 0ms;
    duration_   = *stage_.roles.lookAround + kLookToHeart;
    answer.role = core::pet::Role::LookAround;
}

void PetCeremony::enterLeaveBye(Step& answer)
{
    if (not stage_.roles.encourage) {
        beginExit(answer);
        return;
    }
    phase_      = Phase::LeaveBye;
    elapsed_    = 0ms;
    duration_   = *stage_.roles.encourage + kByeToExit;
    answer.role = core::pet::Role::Encourage;
}

void PetCeremony::beginExit(Step& answer)
{
    if (not stage_.area.isValid()) {
        phase_        = Phase::Inactive;
        answer.hidden = true;
        return;
    }
    auto const to       = PetWander::offscreenBase(stage_.area, stage_.size, at_, side_);
    auto const walk     = WanderStep{at_, to};
    auto const duration = PetWander::walkDuration(walk);
    if (duration.count() == 0) {
        move(to);
        answer.position = at_;
        phase_          = Phase::Inactive;
        answer.hidden   = true;
        return;
    }
    walk_            = walk;
    phase_           = Phase::Exiting;
    elapsed_         = 0ms;
    duration_        = duration;
    answer.startWalk = true;
}

void PetCeremony::beginEntranceAfterSwap(Step& answer)
{
    auto const from = stage_.area.isValid() ? PetWander::offscreenBase(stage_.area, stage_.size, home_, side_) : home_;
    move(from);
    answer.position = at_;
    if (stage_.canWalk) {
        auto const walk     = WanderStep{from, home_};
        auto const duration = PetWander::walkDuration(walk);
        if (duration.count() > 0) {
            walk_            = walk;
            phase_           = Phase::Entering;
            elapsed_         = 0ms;
            duration_        = duration;
            answer.startWalk = true;
            return;
        }
    }
    move(home_);
    answer.position = at_;
    enterLook(answer);
}

} // namespace lens::app::pet
