/**
 * @file pet_throw_mood_test.cpp
 * @brief The run of landed throws and its angry roll, offline (ADR 0092).
 */

#include <chrono>

#include <gtest/gtest.h>

#include "app/pet/pet_throw_mood.h"

namespace {

using lens::app::pet::PetThrowMood;
using namespace std::chrono_literals;

TEST(PetThrowMood, TheFirstThrowIsSilent)
{
    PetThrowMood mood;
    EXPECT_FALSE(mood.landed(0.0));
    EXPECT_EQ(mood.streak(), 1);
}

TEST(PetThrowMood, TheSecondThrowAngersOnASuccessfulRoll)
{
    PetThrowMood mood;
    EXPECT_FALSE(mood.landed(0.0));
    EXPECT_TRUE(mood.landed(0.5));
    EXPECT_EQ(mood.streak(), 0) << "the angry landing did not spend the run";
}

TEST(PetThrowMood, AFailedRollKeepsTheRunForTheThirdThrow)
{
    PetThrowMood mood;
    EXPECT_FALSE(mood.landed(0.0));
    EXPECT_FALSE(mood.landed(0.9));
    EXPECT_EQ(mood.streak(), 2);
    EXPECT_TRUE(mood.landed(0.5));
}

TEST(PetThrowMood, TheRunIsForgottenWhenTheWindowExpires)
{
    PetThrowMood mood;
    EXPECT_FALSE(mood.landed(0.0));
    mood.advance(PetThrowMood::kWindow - 10ms);
    EXPECT_EQ(mood.streak(), 1);
    mood.advance(20ms);
    EXPECT_EQ(mood.streak(), 0);

    // The next landing is the first of a new run, so even a perfect roll cannot anger.
    EXPECT_FALSE(mood.landed(0.0));
    EXPECT_EQ(mood.streak(), 1);
}

TEST(PetThrowMood, ResetForgetsTheRun)
{
    PetThrowMood mood;
    EXPECT_FALSE(mood.landed(0.0));
    mood.reset();
    EXPECT_EQ(mood.streak(), 0);
    EXPECT_FALSE(mood.landed(0.0));
}

} // namespace
