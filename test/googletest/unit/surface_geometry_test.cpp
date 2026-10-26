/**
 * @file surface_geometry_test.cpp
 * @brief Every surface's place as a value: anchor, size, gap, shadow and work area in, top-left out, and the
 *        one per-screen native-to-DIP conversion (docs/adr/0116).
 *
 * These are the cases that used to stand up Main.qml. The window side is only SurfaceHost's ten lines of
 * reading and writing, and tst_main keeps one case for that handover.
 */

#include <gtest/gtest.h>

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include "app/geometry/surface_geometry.h"

namespace {

using lens::app::geometry::SurfaceGeometry;

/// A 1920 by 1040 DIP work area: the primary screen less its taskbar, and large enough for every card.
QRectF const kWorkArea{0, 0, 1920, 1040};

} // namespace

TEST(SurfaceGeometry, BesideAnchorPutsTheCardsCornerOnTheAnchorCentre)
{
    SurfaceGeometry const geometry;
    auto const            anchor = QRectF{1000, 500, 24, 24};
    auto const            at     = geometry.besideAnchor(anchor, QSizeF{332, 300}, kWorkArea, 26);

    EXPECT_EQ(at, QPoint(706, 238));
    // The card, not the window: both shadow bands are taken off before the corner is compared.
    EXPECT_DOUBLE_EQ(at.x() + 332 - 26, anchor.center().x());
    EXPECT_DOUBLE_EQ(at.y() + 300 - 26, anchor.center().y());
}

TEST(SurfaceGeometry, BesideAnchorClampsTheSidesAndFlipsBelowTheTopEdge)
{
    SurfaceGeometry const geometry;
    auto const            size = QSizeF{332, 300};

    // Near the right and bottom edges the panel is pulled in, never off the work area.
    EXPECT_EQ(geometry.besideAnchor(QRectF{1900, 500, 24, 24}, size, kWorkArea, 26), QPoint(1580, 238));
    EXPECT_EQ(geometry.besideAnchor(QRectF{1000, 1030, 24, 24}, size, kWorkArea, 26), QPoint(706, 732));

    // No room above: the panel's top goes below the anchor's bottom, 8 DIPs clear.
    EXPECT_EQ(geometry.besideAnchor(QRectF{1000, 10, 24, 24}, size, kWorkArea, 26), QPoint(706, 42));

    // A work area narrower than the panel collapses both axes onto the near edge rather than inverting.
    EXPECT_EQ(geometry.besideAnchor(QRectF{150, 100, 0, 0}, size, QRectF{0, 0, 300, 200}, 26), QPoint(8, 8));
}

TEST(SurfaceGeometry, BesidePetTakesTheSideThatClearsThePet)
{
    SurfaceGeometry const geometry;
    // The pet in the top-left corner, a card that fits on every side: the first candidate, the right, wins.
    auto const at = geometry.besidePet(QRectF{100, 100, 264, 300}, QSizeF{210, 300}, kWorkArea, 8);
    EXPECT_EQ(at, QPoint(372, 100));
}

TEST(SurfaceGeometry, BesidePetScoresTheSidesByHowMuchOfThePetTheyCover)
{
    SurfaceGeometry const geometry;
    // A card too wide for the room left of the pet: the right side clamps back over it, the left side does not.
    auto const at = geometry.besidePet(QRectF{500, 0, 200, 500}, QSizeF{300, 532}, QRectF{0, 0, 800, 500}, 8);
    EXPECT_EQ(at, QPoint(192, 8));
}

TEST(SurfaceGeometry, BesidePetReadsThePetThatWasDraggedPastAnEdgeAsItsOnScreenPart)
{
    SurfaceGeometry const geometry;
    // Wholly past the left edge: the pet reads as a zero-width line on the edge, and the card lands beside it.
    auto const at = geometry.besidePet(QRectF{-500, 100, 200, 200}, QSizeF{200, 200}, QRectF{0, 0, 800, 600}, 8);
    EXPECT_EQ(at, QPoint(16, 100));
}

