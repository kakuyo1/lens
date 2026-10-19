/**
 * @file glass_duplication_test.cpp
 * @brief The one part of the duplicated-output read that can be checked without a desktop: where a rectangle of the
 *        screen lands in the frame (src/app/glass_duplication.h).
 *
 * The frame is what DXGI hands over, in the panel's native orientation; the screen rectangle is what the glass knows,
 * in the desktop's. On the measurement machine the external monitor is portrait: 1440 x 2560 on the desktop and a
 * 2560 x 1440 frame, turned 90 degrees. The numbers below are that machine's, so a mistake here is a wrong picture on
 * the only screen of that machine that can duplicate at all
 * (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.1).
 */

#include <gtest/gtest.h>

#include <QRect>
#include <QSize>

#include "app/glass_duplication.h"

namespace {

using lens::app::frameRectFor;
using lens::app::ScreenRotation;

/// @return @p rect in the form EXPECT_EQ prints readably.
QRect rect(int const x, int const y, int const w, int const h)
{
    return {x, y, w, h};
}

} // namespace

TEST(GlassDuplication, IdentityFrameIsTheScreen)
{
    EXPECT_EQ(frameRectFor(rect(100, 200, 300, 400), QSize(1440, 2560), ScreenRotation::Identity),
              rect(100, 200, 300, 400));
    EXPECT_EQ(frameRectFor(rect(0, 0, 1440, 2560), QSize(1440, 2560), ScreenRotation::Identity),
              rect(0, 0, 1440, 2560));
}

TEST(GlassDuplication, AQuarterTurnSwapsTheSidesAndMovesTheCorner)
{
    // The whole screen covers the whole frame whichever way it is turned.
    EXPECT_EQ(frameRectFor(rect(0, 0, 1440, 2560), QSize(2560, 1440), ScreenRotation::Rotate90),
              rect(0, 0, 2560, 1440));
    // Clockwise: the screen's top-left corner is the frame's top-right, and width and height trade places.
    EXPECT_EQ(frameRectFor(rect(100, 200, 300, 400), QSize(2560, 1440), ScreenRotation::Rotate90),
              rect(2560 - 200 - 400, 100, 400, 300));
}

TEST(GlassDuplication, TheOtherQuarterTurnPutsTheCornerTheOtherWay)
{
    EXPECT_EQ(frameRectFor(rect(0, 0, 1440, 2560), QSize(2560, 1440), ScreenRotation::Rotate270),
              rect(0, 0, 2560, 1440));
    // Counter-clockwise: the screen's top-left corner is the frame's bottom-left.
    EXPECT_EQ(frameRectFor(rect(100, 200, 300, 400), QSize(2560, 1440), ScreenRotation::Rotate270),
              rect(200, 1440 - 100 - 300, 400, 300));
}

TEST(GlassDuplication, HalfATurnMirrorsBothSides)
{
    EXPECT_EQ(frameRectFor(rect(100, 200, 300, 400), QSize(1440, 2560), ScreenRotation::Rotate180),
              rect(1440 - 100 - 300, 2560 - 200 - 400, 300, 400));
}

TEST(GlassDuplication, AQuarterTurnKeepsTheSheetInsideTheFrame)
{
    // A 112-DIP margin on a card near the bottom-right of the portrait screen: the mapped rectangle must still lie in
    // the landscape frame, or the copy box would be rejected and the glass would fall back for no reason.
    auto const sheet = rect(1440 - 800, 2560 - 900, 780, 880);
    auto const frame = frameRectFor(sheet, QSize(2560, 1440), ScreenRotation::Rotate90);
    EXPECT_TRUE(QRect(QPoint(0, 0), QSize(2560, 1440)).contains(frame));
    EXPECT_EQ(frame.size(), QSize(sheet.height(), sheet.width()));
}
