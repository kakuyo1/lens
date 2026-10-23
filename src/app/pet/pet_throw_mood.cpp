/**
 * @file pet_throw_mood.cpp
 * @brief Implementation of PetThrowMood: the landed-throw streak and its angry roll.
 */

#include "app/pet/pet_throw_mood.h"

namespace lens::app::pet {

using namespace std::chrono_literals;

bool PetThrowMood::landed(double roll)
{
    ++streak_;
    windowLeft_ = kWindow;
    if (streak_ < kThrowsBeforeAngry or roll >= kAngryChance) return false;

    // The angry landing spends the run: the next throw starts a new one.
    streak_     = 0;
    windowLeft_ = 0ms;
    return true;
}

void PetThrowMood::advance(std::chrono::milliseconds step)
{
    if (windowLeft_ <= 0ms) return;
    windowLeft_ -= step;
    if (windowLeft_ > 0ms) return;

    windowLeft_ = 0ms;
    streak_     = 0;
}

void PetThrowMood::reset()
{
    streak_     = 0;
    windowLeft_ = 0ms;
}

} // namespace lens::app::pet
