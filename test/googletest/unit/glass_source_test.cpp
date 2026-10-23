/**
 * @file glass_source_test.cpp
 * @brief The desktop seam's fallback order, crossed without a desktop (src/app/glass/glass_source.h).
 *
 * The backdrop used to decide inside its own read which way answered: a duplicated output, then GDI, then a black
 * picture on a test platform. The order was therefore only observable by assembling a sheet, a window and a QML
 * surface, and the probe re-implemented the same DXGI and GDI calls to measure it (docs/adr/0086). These cases pin the
 * order with scripted sources, so a read that refuses, a read that finds nothing new, and a session that cannot be
 * tried at all are three separate facts, each asserted without a screen.
 */

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QRect>

#include <utility>

#include "app/glass/glass_source.h"

namespace {

using lens::app::DesktopRead;
using lens::app::DesktopSource;
using lens::app::FallbackSource;
using lens::app::GdiSource;
using lens::app::SheetScale;

/// A source that answers whatever a case tells it to, and counts how often it was asked.
struct ScriptedSource final : DesktopSource {
    explicit ScriptedSource(DesktopRead answer = {}, bool const duplicates = false)
        : next(std::move(answer)), duplicate(duplicates)
    {}

    DesktopRead next;
    bool        duplicate = false;
    int         reads     = 0;

    DesktopRead read(QRect, double, int) override
    {
        ++reads;
        return next;
    }
    bool canFollow() const override
    {
        return duplicate;
    }
};

/// @return A small picture no other source would produce, so a case can say which one answered.
QImage picture(QColor const& colour)
{
    auto image = QImage(8, 8, QImage::Format_RGB32);
    image.fill(colour);
    return image;
}

constexpr QRect kSheet{0, 0, 64, 48};

} // namespace

TEST(GlassSource, GdiShrinksToTheSheetScaleAndSpendsTheGdiBudget)
{
    // A quarter of 64 x 48 is 16 x 12. Off the desktop the read is a black picture that never touches the reader's
    // screen, which is how a test gets a GDI read at all.
    auto source = GdiSource{false, SheetScale::Quarter};
    auto read   = source.read(kSheet, 2.0);

    EXPECT_EQ(read.image.size(), QSize(16, 12));
    EXPECT_DOUBLE_EQ(read.image.devicePixelRatio(), 2.0);
    EXPECT_TRUE(read.newFrame);
    EXPECT_TRUE(read.usedGdi);
    EXPECT_FALSE(read.refused);
    EXPECT_FALSE(source.canFollow());
    EXPECT_GE(read.copyMs, 0.0);
}

