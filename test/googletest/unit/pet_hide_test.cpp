/**
 * @file pet_hide_test.cpp
 * @brief One edge hide, offline: the slide, the peek's lean, the ask pause, the call-out and the drag back
 *        (ADR 0076, 0095).
 */

#include <chrono>

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include "app/pet/pet_hide.h"
#include "app/pet/pet_wander.h"

namespace {

using lens::app::pet::EdgeSide;
using lens::app::pet::PetHide;
using lens::app::pet::PetWander;
using namespace std::chrono_literals;

QRect const      kArea{0, 0, 1200, 800};
QSize const      kPet{200, 200};
QPoint const     kFrom{300, 200};
constexpr double kInkLeft{20.0};
constexpr double kInkRight{180.0};

PetHide makeHide(EdgeSide side = EdgeSide::Left, QString const& screen = QStringLiteral("screen"))
{
    auto hide = PetHide{};
    hide.begin(side, kFrom, kPet, kArea, screen, kInkLeft, kInkRight);
    return hide;
}

QPoint baseOf(EdgeSide side = EdgeSide::Left, QPoint from = kFrom, QSize size = kPet)
{
    return PetWander::hiddenBase(kArea, size, side, from, kInkLeft, kInkRight);
}

/// @brief Runs the slide out to its end.
void settle(PetHide& hide)
{
    hide.tick(PetWander::kHideSlide, true, false);
}

TEST(PetHide, TheSlideEndsOnTheHiddenBase)
{
    auto hide = makeHide();
    EXPECT_FALSE(hide.peeking());

    auto const half = hide.tick(PetWander::kHideSlide / 2, true, false);
    ASSERT_TRUE(half.position);
    EXPECT_EQ(*half.position, PetWander::slideAt(kFrom, baseOf(), PetWander::kHideSlide / 2));
    EXPECT_TRUE(half.loopPeek);
    EXPECT_FALSE(half.left);

    auto const end = hide.tick(PetWander::kHideSlide, true, false);
    ASSERT_TRUE(end.position);
    EXPECT_EQ(*end.position, baseOf());
    EXPECT_TRUE(hide.peeking());
}

TEST(PetHide, ThePeekLeansInAndBackOnItsCycle)
{
    auto hide = makeHide();
    settle(hide);
    auto const base = baseOf();

    auto move = hide.tick(PetWander::kPeekCycle / 4, true, false);
    ASSERT_TRUE(move.position);
    EXPECT_EQ(move.position->x(), base.x() + PetWander::peekOffset(PetWander::kPeekCycle / 4));
    move = hide.tick(PetWander::kPeekCycle / 4, true, false);
    EXPECT_EQ(move.position->x(), base.x() + PetWander::peekOffset(PetWander::kPeekCycle / 2));
    move = hide.tick(PetWander::kPeekCycle / 2, true, false);
    EXPECT_EQ(move.position->x(), base.x()) << "the cycle did not close";
}

TEST(PetHide, TheRightEdgeMirrorsTheLeft)
{
    auto hide = makeHide(EdgeSide::Right);
    settle(hide);
    auto const base = baseOf(EdgeSide::Right);

    auto const move = hide.tick(PetWander::kPeekCycle / 4, true, false);
    ASSERT_TRUE(move.position);
    EXPECT_EQ(move.position->x(), base.x() - PetWander::peekOffset(PetWander::kPeekCycle / 4));
    EXPECT_EQ(move.position->y(), base.y());
}

TEST(PetHide, APointerNearTheSliverLeansFurtherAndWithdraws)
{
    auto near = makeHide();
    auto away = makeHide();
    settle(near);
    settle(away);

    auto const leaning = near.tick(PetWander::kHoverLeanTime, true, true);
    auto const still   = away.tick(PetWander::kHoverLeanTime, true, false);
    ASSERT_TRUE(leaning.position);
    ASSERT_TRUE(still.position);
    EXPECT_EQ(leaning.position->x() - still.position->x(), PetWander::kHoverLean)
        << "the pointer's lean did not come in";

    auto const back   = near.tick(PetWander::kHoverLeanTime, true, false);
    auto const stayed = away.tick(PetWander::kHoverLeanTime, true, false);
    ASSERT_TRUE(back.position);
    ASSERT_TRUE(stayed.position);
    EXPECT_EQ(back.position->x(), stayed.position->x()) << "the lean did not withdraw";
}

TEST(PetHide, AQuestionPausesThePeekAtTheHiddenBase)
{
    auto hide = makeHide();
    settle(hide);
    hide.tick(PetWander::kPeekCycle / 4, true, false);

    auto const answer = hide.ask();
    EXPECT_EQ(answer.position, baseOf());
    EXPECT_FALSE(answer.left);
    EXPECT_FALSE(hide.peeking());

    auto const paused = hide.tick(50ms, false, false);
    ASSERT_TRUE(paused.position);
    EXPECT_EQ(*paused.position, baseOf());
    EXPECT_FALSE(paused.loopPeek);

    auto const resumed = hide.tick(50ms, true, false);
    ASSERT_TRUE(resumed.position);
    EXPECT_TRUE(resumed.loopPeek);
    EXPECT_TRUE(hide.peeking());
    EXPECT_EQ(resumed.position->x(), baseOf().x() + PetWander::peekOffset(50ms))
        << "the peek did not restart from its beginning";
}

TEST(PetHide, ACallOutEndsAtTheHideStart)
{
    auto hide = makeHide();
    settle(hide);
    auto const at = *hide.tick(10ms, true, false).position;

    hide.lureOut(at, kPet);
    auto const midway = hide.tick(PetWander::kHideSlide / 2, true, false);
    ASSERT_TRUE(midway.position);
    EXPECT_EQ(*midway.position, PetWander::slideAt(at, kFrom, PetWander::kHideSlide / 2));
    EXPECT_FALSE(midway.left);
    EXPECT_FALSE(midway.loopPeek);

    auto const done = hide.tick(PetWander::kHideSlide, true, false);
    EXPECT_TRUE(done.left);
    ASSERT_TRUE(done.position);
    EXPECT_EQ(*done.position, kFrom);
}

TEST(PetHide, AQuestionDuringTheCallOutFinishesIt)
{
    auto hide = makeHide();
    settle(hide);
    hide.lureOut(baseOf(), kPet);
    hide.tick(20ms, true, false);

    auto const answer = hide.ask();
    EXPECT_TRUE(answer.left);
    EXPECT_EQ(answer.position, kFrom);
}

TEST(PetHide, PullingThePetBackInsideEndsTheHide)
{
    auto hide = makeHide();
    settle(hide);
    hide.dragStart();

    auto const drop = hide.drop(QPoint{100, 200}, kPet);
    EXPECT_TRUE(drop.pulledOut);
    EXPECT_FALSE(hide.active());
    EXPECT_FALSE(hide.tick(20ms, true, false).position);
}

TEST(PetHide, AShortDragSnapsBackToTheHiddenBase)
{
    auto hide = makeHide();
    settle(hide);
    hide.tick(PetWander::kHoverLeanTime, true, true); // build the lean
    hide.dragStart();

    auto const drop = hide.drop(baseOf(), kPet);
    EXPECT_FALSE(drop.pulledOut);
    EXPECT_EQ(drop.snapBackTo, baseOf());
    EXPECT_TRUE(hide.active());
    EXPECT_TRUE(hide.peeking());

    auto const after = hide.tick(10ms, true, false);
    ASSERT_TRUE(after.position);
    EXPECT_EQ(after.position->x(), baseOf().x() + PetWander::peekOffset(10ms))
        << "the lean survived the snap back";
}

TEST(PetHide, AResizeRecomputesTheBaseFromTheNewSize)
{
    auto hide = makeHide();
    settle(hide);

    QSize const bigger{300, 240};
    auto const  base = hide.refit(kArea, bigger, 30.0, 270.0);
    EXPECT_EQ(base, PetWander::hiddenBase(kArea, bigger, EdgeSide::Left, baseOf(), 30.0, 270.0));
    EXPECT_TRUE(hide.peeking());
}

TEST(PetHide, AMoveToAnotherScreenRehidesOnItsNewArea)
{
    auto hide = makeHide();
    settle(hide);

    QRect const other{1300, 40, 1000, 700};
    auto const  base = hide.refit(other, kPet, kInkLeft, kInkRight);
    EXPECT_EQ(hide.area(), other);
    EXPECT_EQ(base, PetWander::hiddenBase(other, kPet, EdgeSide::Left, baseOf(), kInkLeft, kInkRight));
}

TEST(PetHide, TheHideRemembersItsScreenAndForgetsItOnStop)
{
    auto hide = makeHide(EdgeSide::Left, QStringLiteral("DP-2"));
    EXPECT_EQ(hide.screen(), QStringLiteral("DP-2"));

    hide.stop();
    EXPECT_FALSE(hide.active());
    auto const after = hide.tick(20ms, true, true);
    EXPECT_FALSE(after.position);
    EXPECT_FALSE(after.loopPeek);
    EXPECT_FALSE(after.left);
}

TEST(PetHide, ThePeekLoopWaitsForTheStateMachine)
{
    auto hide = makeHide();
    settle(hide);
    EXPECT_FALSE(hide.tick(20ms, false, false).loopPeek);
    EXPECT_TRUE(hide.tick(20ms, true, false).loopPeek);

    hide.dragStart();
    auto const dragged = hide.tick(20ms, true, false);
    EXPECT_FALSE(dragged.position) << "the hide moved the window while the reader held it";
    EXPECT_FALSE(dragged.loopPeek);
    hide.drop(baseOf(), kPet);

    hide.lureOut(baseOf(), kPet);
    EXPECT_FALSE(hide.tick(20ms, true, false).loopPeek) << "the peek played while the pet was being called out";
}

} // namespace
