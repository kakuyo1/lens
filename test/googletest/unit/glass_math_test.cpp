/**
 * @file glass_math_test.cpp
 * @brief The glass sheet's arithmetic: the text tone over the desktop picture, its blur and saturation, and the
 *        displacement map the liquid theme bends with (docs/adr/0035, 0039). The corners and the bend themselves
 *        are in shaders/glass.frag.
 */

#include <gtest/gtest.h>

#include <QImage>

#include "app/glass_math.h"

#include <cstddef>
#include <vector>

namespace {

using lens::app::blurredPicture;
using lens::app::displacementMap;
using lens::app::pictureFingerprint;
using lens::app::readTone;
using lens::app::refractionMap;
using lens::app::settleTone;

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

TEST(GlassMath, AMarginalBackdropDoesNotFlipASettledTone)
{
    // 115 and 116 are the greys astride the crossover for the liquid theme, whose card is drawn straight on the
    // picture: over the one dark text reads 3.92 to 1 and light text 4.04, over the other 3.98 and 3.98. The rule
    // with no history picks light for the one and dark for the other, one 8-bit level apart — which a live sheet
    // re-read fifteen times a second would show as the text flickering.
    auto const justBelow = readTone(solid(40, 40, qRgb(115, 115, 115)), 0.0);
    auto const justAbove = readTone(solid(40, 40, qRgb(116, 116, 116)), 0.0);
    EXPECT_TRUE(justBelow.light());
    EXPECT_FALSE(justAbove.light());

    // A tone in force does not move across that level, however many times the desktop is read again.
    auto streak = 0;
    for (int read = 0; read < 8; ++read)
        EXPECT_FALSE(settleTone(justBelow, false, streak, lens::app::kToneHold));
    for (int read = 0; read < 8; ++read)
        EXPECT_TRUE(settleTone(justAbove, true, streak, lens::app::kToneHold));
}

TEST(GlassMath, ABackdropSwingingBetweenTwoTonesDoesNotFlipTheTone)
{
    // A video behind the card: one read says the desktop is nearly black, the next that it is nearly white.
    // Each read on its own is decisive, and the pair still cannot move the tone, however long it carries on.
    auto const black  = readTone(solid(40, 40, qRgb(20, 20, 20)), 0.0);
    auto const white  = readTone(solid(40, 40, qRgb(240, 240, 240)), 0.0);
    auto       streak = 0;
    auto       light  = true;
    for (int read = 0; read < 20; ++read) {
        light = settleTone(black, light, streak, lens::app::kToneHold);
        light = settleTone(white, light, streak, lens::app::kToneHold);
    }
    EXPECT_TRUE(light);
}

TEST(GlassMath, ABackdropThatChangesForGoodDoesMoveTheTone)
{
    // The card is dragged onto a pale window and stays there: the second read that asks for dark text takes it.
    auto const pale   = readTone(solid(40, 40, qRgb(220, 220, 220)), 0.0);
    auto       streak = 0;
    auto       light  = true;
    light             = settleTone(pale, light, streak, lens::app::kToneHold);
    EXPECT_TRUE(light);
    light = settleTone(pale, light, streak, lens::app::kToneHold);
    EXPECT_FALSE(light);

    // A sheet just taken is judged by its one read, the tone in force giving way across the margin alone.
    auto freshStreak = 0;
    EXPECT_FALSE(settleTone(pale, true, freshStreak, 1));
}

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

TEST(GlassMath, TheFingerprintOfAPictureIsStableAndOfAnotherPictureIsNot)
{
    auto const picture = split(64, 64);
    EXPECT_EQ(pictureFingerprint(picture), pictureFingerprint(picture));
    EXPECT_NE(pictureFingerprint(picture), pictureFingerprint(solid(64, 64, qRgb(255, 255, 255))));
    // The same content at another size is the same desktop, so the thumbnail has to come out alike enough to say so.
    EXPECT_EQ(pictureFingerprint(solid(64, 64, qRgb(40, 90, 140))), pictureFingerprint(solid(128, 96, qRgb(40, 90, 140))));
}

TEST(GlassMath, TheFingerprintIgnoresASingleChangedPixel)
{
    // What the fingerprint is for: a caret blinking under the card is not a reason to blur the sheet again. The
    // size is a real sheet's, since how much one pixel weighs in the thumbnail depends on it.
    auto const picture = solid(480, 270, qRgb(30, 30, 30));
    auto       changed = picture;
    changed.setPixel(240, 135, qRgb(255, 255, 255));
    EXPECT_EQ(pictureFingerprint(picture), pictureFingerprint(changed));
}

TEST(GlassMath, TheFingerprintSeesARealChangeOfBackdrop)
{
    // Half the desktop turning white is what the glass has to follow, one pixel or not.
    auto       picture = solid(480, 270, qRgb(30, 30, 30));
    auto const before  = pictureFingerprint(picture);
    for (int y = 0; y < picture.height(); ++y)
        for (int x = 0; x < picture.width() / 2; ++x)
            picture.setPixel(x, y, qRgb(255, 255, 255));
    EXPECT_NE(before, pictureFingerprint(picture));
}

TEST(GlassMath, TheFingerprintOfNothingIsZero)
{
    EXPECT_EQ(pictureFingerprint(QImage{}), 0u);
}
