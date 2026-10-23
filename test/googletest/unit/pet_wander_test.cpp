/**
 * @file pet_wander_test.cpp
 * @brief The idle stroll's geometry, offline: the home ellipse, the five gait paths, edge detection and hiding
 *        (docs/adr/0076).
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_wander.h"

namespace {

using lens::app::pet::EdgeSide;
using lens::app::pet::Gait;
using lens::app::pet::PetWander;
using lens::app::pet::WanderStep;
using namespace std::chrono_literals;

QRect const kArea{0, 0, 1920, 1040};
QSize const kPet{264, 264};

PetWander makeWander(Gait gait, QPoint home = QPoint{500, 500}, int range = 30, double random = 0.5, double hideChance = lens::core::pet::kHideChance)
{
    PetWander wander{gait, range, [random] { return random; }, hideChance};
    wander.setHome(home);
    wander.setBounds(kPet, kArea);
    return wander;
}

TEST(PetWander, EveryGaitTakesAStep)
{
    for (auto const gait : {Gait::Glide, Gait::Float, Gait::Hop, Gait::Trot, Gait::Pounce}) {
        auto const step = makeWander(gait).nextStep(QPoint{500, 500});
        ASSERT_TRUE(step.has_value());
        EXPECT_GT(std::hypot(step->to.x() - step->from.x(), step->to.y() - step->from.y()), 10.0);
    }
}

TEST(PetWander, TheStepNeverLeavesTheHomeEllipseOrTheWorkArea)
{
    // A source that keeps moving instead of settling on one direction.
    auto const values = std::vector<double>{0.05, 0.9, 0.25, 0.6, 0.75, 0.12, 0.45, 0.98, 0.33, 0.7};
    auto       at     = std::size_t{0};
    PetWander  wander{Gait::Trot, 30, [&values, &at] { return values[at++ % values.size()]; }};
    wander.setHome(QPoint{500, 500});
    wander.setBounds(kPet, kArea);

    auto point = QPoint{500, 500};
    for (auto i = 0; i < 100; ++i) {
        auto const step = wander.nextStep(point);
        ASSERT_TRUE(step.has_value());
        point = step->to;

        auto const radius   = kPet.width() * 30 / 100.0;
        auto const vertical = radius * 0.10;
        auto const dx       = (point.x() - 500) / radius;
        auto const dy       = (point.y() - 500) / vertical;
        // The window moves in whole pixels, so a point pulled onto the ellipse may land up to a pixel outside it.
        EXPECT_LE((std::hypot(dx, dy) - 1.0) * vertical, 1.5) << point.x() << "," << point.y();
        EXPECT_TRUE(kArea.contains(point));
    }
}

TEST(PetWander, FloatRisesOnTheQuarterAndTheOthersArcUp)
{
    auto const step = WanderStep{QPoint{250, 300}, QPoint{350, 300}};

    auto const floatWander = makeWander(Gait::Float, QPoint{300, 300}, 60);
    EXPECT_EQ(floatWander.positionAt(step, 0.25).y(), 305) << "the float's sine is at its top quarter of the way through";
    EXPECT_EQ(floatWander.positionAt(step, 0.75).y(), 295);

    EXPECT_EQ(makeWander(Gait::Hop, QPoint{300, 300}, 60).positionAt(step, 0.5).y(), 297);
    EXPECT_EQ(makeWander(Gait::Trot, QPoint{300, 300}, 60).positionAt(step, 0.5).y(), 298);
    EXPECT_EQ(makeWander(Gait::Pounce, QPoint{300, 300}, 60).positionAt(step, 0.5).y(), 296);
    EXPECT_EQ(makeWander(Gait::Glide, QPoint{300, 300}, 60).positionAt(step, 0.5).y(), 300);
}

TEST(PetWander, HopAndPounceReachTheirTargetEarlyWhileGlideIsEven)
{
    auto const step = WanderStep{QPoint{250, 300}, QPoint{350, 300}};

    EXPECT_EQ(makeWander(Gait::Pounce, QPoint{300, 300}, 60).positionAt(step, 0.5).x(), 338);
    EXPECT_EQ(makeWander(Gait::Glide, QPoint{300, 300}, 60).positionAt(step, 0.5).x(), 300);
    EXPECT_EQ(makeWander(Gait::Glide, QPoint{300, 300}, 60).positionAt(step, 0.25).x(), 263);
}

TEST(PetWander, EveryPointOnAPathStaysInsideTheWorkArea)
{
    // The float arc at the top edge would take the window above the screen; the clamp keeps it in.
    auto const wander = makeWander(Gait::Float, QPoint{500, 0}, 30);
    auto const step   = WanderStep{QPoint{500, 0}, QPoint{600, 0}};
    for (auto i = 0; i <= 20; ++i) {
        auto const point = wander.positionAt(step, i / 20.0);
        EXPECT_TRUE(kArea.contains(point)) << i << ": " << point.x() << "," << point.y();
    }
}

TEST(PetWander, TheCeremonyPathKeepsTheWalkButDropsTheHomeEllipse)
{
    // A stroll of this length would be pulled back into the home ellipse; the entrance and exit walks are straight
    // crossings, so the same points are not clamped.
    auto const step = WanderStep{QPoint{0, 400}, QPoint{1000, 400}};

    EXPECT_EQ(PetWander::walkAt(step, 0.0, Gait::Glide, kPet.width()), step.from);
    EXPECT_EQ(PetWander::walkAt(step, 0.25, Gait::Glide, kPet.width()), (QPoint{125, 400}));
    EXPECT_EQ(PetWander::walkAt(step, 0.5, Gait::Glide, kPet.width()), (QPoint{500, 400}));
    EXPECT_EQ(PetWander::walkAt(step, 1.0, Gait::Glide, kPet.width()), step.to);

    // Each gait's arc still belongs to the walk: the hop reaches its target early and lifts on the way.
    EXPECT_EQ(PetWander::walkAt(step, 0.5, Gait::Hop, kPet.width()), (QPoint{875, 397}));
}

TEST(PetWander, ACeremonyStartsOneWholeWindowPastTheEdge)
{
    auto const left = PetWander::offscreenBase(kArea, kPet, QPoint{120, 300}, EdgeSide::Left);
    EXPECT_EQ(left, (QPoint{kArea.left() - kPet.width(), 300}));

    auto const right = PetWander::offscreenBase(kArea, kPet, QPoint{120, 300}, EdgeSide::Right);
    EXPECT_EQ(right, (QPoint{kArea.right() + 1, 300}));

    // The release row obeys the same work-area clamp as a hide.
    EXPECT_EQ(PetWander::offscreenBase(kArea, kPet, QPoint{120, 2000}, EdgeSide::Left).y(), 776);
}

TEST(PetWander, TheNearerHorizontalEdgeIsTheOneTheCeremonyUses)
{
    EXPECT_EQ(PetWander::nearestEdge(kArea, kPet, QPoint{0, 400}), EdgeSide::Left);
    EXPECT_EQ(PetWander::nearestEdge(kArea, kPet, QPoint{1656, 400}), EdgeSide::Right);
    EXPECT_EQ(PetWander::nearestEdge(kArea, kPet, QPoint{700, 400}), EdgeSide::Left);
    EXPECT_EQ(PetWander::nearestEdge(kArea, kPet, QPoint{1000, 400}), EdgeSide::Right);
}

TEST(PetWander, AWalkKeepsAPaceAndStaysInsideItsBand)
{
    EXPECT_EQ(PetWander::walkDuration(WanderStep{QPoint{0, 0}, QPoint{1, 0}}), 0ms) << "nowhere to walk";
    EXPECT_EQ(PetWander::walkDuration(WanderStep{QPoint{0, 0}, QPoint{100, 0}}), PetWander::kWalkMin);
    EXPECT_EQ(PetWander::walkDuration(WanderStep{QPoint{0, 0}, QPoint{600, 0}}), 2000ms);
    EXPECT_EQ(PetWander::walkDuration(WanderStep{QPoint{0, 0}, QPoint{2000, 0}}), PetWander::kWalkMax);
}

TEST(PetWander, AStepClampsToTheWorkAreaAndEndsOnTheEdge)
{
    auto       wander = makeWander(Gait::Trot, QPoint{0, 500}, 60);
    auto const step   = wander.nextStep(QPoint{18, 500});
    ASSERT_TRUE(step.has_value());
    EXPECT_EQ(step->to.x(), kArea.left());

    auto side = std::optional<EdgeSide>{};
    side      = wander.edgeAt(step->to);
    ASSERT_TRUE(side.has_value());
    EXPECT_EQ(*side, EdgeSide::Left);
}

TEST(PetWander, EdgeDetectionUsesAOnePixelTolerance)
{
    auto const wander = makeWander(Gait::Trot, QPoint{500, 500});

    EXPECT_EQ(wander.edgeAt(QPoint{0, 100}), EdgeSide::Left);
    EXPECT_EQ(wander.edgeAt(QPoint{1, 100}), EdgeSide::Left);
    EXPECT_FALSE(wander.edgeAt(QPoint{2, 100}).has_value());

    // The rightmost top-left that keeps the window whole is 1920 - 264.
    EXPECT_EQ(wander.edgeAt(QPoint{1656, 100}), EdgeSide::Right);
    EXPECT_EQ(wander.edgeAt(QPoint{1655, 100}), EdgeSide::Right);
    EXPECT_FALSE(wander.edgeAt(QPoint{1654, 100}).has_value());
    EXPECT_FALSE(wander.edgeAt(QPoint{900, 100}).has_value());
}

TEST(PetWander, SevenStepsInTenBecomeAHide)
{
    auto const wander = makeWander(Gait::Trot);
    EXPECT_TRUE(wander.hides(0.0));
    EXPECT_TRUE(wander.hides(0.69));
    EXPECT_FALSE(wander.hides(0.7));
    EXPECT_FALSE(wander.hides(0.99));
}

TEST(PetWander, TheBehaviorMovesTheHideChance)
{
    // A shy pet hides on almost every edge step, a bold one rarely (ADR 0094).
    auto const shy  = makeWander(Gait::Trot, QPoint{500, 500}, 30, 0.5, 0.9);
    auto const bold = makeWander(Gait::Trot, QPoint{500, 500}, 30, 0.5, 0.3);
    EXPECT_TRUE(shy.hides(0.85));
    EXPECT_FALSE(bold.hides(0.85));
}

TEST(PetWander, TheHiddenWindowLeavesTheFigureInkVisibleNotTheSquareCanvas)
{
    // The ink runs from 90 to 250 of the 264 px window, so the visible sliver is 34% of 160 px.
    auto const left = PetWander::hiddenX(kArea, kPet, EdgeSide::Left, 90, 250);
    EXPECT_EQ(left, -196);
    EXPECT_EQ(left + 250, 54) << "the ink's right edge sits 54 px inside the screen";

    auto const right = PetWander::hiddenX(kArea, kPet, EdgeSide::Right, 90, 250);
    EXPECT_EQ(right, 1776);
    EXPECT_EQ(right + 90, 1866) << "the ink's left edge sits 54 px inside the right edge";
}

TEST(PetWander, HidingWorksOnAScreenWithNegativeCoordinates)
{
    QRect const second{-1920, 0, 1920, 1040};

    auto const left = PetWander::hiddenX(second, kPet, EdgeSide::Left, 90, 250);
    EXPECT_EQ(left + 250, -1866);

    auto const right = PetWander::hiddenX(second, kPet, EdgeSide::Right, 90, 250);
    EXPECT_EQ(right + 90, -54);
}

TEST(PetWander, TheHiddenBaseLeavesTheInkVisibleOnTheChosenEdge)
{
    auto const base = PetWander::hiddenBase(kArea, kPet, EdgeSide::Left, QPoint{120, 300}, 90, 250);

    EXPECT_EQ(base.x(), PetWander::hiddenX(kArea, kPet, EdgeSide::Left, 90, 250));
    EXPECT_EQ(base.y(), 300) << "a release row already inside the area is kept";
}

TEST(PetWander, TheHiddenBaseClampsTheReleaseRowIntoTheArea)
{
    EXPECT_EQ(PetWander::hiddenBase(kArea, kPet, EdgeSide::Right, QPoint{120, -50}, 90, 250).y(), kArea.top());
    // The lowest top-left that keeps the window whole is 1040 - 264.
    EXPECT_EQ(PetWander::hiddenBase(kArea, kPet, EdgeSide::Right, QPoint{120, 2000}, 90, 250).y(), 776);
}

TEST(PetWander, AHiddenWindowTallerThanTheAreaSitsAtItsTop)
{
    QRect const shortArea{0, 0, 1920, 200};
    EXPECT_EQ(PetWander::hiddenBase(shortArea, kPet, EdgeSide::Left, QPoint{120, 50}, 90, 250).y(), shortArea.top());
}

TEST(PetWander, ThePeekLeansInAndBackOverItsCycle)
{
    EXPECT_EQ(PetWander::peekOffset(0ms), 0);
    EXPECT_EQ(PetWander::peekOffset(PetWander::kPeekCycle / 4), 15);
    EXPECT_EQ(PetWander::peekOffset(PetWander::kPeekCycle / 2), 30);
    EXPECT_EQ(PetWander::peekOffset(PetWander::kPeekCycle), 0);
}

TEST(PetWander, TheSlideEasesOutAndEndsOnTheHiddenBase)
{
    auto const from = QPoint{100, 100};
    auto const base = QPoint{0, 100};

    EXPECT_EQ(PetWander::slideAt(from, base, 0ms), from);
    EXPECT_EQ(PetWander::slideAt(from, base, PetWander::kHideSlide), base);
    EXPECT_EQ(PetWander::slideAt(from, base, PetWander::kHideSlide / 2).x(), 13) << "ease-out has covered seven eighths";
}

TEST(PetWander, AFourFifthsDropEndsTheHideAndAnythingLessSnapsBack)
{
    EXPECT_TRUE(PetWander::draggedOut(QPoint{-52, 500}, kPet, kArea));
    EXPECT_FALSE(PetWander::draggedOut(QPoint{-53, 500}, kPet, kArea));

    EXPECT_TRUE(PetWander::draggedOut(QPoint{1708, 500}, kPet, kArea));
    EXPECT_FALSE(PetWander::draggedOut(QPoint{1709, 500}, kPet, kArea));
}

} // namespace
