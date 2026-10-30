/**
 * @file pet_stroll_test.cpp
 * @brief One idle stroll, offline: the wait, the steps, the pauses, the edge hide and the pointer's hold
 *        (ADR 0076, 0095).
 */

#include <chrono>

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_stroll.h"
#include "app/pet/pet_wander.h"

namespace {

using lens::app::pet::EdgeSide;
using lens::app::pet::Gait;
using lens::app::pet::PetStroll;
using lens::app::pet::PetWander;
using lens::core::pet::BehaviorSpec;
using namespace std::chrono_literals;

QRect const  kArea{0, 0, 1920, 1040};
QSize const  kPet{200, 200};
QPoint const kAt{500, 500};

BehaviorSpec behaviorOf(std::chrono::milliseconds wait = 100ms, int steps = 1)
{
    auto behavior          = BehaviorSpec{};
    behavior.wanderWaitMin = wait;
    behavior.wanderWaitMax = wait;
    behavior.strollSteps   = steps;
    return behavior;
}

PetWander wanderAt(QPoint home, Gait gait = Gait::Glide, double roll = 0.5, QRect area = kArea)
{
    auto wander = PetWander{gait, 30, [roll] { return roll; }};
    wander.setHome(home);
    wander.setBounds(kPet, area);
    return wander;
}

/// @brief A stroll whose dice always answer `value`, so the wait, the step count, the pause and the hide roll are known.
PetStroll strollOf(double value, std::chrono::milliseconds walkCycle = 500ms, bool canHide = false, Gait gait = Gait::Glide, int steps = 1)
{
    auto stroll = PetStroll{[value] { return value; }};
    stroll.load(behaviorOf(100ms, steps), gait, walkCycle, canHide);
    return stroll;
}

/// @brief Runs the wait down and starts the first step.
void start(PetStroll& stroll, PetWander& wander, QPoint window = kAt)
{
    stroll.tick(100ms, {}, wander, window);
    stroll.tick(20ms, {}, wander, window);
}

TEST(PetStroll, TheWaitCountsDownOnlyWhileTicked)
{
    auto stroll = strollOf(0.0);
    auto wander = wanderAt(kAt);
    EXPECT_EQ(stroll.waitLeft(), 100ms);
    EXPECT_FALSE(stroll.walking());

    EXPECT_FALSE(stroll.tick(100ms, {}, wander, kAt).startWalk) << "the wait reached zero without starting the step";
    auto const start = stroll.tick(20ms, {}, wander, kAt);
    EXPECT_TRUE(start.startWalk);
    EXPECT_TRUE(stroll.walking());
}

TEST(PetStroll, AStrollWalksItsStepThenPausesBeforeFinishing)
{
    auto stroll = strollOf(0.0);
    auto wander = wanderAt(kAt);
    start(stroll, wander);

    auto       expected = wanderAt(kAt);
    auto const step     = expected.nextStep(kAt);
    ASSERT_TRUE(step);

    auto const half = stroll.tick(250ms, {}, wander, kAt);
    ASSERT_TRUE(half.position);
    EXPECT_EQ(*half.position, wander.positionAt(*step, 0.5));

    auto const end = stroll.tick(250ms, {}, wander, kAt);
    ASSERT_TRUE(end.position);
    EXPECT_EQ(*end.position, step->to);
    EXPECT_TRUE(end.stopWalk);
    EXPECT_FALSE(end.finished);
    EXPECT_FALSE(stroll.walking());

    auto const paused = stroll.tick(300ms, {}, wander, kAt);
    EXPECT_TRUE(paused.finished);
    EXPECT_EQ(stroll.waitLeft(), 100ms) << "the stroll did not draw the next wait";
}

TEST(PetStroll, APounceTakesExactlyOneStep)
{
    auto stroll = strollOf(0.9, 500ms, false, Gait::Pounce, 5);
    auto wander = wanderAt(kAt, Gait::Pounce, 0.5);
    start(stroll, wander);

    stroll.tick(500ms, {}, wander, kAt);
    auto const paused = stroll.tick(1140ms, {}, wander, kAt);
    EXPECT_TRUE(paused.finished) << "a pounce drew more than its one leap";
}

TEST(PetStroll, AStepThatEndsOnAnEdgeCanHide)
{
    auto stroll = strollOf(0.0, 500ms, true);
    auto wander = wanderAt(QPoint{30, 300}, Gait::Glide, 0.5, QRect{0, 0, 800, 600});
    start(stroll, wander, QPoint{30, 300});

    auto const end = stroll.tick(500ms, {}, wander, QPoint{30, 300});
    ASSERT_TRUE(end.hide);
    EXPECT_EQ(*end.hide, EdgeSide::Left);
    EXPECT_TRUE(end.stopWalk);
    EXPECT_FALSE(stroll.walking());
}

TEST(PetStroll, APetWithoutPeekArtNeverHides)
{
    auto stroll = strollOf(0.0, 500ms, false);
    auto wander = wanderAt(QPoint{30, 300}, Gait::Glide, 0.5, QRect{0, 0, 800, 600});
    start(stroll, wander, QPoint{30, 300});

    auto const end = stroll.tick(500ms, {}, wander, QPoint{30, 300});
    EXPECT_FALSE(end.hide);
    EXPECT_TRUE(end.stopWalk);
}

TEST(PetStroll, ThePointerOnThePetEndsTheStepAndHoldsOneWait)
{
    auto stroll = strollOf(0.0);
    auto wander = wanderAt(kAt);
    start(stroll, wander);
    EXPECT_TRUE(stroll.tick(20ms, {}, wander, kAt).position);

    auto const stopped = stroll.tick(20ms, PetStroll::Facts{true}, wander, kAt);
    EXPECT_TRUE(stopped.stopWalk);
    EXPECT_FALSE(stroll.walking());

    auto const held = stroll.waitLeft();
    EXPECT_EQ(held, 100ms);
    stroll.tick(50ms, PetStroll::Facts{true}, wander, kAt);
    EXPECT_EQ(stroll.waitLeft(), held) << "the pointer redrew a wait it already holds";

    stroll.tick(20ms, {}, wander, kAt);
    EXPECT_EQ(stroll.waitLeft(), held - 20ms);
}

TEST(PetStroll, StopKeepsTheWaitAndWaitDrawsANewOne)
{
    auto stroll = strollOf(0.0);
    auto wander = wanderAt(kAt);
    stroll.tick(100ms, {}, wander, kAt);
    EXPECT_EQ(stroll.waitLeft(), 0ms);

    stroll.stop();
    stroll.wait();
    EXPECT_EQ(stroll.waitLeft(), 100ms);

    stroll.tick(20ms, {}, wander, kAt);
    EXPECT_EQ(stroll.waitLeft(), 80ms);
}

TEST(PetStroll, ARefusedWalkEndsTheStroll)
{
    auto stroll = strollOf(0.0);
    auto wander = wanderAt(kAt);
    start(stroll, wander);

    auto const failed = stroll.walkFailed();
    EXPECT_TRUE(failed.finished);
    EXPECT_FALSE(stroll.walking());
    EXPECT_EQ(stroll.waitLeft(), 100ms);
}

} // namespace
