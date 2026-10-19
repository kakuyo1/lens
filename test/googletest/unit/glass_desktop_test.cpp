/**
 * @file glass_desktop_test.cpp
 * @brief What a sheet is read at: the scale the request asks for, and the black stand-in where there is no desktop.
 *
 * The read itself is measured on a real screen by `GlassReadPace` (test/googletest/integration/); these cases pin the
 * part that must not silently change: which resolution a `SheetScale` asks for, and that a platform with nothing to
 * read yields a black sheet rather than a failed load. The scale is an enum rather than an int because the argument
 * next to it is a `bool`, and an int there would accept the bool and quietly read every sheet at full resolution.
 */

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QRect>

#include "app/glass_desktop.h"

namespace {

using lens::app::readDesktopRegion;
using lens::app::SheetScale;

/// A region big enough that each scale's quotient is its own: no rounding to argue with.
constexpr QRect kRegion{0, 0, 1000, 800};

TEST(GlassDesktop, ScaleDecidesThePictureSize)
{
    EXPECT_EQ(readDesktopRegion(kRegion, SheetScale::Full, false).size(), QSize(1000, 800));
    EXPECT_EQ(readDesktopRegion(kRegion, SheetScale::Half, false).size(), QSize(500, 400));
    EXPECT_EQ(readDesktopRegion(kRegion, SheetScale::Quarter, false).size(), QSize(250, 200));
}

TEST(GlassDesktop, NoDesktopIsABlackSheetNotAFailure)
{
    auto const picture = readDesktopRegion(kRegion, SheetScale::Quarter, false);
    ASSERT_FALSE(picture.isNull());
    EXPECT_FALSE(picture.hasAlphaChannel());
    // The card draws nothing over a black sheet, which is what an offscreen platform and a non-Windows build must do:
    // a failed load would leave the glass empty and look like a bug (docs/adr/0034).
    EXPECT_EQ(picture.pixel(0, 0), QColor(Qt::black).rgb());
    EXPECT_EQ(picture.pixel(249, 199), QColor(Qt::black).rgb());
}

TEST(GlassDesktop, ASheetIsOpaqueInEveryByte)
{
    // A texture upload reads the fourth byte. GDI leaves it zero on the real read, which made the sheet transparent and
    // the glass draw nothing (docs/adr/0070); the reader sets it, and the stand-in gets it from the fill. Pinned here
    // for both, since the real read cannot run in an offline case.
    auto const picture = readDesktopRegion(kRegion, SheetScale::Quarter, false);
    ASSERT_FALSE(picture.isNull());
    auto const* const first = picture.constScanLine(0);
    auto const* const last  = picture.constScanLine(picture.height() - 1);
    EXPECT_EQ(first[3], 0xFF);
    EXPECT_EQ(last[(picture.width() - 1) * 4 + 3], 0xFF);
}

TEST(GlassDesktop, ARegionSmallerThanTheScaleStillReadsOnePixel)
{
    // A sheet can be asked for a region one device pixel on a side; dividing it away would give an empty picture and a
    // card that never fills.
    EXPECT_EQ(readDesktopRegion(QRect{0, 0, 3, 3}, SheetScale::Quarter, false).size(), QSize(1, 1));
}

} // namespace
