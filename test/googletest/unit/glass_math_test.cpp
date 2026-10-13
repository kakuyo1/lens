/**
 * @file glass_math_test.cpp
 * @brief The glass sheet's arithmetic: the blur and saturation of the desktop picture, and the displacement map the
 *        liquid theme bends with (docs/adr/0039). The corners and the bend themselves are in shaders/glass.frag.
 */

#include <gtest/gtest.h>

#include <QImage>

#include "app/glass_math.h"

#include <cstddef>
#include <vector>

namespace {

using lens::app::blurredPicture;
using lens::app::displacementMap;
using lens::app::refractionMap;

QImage solid(int w, int h, QRgb colour)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(colour);
    return image;
}

/// Black on the left half, white on the right: an edge a blur softens.
QImage split(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, x < w / 2 ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
    return image;
}

} // namespace

TEST(GlassMath, ZeroBlurAndFullSaturationLeaveThePictureAsItWas)
{
    auto const sheet = blurredPicture(solid(60, 60, qRgb(40, 120, 200)), 0, 100);

    EXPECT_EQ(qRed(sheet.pixel(30, 30)), 40);
    EXPECT_EQ(qGreen(sheet.pixel(30, 30)), 120);
    EXPECT_EQ(qBlue(sheet.pixel(30, 30)), 200);
}

TEST(GlassMath, ZeroSaturationGreysThePictureByLuma)
{
    auto const sheet = blurredPicture(solid(60, 60, qRgb(255, 0, 0)), 0, 0);

    // CSS saturate(0): every channel becomes the luma of the pixel, 0.213 of pure red.
    EXPECT_EQ(qRed(sheet.pixel(30, 30)), 54);
    EXPECT_EQ(qGreen(sheet.pixel(30, 30)), 54);
    EXPECT_EQ(qBlue(sheet.pixel(30, 30)), 54);
}

TEST(GlassMath, BlurSoftensAnEdgeAndLeavesFarPixelsAlone)
{
    auto const sheet = blurredPicture(split(60, 60), 10, 100);

    EXPECT_LT(qRed(sheet.pixel(5, 30)), 5);
    EXPECT_GT(qRed(sheet.pixel(55, 30)), 250);
    EXPECT_GT(qRed(sheet.pixel(30, 30)), 40);
    EXPECT_LT(qRed(sheet.pixel(30, 30)), 215);
}

TEST(GlassMath, TheBlurFollowsTheDevicePixelRatio)
{
    // The same content at 2x is twice the device pixels, so the same blur spreads twice as far in them: a pixel six
    // device pixels from the edge is darker at 1x than at 2x.
    auto       picture = split(60, 60);
    auto const at1x    = blurredPicture(picture, 10, 100);
    picture.setDevicePixelRatio(2.0);
    auto const at2x = blurredPicture(picture, 10, 100);

    EXPECT_GT(qRed(at2x.pixel(24, 30)), qRed(at1x.pixel(24, 30)) + 10);
}

TEST(GlassMath, DisplacementMapIsNullWhenEmptyAndNeutralAtTheCentre)
{
    EXPECT_TRUE(displacementMap(QSize(0, 10), 5, 4).isNull());

    auto const map    = displacementMap(QSize(80, 80), 10, 18);
    auto const centre = map.pixel(40, 40);
    EXPECT_EQ(qRed(centre), 128);
    EXPECT_EQ(qGreen(centre), 128);
}

TEST(GlassMath, RefractionMapIsTheCardsSizeInDevicePixels)
{
    EXPECT_EQ(refractionMap(QSize(80, 60), 10, 1.25).size(), QSize(80, 60));
}

TEST(GlassMath, NativeRectScalesFromTheScreensCornerOnThePrimary)
{
    // Primary at 125%: the screen starts at device pixel (0, 0).
    EXPECT_EQ(lens::app::toNativeRect(QRect(100, 40, 200, 100), QRect(0, 0, 1536, 864), QPoint(0, 0), 1.25),
              QRect(125, 50, 250, 125));
}

TEST(GlassMath, NativeRectStartsFromTheSecondScreensOwnOrigin)
{
    // A screen to the right at 150%, with its device origin at (1920, -180): the offset from its own corner is
    // (20, 20) logical, which is (30, 30) device pixels from that origin.
    EXPECT_EQ(lens::app::toNativeRect(QRect(1300, -100, 100, 50), QRect(1280, -120, 1280, 720), QPoint(1920, -180), 1.5),
              QRect(1950, -150, 150, 75));
}

TEST(GlassMath, NextLiveWalksRoundFromTheLastServed)
{
    // Sheets 0, 2 and 3 are live, and 2 was served last: the walk goes on to 3, then wraps to 0, and only then 2.
    auto const live = std::vector<bool>{true, false, true, true};
    EXPECT_EQ(lens::app::nextLive(live, 2), 3u);
    EXPECT_EQ(lens::app::nextLive(live, 3), 0u);
    EXPECT_EQ(lens::app::nextLive(live, 0), 2u);
}

TEST(GlassMath, NextLiveGivesEveryLiveSurfaceItsTurn)
{
    // Each refresh serves the one it returned, so three live sheets come round in the same order, each once.
    auto const live   = std::vector<bool>{false, true, true, false, true};
    auto       served = std::vector<std::size_t>{};
    auto       last   = std::size_t{4};
    for (int tick = 0; tick < 6; ++tick) {
        last = *lens::app::nextLive(live, last);
        served.push_back(last);
    }
    EXPECT_EQ(served, (std::vector<std::size_t>{1, 2, 4, 1, 2, 4}));
}

TEST(GlassMath, NextLiveIsNothingWhenNoSurfaceIsLive)
{
    EXPECT_FALSE(lens::app::nextLive(std::vector<bool>{false, false}, 0).has_value());
    EXPECT_FALSE(lens::app::nextLive(std::vector<bool>{}, 0).has_value());
}

TEST(GlassMath, NextLiveReturnsTheOnlyLiveSurfaceEveryTime)
{
    auto const live = std::vector<bool>{false, false, true};
    EXPECT_EQ(lens::app::nextLive(live, 2), 2u);
    EXPECT_EQ(lens::app::nextLive(live, 0), 2u);
}
