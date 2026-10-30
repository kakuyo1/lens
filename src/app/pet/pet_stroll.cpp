/**
 * @file pet_stroll.cpp
 * @brief Implementation of PetStroll: the wait, the walk cycle, the pause and the hide roll.
 */

#include "app/pet/pet_stroll.h"

#include <algorithm>
#include <utility>

namespace lens::app::pet {

using namespace std::chrono_literals;

PetStroll::PetStroll(core::pet::RandomSource roll)
    : roll_(std::move(roll))
{}

void PetStroll::load(core::pet::BehaviorSpec behavior, Gait gait, std::chrono::milliseconds walkCycle, bool canHide)
{
    behavior_  = behavior;
    gait_      = gait;
    walkCycle_ = walkCycle;
    canHide_   = canHide;
    phase_     = Phase::Waiting;
    step_.reset();
    elapsed_   = 0ms;
    pauseLeft_ = 0ms;
    stepsLeft_ = 0;
    hovering_  = false;
    drawWait();
}

void PetStroll::wait()
{
    drawWait();
}

void PetStroll::stop()
{
    phase_     = Phase::Waiting;
    stepsLeft_ = 0;
    step_.reset();
}

void PetStroll::hover()
{
    stop();
    drawWait();
    hovering_ = true;
}

PetStroll::Step PetStroll::tick(std::chrono::milliseconds step, Facts const& facts, PetWander& wander, QPoint window)
{
    Step answer;

    if (facts.pointerOnPet) {
        if (phase_ != Phase::Waiting) {
            phase_     = Phase::Waiting;
            stepsLeft_ = 0;
            step_.reset();
            answer.stopWalk = true;
        }
        if (not hovering_) {
            hovering_ = true;
            drawWait();
        }
        return answer;
    }
    hovering_ = false;

    switch (phase_) {
        case Phase::Waiting:
            if (wait_ <= 0ms) {
                stepsLeft_ = gait_ == Gait::Pounce ? 1 : 1 + static_cast<int>(roll_() * behavior_.strollSteps);
                return beginStep(wander, window);
            }
            wait_ -= step;
            return answer;

        case Phase::Walking: {
            elapsed_ += step;
            auto const progress = walkCycle_.count() > 0
                                      ? std::min(1.0, static_cast<double>(elapsed_.count()) / static_cast<double>(walkCycle_.count()))
                                      : 1.0;
            answer.position     = wander.positionAt(*step_, progress);
            if (progress < 1.0) return answer;

            answer.position = step_->to;
            if (canHide_) {
                if (auto const side = wander.edgeAt(step_->to); side and wander.hides(roll_())) {
                    phase_     = Phase::Waiting;
                    stepsLeft_ = 0;
                    step_.reset();
                    answer.stopWalk = true;
                    answer.hide     = *side;
                    return answer;
                }
            }
            phase_          = Phase::Pausing;
            pauseLeft_      = nextPause();
            answer.stopWalk = true;
            return answer;
        }

        case Phase::Pausing:
            pauseLeft_ -= step;
            if (pauseLeft_ > 0ms) return answer;
            --stepsLeft_;
            if (stepsLeft_ > 0) return beginStep(wander, step_->to);
            return finish();
    }
    return answer;
}

PetStroll::Step PetStroll::walkFailed()
{
    return finish();
}

void PetStroll::drawWait()
{
    auto const spread = static_cast<double>((behavior_.wanderWaitMax - behavior_.wanderWaitMin).count());
    wait_             = behavior_.wanderWaitMin +
            std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(roll_() * spread)};
}

PetStroll::Step PetStroll::beginStep(PetWander& wander, QPoint window)
{
    auto const next = wander.nextStep(window);
    if (not next) return finish();
    step_    = *next;
    elapsed_ = 0ms;
    phase_   = Phase::Walking;
    Step answer;
    answer.startWalk = true;
    return answer;
}

PetStroll::Step PetStroll::finish()
{
    phase_     = Phase::Waiting;
    stepsLeft_ = 0;
    step_.reset();
    hovering_ = false;
    drawWait();
    Step answer;
    answer.finished = true;
    return answer;
}

std::chrono::milliseconds PetStroll::nextPause() const
{
    auto const low  = gait_ == Gait::Pounce ? 600 : gait_ == Gait::Float ? 400
                                                : gait_ == Gait::Glide   ? 300
                                                                         : 220;
    auto const high = gait_ == Gait::Pounce ? 1200 : gait_ == Gait::Float ? 900
                                                 : gait_ == Gait::Glide   ? 800
                                                                          : 620;
    return std::chrono::milliseconds{low + static_cast<int>(roll_() * (high - low))};
}

} // namespace lens::app::pet
