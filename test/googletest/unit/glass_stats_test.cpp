/**
 * @file glass_stats_test.cpp
 * @brief The glass pipeline's tally: what one summary line says about the durations and counts it gathered, and what
 *        it forgets when the window closes (src/app/glass/glass_stats.h).
 */

#include <gtest/gtest.h>

#include <QString>

#include "app/glass/glass_stats.h"

#include <cstddef>

namespace {

using lens::app::GlassStats;

/// @brief Does @p line carry the three numbers of one stage as the format writes them?
/// @param label The stage's name, as the line spells it.
/// @return True when the stage reads exactly as the samples say it should.
bool hasStage(QString const& line, char const* const label, QString const& med, QString const& p95, QString const& worst)
{
    auto const wanted = QStringLiteral(" | %1 ms med %2 p95 %3 worst %4")
                            .arg(QString::fromLatin1(label), med, p95, worst);
    return line.contains(wanted);
}

TEST(GlassStats, QuietWindowLogsNothing)
{
    auto stats = GlassStats();
    EXPECT_TRUE(stats.takeSummaryLine(10).isEmpty());
}

TEST(GlassStats, ReportsMedianPercentileAndWorst)
{
    auto stats = GlassStats();
    for (double value = 1.0; value <= 10.0; value += 1.0)
        stats.addRead(value);
    auto const line = stats.takeSummaryLine(10);
    // Ten samples: no single middle, so the median is the pair's mean, and the 95th percentile of ten is the last.
    EXPECT_TRUE(hasStage(line, "desktop read", QStringLiteral("5.5"), QStringLiteral("10.0"), QStringLiteral("10.0")));
    EXPECT_TRUE(line.contains(QStringLiteral("frame copy ms: no sample")));
    EXPECT_TRUE(line.contains(QStringLiteral("image request ms: no sample")));
}

TEST(GlassStats, ReportsTheDuplicatedFrameStage)
{
    auto stats = GlassStats();
    stats.countTick();
    stats.addFrame(0.8);
    stats.addFrame(1.2);
    stats.countFrameSkip();
    auto const line = stats.takeSummaryLine(10);
    // Two samples: the median is their mean, and the 95th percentile of two is the larger.
    EXPECT_TRUE(hasStage(line, "frame copy", QStringLiteral("1.0"), QStringLiteral("1.2"), QStringLiteral("1.2")));
    EXPECT_TRUE(line.contains(QStringLiteral("frames 2 noFrame 1")));
    // The GDI stage was not used, and says so rather than reporting a zero.
    EXPECT_TRUE(line.contains(QStringLiteral("desktop read ms: no sample")));
}

TEST(GlassStats, CountsTheRefreshBudget)
{
    auto stats = GlassStats();
    stats.countTick();
    stats.countTick();
    stats.countTick();
    stats.countBusySkip();
    stats.addRead(9.0);
    stats.countPublish();
    stats.countIdenticalSkip();
    stats.addRequest(2.0);
    auto const line = stats.takeSummaryLine(7);
    EXPECT_TRUE(line.contains(QStringLiteral("glass 7s: ticks 3 busySkips 1 reads 1 frames 0 noFrame 0 publishes 1 sameSkips 1 requests 1")));
}

TEST(GlassStats, WindowClosesAndRestarts)
{
    auto stats = GlassStats();
    stats.countTick();
    stats.addRead(5.0);
    EXPECT_FALSE(stats.takeSummaryLine(10).isEmpty());
    // The second call covers a window nothing was added to, so it reports nothing rather than repeating the first.
    EXPECT_TRUE(stats.takeSummaryLine(10).isEmpty());
    stats.countTick();
    auto const again = stats.takeSummaryLine(10);
    EXPECT_TRUE(again.contains(QStringLiteral("ticks 1")));
    // A tick with no read behind it: the stage has no numbers to give, and says so rather than inventing a zero.
    EXPECT_TRUE(again.contains(QStringLiteral("desktop read ms: no sample")));
}

TEST(GlassStats, OldSamplesFallOutButTheWorstStays)
{
    auto stats = GlassStats();
    stats.addRead(500.0);
    for (std::size_t i = 0; i <= GlassStats::kKeep; ++i)
        stats.addRead(1.0);
    auto const line = stats.takeSummaryLine(10);
    // More samples than the ring holds: the stall is no longer in the window the percentiles read, and the median
    // shows the recent reads. The worst case stays, because the window it describes is longer than the ring.
    EXPECT_TRUE(hasStage(line, "desktop read", QStringLiteral("1.0"), QStringLiteral("1.0"), QStringLiteral("500.0")));
}

TEST(GlassStats, ZeroDurationStillCounts)
{
    auto stats = GlassStats();
    stats.addRead(0.0);
    stats.countTick();
    auto const line = stats.takeSummaryLine(10);
    EXPECT_TRUE(hasStage(line, "desktop read", QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")));
}

} // namespace
