/**
 * @file pet_throw.cpp
 * @brief Implementation of PetThrow: the landing, the run it builds and the angry repeat.
 */

#include "app/pet/pet_throw.h"

#include <utility>

namespace lens::app::pet {

using core::pet::Role;

PetThrow::PetThrow(core::pet::RandomSource roll)
    : roll_{std::move(roll)}
{}

bool PetThrow::begin(QPoint at, QPointF velocity, QSize size, QRect area)
{
    if (not PetFlight::isFling(velocity)) return false;
    flight_          = PetFlight{at, velocity, size, area};
    repeatsLeft_     = 0;
    awaitingLanding_ = false;
    angryLanding_    = false;
    return true;
}

void PetThrow::stop(ThrowStop why)
{
    flight_.reset();
    repeatsLeft_     = 0;
    awaitingLanding_ = false;
    angryLanding_    = false;
    // Only what the reader does without leaving the pet alone keeps the run: a pickup off the floor and an outside
    // learning event. Every other interruption starts it over (ADR 0092).
    if (why == ThrowStop::Held or why == ThrowStop::Learning) return;
    mood_.reset();
}

ThrowStep PetThrow::tick(std::chrono::milliseconds step, bool idle)
{
    mood_.advance(step);
    if (flight_) {
        auto const next = flight_->advance(step);
        if (not next.landed) return {.position = next.position};

        auto const elapsed = flight_->elapsed();
        flight_.reset();
        // The roll is drawn even when the landing has no dizzy art: the attempt spends the run either way.
        awaitingLanding_ = true;
        angryLanding_    = mood_.landed(roll_());
        return {.position = next.position, .role = Role::Dizzy, .landed = true, .elapsed = elapsed};
    }
    if (repeatsLeft_ > 0 and idle) {
        --repeatsLeft_;
        return {.role = Role::Angry};
    }
    return {};
}

void PetThrow::landingReacted(bool dizzy)
{
    if (not awaitingLanding_) return;
    awaitingLanding_ = false;
    if (dizzy and angryLanding_) repeatsLeft_ = kAngryRepeats;
    angryLanding_ = false;
}

void PetThrow::dropRepeats()
{
    repeatsLeft_ = 0;
}

} // namespace lens::app::pet