TEST(SurfaceGeometry, TransientHangsOnTheChosenSideAndFlipsOnlyWhenItHasNoRoom)
{
    SurfaceGeometry const geometry;
    auto const            area   = QRectF{0, 0, 1920, 1000};
    auto const            size   = QSizeF{332, 100};
    auto const            anchor = QPointF{500, 500};

    // Both sides fit: the reader's choice decides.
    EXPECT_EQ(geometry.transient(anchor, size, area, 10, 26, false), QPoint(474, 416));
    EXPECT_EQ(geometry.transient(anchor, size, area, 10, 26, true), QPoint(474, 484));

    // Near the top nothing fits above; near the bottom nothing fits below. The other side is taken.
    EXPECT_EQ(geometry.transient(QPointF{500, 40}, size, area, 10, 26, false), QPoint(474, 24));
    EXPECT_EQ(geometry.transient(QPointF{500, 960}, size, area, 10, 26, true), QPoint(474, 876));
}

TEST(SurfaceGeometry, TransientAlignsTheCardsLeftEdgeWithTheAnchorAndClampsAtTheEdges)
{
    SurfaceGeometry const geometry;
    auto const            area = QRectF{0, 0, 1920, 1000};
    auto const            size = QSizeF{332, 100};

    // The left edge lands on the anchor; the shadow band is already off.
    auto const middle = geometry.transient(QPointF{500, 500}, size, area, 10, 26, false);
    EXPECT_EQ(middle.x() + 26, 500);

    // At either edge the window is kept inside, and the card alone gives way.
    EXPECT_EQ(geometry.transient(QPointF{5, 500}, size, area, 10, 26, false).x(), 8);
    EXPECT_EQ(geometry.transient(QPointF{1900, 500}, size, area, 10, 26, false).x(), 1920 - 8 - 332);
}

TEST(SurfaceGeometry, ReviewCardKeepsItsGapAboveTheWordAndItsLeftEdgeOnTheWord)
{
    SurfaceGeometry const geometry;
    auto const            at = geometry.reviewCard(QRectF{300, 400, 40, 20}, QSizeF{432, 120}, kWorkArea, 10, 26);

    EXPECT_EQ(at, QPoint(274, 296));
    EXPECT_EQ(at.x() + 26, 300);
    EXPECT_EQ(at.y() + 120 - 26, 400 - 10);
}

TEST(SurfaceGeometry, ReviewCardFlipsBelowAtTheTopEdge)
{
    SurfaceGeometry const geometry;
    auto const            at = geometry.reviewCard(QRectF{500, 10, 40, 20}, QSizeF{432, 120}, kWorkArea, 10, 26);
    EXPECT_EQ(at, QPoint(474, 14));
}

TEST(SurfaceGeometry, ReviewCardTakesTheRoomierSideWhenNeitherSideFits)
{
    SurfaceGeometry const geometry;
    // 92 DIPs above the word, 72 below: neither fits a 150 DIP card, so the top edge is the answer.
    auto const at = geometry.reviewCard(QRectF{100, 100, 40, 20}, QSizeF{400, 150}, QRectF{0, 0, 800, 200}, 10, 26);
    EXPECT_EQ(at, QPoint(74, 8));
}

TEST(SurfaceGeometry, ReviewCardClampsTheLeftEdgeAtEitherSide)
{
    SurfaceGeometry const geometry;
    auto const            size = QSizeF{400, 120};
    EXPECT_EQ(geometry.reviewCard(QRectF{5, 400, 40, 20}, size, kWorkArea, 10, 26).x(), 8);
    EXPECT_EQ(geometry.reviewCard(QRectF{1900, 400, 40, 20}, size, kWorkArea, 10, 26).x(), 1920 - 8 - 400);
}

TEST(SurfaceGeometry, ReviewCardGrowsAwayFromTheWord)
{
    SurfaceGeometry const geometry;
    auto const            word  = QRectF{300, 400, 40, 20};
    auto const            small = geometry.reviewCard(word, QSizeF{432, 120}, kWorkArea, 10, 26);
    auto const            large = geometry.reviewCard(word, QSizeF{432, 300}, kWorkArea, 10, 26);

    EXPECT_LT(large.y(), small.y());
    EXPECT_EQ(large.y() + 300 - 26, 400 - 10);
}