TEST(GlassSource, APreferredFrameDoesNotTouchTheFallback)
{
    auto preferred = ScriptedSource{{.image = picture(Qt::red), .newFrame = true, .copyMs = 0.75}, true};
    auto fallback  = ScriptedSource{{.image = picture(Qt::blue), .usedGdi = true}};
    auto chain     = FallbackSource{{&preferred, &fallback}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_EQ(read.image.pixelColor(0, 0).rgb(), QColor(Qt::red).rgb());
    EXPECT_TRUE(read.newFrame);
    EXPECT_FALSE(read.refused);
    EXPECT_FALSE(read.usedGdi);
    EXPECT_DOUBLE_EQ(read.copyMs, 0.75);
    EXPECT_EQ(preferred.reads, 1);
    EXPECT_EQ(fallback.reads, 0);
    EXPECT_TRUE(chain.canFollow());
}

TEST(GlassSource, ARefusedDuplicationFallsBackToGdiAndSaysSo)
{
    auto preferred = ScriptedSource{{.refused = true}, true};
    auto fallback  = ScriptedSource{{.image = picture(Qt::blue), .newFrame = true, .usedGdi = true}};
    auto chain     = FallbackSource{{&preferred, &fallback}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_EQ(read.image.pixelColor(0, 0).rgb(), QColor(Qt::blue).rgb());
    EXPECT_TRUE(read.usedGdi);
    // The sheet reads this to tell the reader the screen refused duplication; a fallback that forgot the refusal would
    // leave the settings page saying nothing about a screen that cannot go fast.
    EXPECT_TRUE(read.refused);
    EXPECT_EQ(preferred.reads, 1);
    EXPECT_EQ(fallback.reads, 1);
}

TEST(GlassSource, NoNewFrameIsNotAFallback)
{
    // The duplicated output is healthy and the desktop behind the card simply did not change. Re-reading the screen
    // through GDI here would spend the shared budget to publish the same picture.
    auto preferred = ScriptedSource{{}, true};
    auto fallback  = ScriptedSource{{.image = picture(Qt::blue), .usedGdi = true}};
    auto chain     = FallbackSource{{&preferred, &fallback}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_TRUE(read.image.isNull());
    EXPECT_FALSE(read.newFrame);
    EXPECT_FALSE(read.refused);
    EXPECT_FALSE(read.usedGdi);
    EXPECT_EQ(fallback.reads, 0);
}

TEST(GlassSource, ASessionThatCannotDuplicateSkipsIt)
{
    // A permanently refused output is not retried: the read goes straight to the fallback and still reports that
    // duplication was unavailable for this screen.
    auto preferred = ScriptedSource{{.refused = true}, false};
    auto fallback  = ScriptedSource{{.image = picture(Qt::green), .usedGdi = true}};
    auto chain     = FallbackSource{{&preferred, &fallback}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_EQ(read.image.pixelColor(0, 0).rgb(), QColor(Qt::green).rgb());
    EXPECT_TRUE(read.refused);
    EXPECT_TRUE(read.usedGdi);
    EXPECT_EQ(preferred.reads, 0);
    EXPECT_EQ(fallback.reads, 1);
    EXPECT_FALSE(chain.canFollow());
}

TEST(GlassSource, AMiddleSourceThatFollowsKeepsTheSlowPathNoteAway)
{
    // Duplication refused, Windows Graphics Capture served: the screen is not on the slow path, so the settings page
    // must not say it is reading fifteen times a second.
    auto duplication = ScriptedSource{{.refused = true}, false};
    auto capture     = ScriptedSource{{.image = picture(Qt::yellow), .newFrame = true}, true};
    auto gdi         = ScriptedSource{{.image = picture(Qt::black), .usedGdi = true}};
    auto chain       = FallbackSource{{&duplication, &capture, &gdi}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_EQ(read.image.pixelColor(0, 0).rgb(), QColor(Qt::yellow).rgb());
    EXPECT_FALSE(read.usedGdi);
    EXPECT_FALSE(read.refused);
    EXPECT_EQ(duplication.reads, 0);
    EXPECT_EQ(capture.reads, 1);
    EXPECT_EQ(gdi.reads, 0);
    EXPECT_TRUE(chain.canFollow());
}

TEST(GlassSource, AFrameKeepsTheFingerprintItsSourceTook)
{
    // Windows Graphics Capture takes the fingerprint on the worker, where the frame is already mapped, so the sheet can
    // compare it without walking the picture again on the GUI thread. The chain must carry it through untouched.
    auto preferred = ScriptedSource{{.image = picture(Qt::red), .newFrame = true, .fingerprint = 0x5eed}, true};
    auto fallback  = ScriptedSource{};
    auto chain     = FallbackSource{{&preferred, &fallback}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_EQ(read.fingerprint, 0x5eedu);
}

TEST(GlassSource, EveryFastSourceRefusingLeavesTheLastOneToAnswer)
{
    auto duplication = ScriptedSource{{.refused = true}, true};
    auto capture     = ScriptedSource{{.refused = true}, true};
    auto gdi         = ScriptedSource{{.image = picture(Qt::black), .usedGdi = true}};
    auto chain       = FallbackSource{{&duplication, &capture, &gdi}};

    auto const read = chain.read(kSheet, 1.0);

    EXPECT_TRUE(read.usedGdi);
    // The note follows the source that answered, not the number of refusals before it.
    EXPECT_TRUE(read.refused);
    EXPECT_EQ(duplication.reads, 1);
    EXPECT_EQ(capture.reads, 1);
    EXPECT_EQ(gdi.reads, 1);
}
