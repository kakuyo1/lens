/**
 * @file pet_placement_test.cpp
 * @brief Where the pet window goes when the screen under it changed: kept if it fits, else the bottom-right (PHASE3 3.5).
 */

#include <optional>

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_placement.h"

namespace {

using lens::app::pet::bottomRightOf;
using lens::app::pet::clampedWithin;
using lens::app::pet::fitsWithin;
using lens::app::pet::indexOfNearestArea;
using lens::app::pet::PetPlacement;
using lens::app::pet::placeWithin;

/// A 1920x1040 usable area, starting at the origin.
QRect const kArea{0, 0, 1920, 1040};
QSize const kPet{264, 264};

TEST(PetPlacement, AWindowInsideTheAreaFits)
{
    EXPECT_TRUE(fitsWithin(QPoint{100, 100}, kPet, kArea));
}

TEST(PetPlacement, AWindowHangingOffAnEdgeDoesNotFit)
{
    EXPECT_FALSE(fitsWithin(QPoint{1800, 100}, kPet, kArea));
}

TEST(PetPlacement, TheBottomRightCornerIsInsideTheArea)
{
    auto const at = bottomRightOf(kPet, kArea);

    EXPECT_EQ(at, (QPoint{1920 - 264, 1040 - 264}));
    EXPECT_TRUE(fitsWithin(at, kPet, kArea));
}

TEST(PetPlacement, AWindowThatFitsIsNotMoved)
{
    EXPECT_EQ(clampedWithin(QPoint{100, 100}, kPet, kArea), (QPoint{100, 100}));
    EXPECT_EQ(clampedWithin(bottomRightOf(kPet, kArea), kPet, kArea), bottomRightOf(kPet, kArea));
}

TEST(PetPlacement, AnOverhangingWindowIsPulledBackToTheEdgeItCrossed)
{
    // The far edges are the last row and column the window may start at.
    EXPECT_EQ(clampedWithin(QPoint{1800, 100}, kPet, kArea), (QPoint{1920 - 264, 100}));
    EXPECT_EQ(clampedWithin(QPoint{100, 900}, kPet, kArea), (QPoint{100, 1040 - 264}));
    EXPECT_EQ(clampedWithin(QPoint{-50, 100}, kPet, kArea), (QPoint{0, 100}));
    EXPECT_EQ(clampedWithin(QPoint{100, -50}, kPet, kArea), (QPoint{100, 0}));
}

TEST(PetPlacement, AWindowOverhangingACornerMovesOnBothAxes)
{
    EXPECT_EQ(clampedWithin(QPoint{1800, 900}, kPet, kArea), (QPoint{1920 - 264, 1040 - 264}));
}

TEST(PetPlacement, AWindowLargerThanTheAreaPinsToItsTopLeft)
{
    QSize const huge{2000, 2000};

    EXPECT_EQ(clampedWithin(QPoint{500, 500}, huge, kArea), kArea.topLeft());
}

TEST(PetPlacement, OnlyTheAxisTooShortPins)
{
    QSize const wide{2000, 264};

    EXPECT_EQ(clampedWithin(QPoint{500, 500}, wide, kArea), (QPoint{0, 500}));
}

TEST(PetPlacement, AWindowKeepsItsSpotWhileAnyScreenStillHoldsIt)
{
    QRect const second{1920, 0, 1920, 1040};

    EXPECT_EQ(placeWithin(QPoint{2000, 100}, kPet, {kArea, second}, kArea), (QPoint{2000, 100}));
}

TEST(PetPlacement, AWindowOnAScreenThatWentAwayMovesToThePrimaryCorner)
{
    EXPECT_EQ(placeWithin(QPoint{2000, 100}, kPet, {kArea}, kArea), bottomRightOf(kPet, kArea));
}

TEST(PetPlacement, APointInsideAnAreaIndexesThatArea)
{
    QRect const second{1920, 0, 1920, 1040};

    EXPECT_EQ(indexOfNearestArea(QPoint{2000, 100}, {kArea, second}), 1);
}

TEST(PetPlacement, APointOutsideEveryAreaIndexesTheNearestOne)
{
    QRect const second{1920, 0, 1920, 1040};

    // Left of the first screen and above the second: the first is nearer.
    EXPECT_EQ(indexOfNearestArea(QPoint{-300, 100}, {kArea, second}), 0);
    EXPECT_EQ(indexOfNearestArea(QPoint{4000, -50}, {kArea, second}), 1);
}

TEST(PetPlacement, NoAreaIndexesNothing)
{
    EXPECT_EQ(indexOfNearestArea(QPoint{10, 10}, {}), -1);
}

TEST(PetPlacement, TheScreensGiveEachPointItsNearestArea)
{
    QRect const second{1920, 0, 1920, 1040};
    auto        placement = PetPlacement{};
    placement.setScreens({kArea, second}, kArea);

    EXPECT_EQ(placement.nearest(QPoint{2000, 100}), 1);
    EXPECT_EQ(placement.areaAt(QPoint{2000, 100}), second);
    EXPECT_EQ(placement.nearest(QPoint{-300, 100}), 0);
    EXPECT_TRUE(placement.holds(QPoint{100, 100}, kPet));
    EXPECT_FALSE(placement.holds(QPoint{4000, 100}, kPet));
}

TEST(PetPlacement, AFitPullsTheWindowIntoItsNearestScreen)
{
    auto placement = PetPlacement{};
    placement.setScreens({kArea}, kArea);

    EXPECT_EQ(placement.fit(QPoint{100, 100}, kPet), (QPoint{100, 100}));
    EXPECT_EQ(placement.fit(QPoint{1800, 100}, kPet), (QPoint{1920 - 264, 100}));
}

TEST(PetPlacement, AKeepMovesOnlyAWindowNoScreenHolds)
{
    QRect const second{1920, 0, 1920, 1040};
    auto        placement = PetPlacement{};
    placement.setScreens({kArea, second}, kArea);

    EXPECT_EQ(placement.keep(QPoint{2000, 100}, kPet), (QPoint{2000, 100}));
    EXPECT_EQ(placement.keep(QPoint{5000, 100}, kPet), bottomRightOf(kPet, kArea));
}

TEST(PetPlacement, NoScreensLeavesTheWindowWhereItIs)
{
    auto placement = PetPlacement{};
    EXPECT_TRUE(placement.empty());
    EXPECT_EQ(placement.fit(QPoint{100, 100}, kPet), (QPoint{100, 100}));
    EXPECT_EQ(placement.keep(QPoint{100, 100}, kPet), (QPoint{100, 100}));
    EXPECT_FALSE(placement.areaAt(QPoint{100, 100}).isValid());
}

TEST(PetPlacement, TheHomeIsWrittenAndForgotten)
{
    auto placement = PetPlacement{};
    EXPECT_FALSE(placement.home().has_value());

    placement.setHome(QPoint{300, 200});
    EXPECT_EQ(placement.home(), (std::optional<QPoint>{QPoint{300, 200}}));

    placement.forgetHome();
    EXPECT_FALSE(placement.home().has_value());
}

} // namespace