TEST(SurfaceGeometry, AWindowThatGrewIsPulledBackOnScreen)
{
    SurfaceGeometry const geometry;
    EXPECT_EQ(geometry.keptOnScreen(QPointF{100, 100}, QSizeF{200, 100}, kWorkArea), QPoint(100, 100));
    EXPECT_EQ(geometry.keptOnScreen(QPointF{1900, 1000}, QSizeF{200, 100}, kWorkArea), QPoint(1712, 932));
    EXPECT_EQ(geometry.keptOnScreen(QPointF{900, 750}, QSizeF{200, 100}, QRectF{0, 0, 1000, 800}),
              QPoint(792, 692));

    // Larger than its work area: each axis pins to the near edge and the overflow stays on the far side.
    EXPECT_EQ(geometry.keptOnScreen(QPointF{0, 0}, QSizeF{1200, 900}, QRectF{0, 0, 1000, 800}), QPoint(8, 8));
}

TEST(SurfaceGeometry, BoundedSizeTakesBothMarginsOffTheWorkArea)
{
    SurfaceGeometry const geometry;
    EXPECT_EQ(geometry.boundedSize(QSizeF{432, 532}, QRectF{0, 0, 320, 480}), QSizeF(304, 464));
    EXPECT_EQ(geometry.boundedSize(QSizeF{0, 0}, kWorkArea), QSizeF(1, 1));
    EXPECT_EQ(geometry.boundedSize(QSizeF{5, 5}, QRectF{0, 0, 10, 10}), QSizeF(1, 1));
}

TEST(SurfaceGeometry, ABubbleHoldsTheHeightItsContentWillNeed)
{
    SurfaceGeometry const geometry;
    EXPECT_DOUBLE_EQ(geometry.windowHeight(200, 0, 26), 252);
    EXPECT_DOUBLE_EQ(geometry.windowHeight(120, 200, 26), 252);

    // A hold keeps the tallest it has held or is heading for; a hold already running does not fall back to
    // the content's height on a later begin().
    EXPECT_DOUBLE_EQ(geometry.heldFloor(0, false, 120, 300), 300);
    EXPECT_DOUBLE_EQ(geometry.heldFloor(200, true, 120, 300), 300);
    EXPECT_DOUBLE_EQ(geometry.heldFloor(200, false, 350, 300), 350);
}

TEST(SurfaceGeometry, APointConvertsFromItsScreensOwnOrigin)
{
    SurfaceGeometry const geometry;
    // The portrait monitor above the primary at 125%, values from the reader's machine (docs/QML.md 4).
    auto const origin = QPointF{197, -2560};
    auto const at     = geometry.dipFromNativePoint(QPointF{876, -1336}, origin, 1.25);

    EXPECT_DOUBLE_EQ(at.x(), 197 + (876 - 197) / 1.25);
    EXPECT_DOUBLE_EQ(at.y(), -2560 + (-1336 + 2560) / 1.25);
    EXPECT_EQ(geometry.nativeFromDipPoint(at, origin, 1.25), QPoint(876, -1336));

    // Scaling from zero would put the same point 512 DIPs away, which is the defect this pins shut.
    EXPECT_NE(at.y(), -1336 / 1.25);
}

TEST(SurfaceGeometry, TheConversionAndItsInverseAgreeOnEveryScreen)
{
    SurfaceGeometry const geometry;
    struct Screen {
        QPointF origin;
        qreal   ratio;
    };
    for (auto const& screen : {Screen{{0, 0}, 1.0}, Screen{{-1920, 0}, 1.5}, Screen{{197, -2560}, 1.25}}) {
        auto const native = QPointF{731.0, -219.0};
        auto const dip    = geometry.dipFromNativePoint(native, screen.origin, screen.ratio);
        EXPECT_EQ(geometry.nativeFromDipPoint(dip, screen.origin, screen.ratio), native.toPoint());
    }

    // A zero or negative ratio is read as 1 rather than dividing by zero.
    EXPECT_EQ(geometry.dipFromNativePoint(QPointF{250, 500}, QPointF{0, 0}, 0), QPointF(250, 500));
}

TEST(SurfaceGeometry, ASizeConvertsByTheScreensRatioAlone)
{
    SurfaceGeometry const geometry;
    EXPECT_DOUBLE_EQ(geometry.dipFromNativeLength(80, 1.25), 64);
    EXPECT_EQ(geometry.nativeFromDipLength(64, 1.25), 80);
    EXPECT_EQ(geometry.nativeFromDipLength(64, 0), 64);

    // Rounded the way QML rounds a phrase width on the way out, not truncated.
    EXPECT_EQ(geometry.nativeFromDipLength(10.5, 1.25), 13);
}
