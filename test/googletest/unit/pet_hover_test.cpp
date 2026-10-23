/**
 * @file pet_hover_test.cpp
 * @brief HoverSense, offline: the rings, the two held moments, the exit margin and the cool-down.
 */

#include <chrono>

#include <gtest/gtest.h>

#include "core/domain/pet/hover_sense.h"

namespace {

using lens::core::pet::HoverSense;
using lens::core::pet::HoverTiming;
using lens::core::pet::HoverZone;
using namespace std::chrono_literals;

HoverSense quick(std::chrono::milliseconds cooldown = 1'000ms)
{
    return HoverSense{HoverTiming{100ms, 300ms, cooldown}};
}

/// Walks in and returns the update on which the greeting is due.
lens::core::pet::HoverUpdate greet(HoverSense& sense, double distance = 10.0)
{
    sense.update(0ms, 40.0, true);
    sense.update(50ms, 20.0, true);
    sense.update(50ms, distance, true);
    return sense.update(100ms, distance, true);
}

TEST(HoverSense, AQuickPassOnlyMakesTheEyesFollow)
{
    auto sense = quick();
    EXPECT_EQ(sense.update(0ms, 40.0, true).zone, HoverZone::Outside);
    EXPECT_EQ(sense.update(50ms, 20.0, true).zone, HoverZone::Gaze);
    EXPECT_EQ(sense.update(50ms, 10.0, true).zone, HoverZone::Near);
    EXPECT_TRUE(sense.watching());
    EXPECT_EQ(sense.update(50ms, 40.0, true).zone, HoverZone::Outside);
    EXPECT_FALSE(sense.greeted());
}

TEST(HoverSense, TheBehaviorMovesTheGazeRadius)
{
    // A wider reach sees the pointer as Gaze where the default ring has already let go (ADR 0094).
    auto wide = quick();
    wide.update(0ms, 40.0, true);
    EXPECT_EQ(wide.zone(), HoverZone::Outside);
    EXPECT_FALSE(wide.watching());

    HoverSense attentive{HoverTiming{100ms, 300ms, 1'000ms}, 40.0};
    attentive.update(0ms, 40.0, true);
    EXPECT_EQ(attentive.zone(), HoverZone::Gaze);
    EXPECT_TRUE(attentive.watching());
    // The near ring stays where it was: the gaze radius only moves the outer one.
    attentive.update(50ms, 10.0, true);
    EXPECT_EQ(attentive.zone(), HoverZone::Near);
}

TEST(HoverSense, AHoldInTheNearRingGreetsOnceThenEarnsOneReaction)
{
    auto       sense = quick();
    auto const hello = greet(sense);
    EXPECT_TRUE(hello.notice);
    EXPECT_FALSE(hello.dwell);
    EXPECT_TRUE(sense.greeted());

    EXPECT_FALSE(sense.update(100ms, 10.0, false).dwell);
    EXPECT_TRUE(sense.update(100ms, 10.0, true).dwell);
    EXPECT_TRUE(sense.finished());
    EXPECT_FALSE(sense.update(100ms, 10.0, true).dwell);
    EXPECT_FALSE(sense.update(100ms, 10.0, true).notice);
}

TEST(HoverSense, AGreetingWaitsForThePetToBeIdle)
{
    auto sense = quick();
    sense.update(0ms, 40.0, true);
    sense.update(50ms, 20.0, true);
    sense.update(50ms, 10.0, true);
    EXPECT_FALSE(sense.update(200ms, 10.0, false).notice);
    EXPECT_TRUE(sense.update(50ms, 10.0, true).notice);
}

TEST(HoverSense, LeavingAndReturningStartsAFreshApproach)
{
    auto sense = quick(0ms);
    greet(sense);
    sense.update(100ms, 40.0, true);
    EXPECT_EQ(sense.update(900ms, 40.0, true).zone, HoverZone::Outside);
    EXPECT_FALSE(sense.greeted());
    EXPECT_TRUE(greet(sense).notice);
}

TEST(HoverSense, TheRingsKeepTheirEdgeForASmallRetreat)
{
    auto sense = quick();
    sense.update(0ms, 40.0, true);
    EXPECT_EQ(sense.update(50ms, 10.0, true).zone, HoverZone::Near);
    EXPECT_EQ(sense.update(50ms, 18.0, true).zone, HoverZone::Near);
    EXPECT_EQ(sense.update(50ms, 22.0, true).zone, HoverZone::Gaze);
    EXPECT_EQ(sense.update(50ms, 31.0, true).zone, HoverZone::Outside);
}

TEST(HoverSense, CoolingDownTurnsASecondHelloIntoAQuietOne)
{
    auto sense = quick(2'000ms);
    greet(sense);
    sense.update(150ms, 40.0, true);
    EXPECT_EQ(sense.update(900ms, 40.0, true).zone, HoverZone::Outside);
    EXPECT_FALSE(greet(sense).notice);
    EXPECT_TRUE(sense.quiet());
    EXPECT_FALSE(sense.finished());

    sense.update(100ms, 40.0, true);
    sense.update(900ms, 40.0, true);
    EXPECT_TRUE(greet(sense).notice);
}

TEST(HoverSense, ResetForgetsTheApproachAndTheCooldown)
{
    auto sense = quick();
    greet(sense);
    sense.reset();
    EXPECT_EQ(sense.zone(), HoverZone::Outside);
    EXPECT_FALSE(sense.watching());
    EXPECT_FALSE(sense.greeted());
    EXPECT_FALSE(sense.finished());
    EXPECT_FALSE(sense.cooling());
    EXPECT_TRUE(greet(sense).notice);
}

} // namespace
