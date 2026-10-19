#pragma once

/**
 * @file glass_stats.h
 * @brief What the glass pipeline costs while it runs, tallied inside the application.
 *
 * Two measurements of the same desktop read disagree by four times: the offline probe has 26.5 ms and 41.2 ms for the
 * two screens of one machine (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md), while the
 * in-application CPU record divides out to about 6.7 ms per read
 * (docs/metrics/resource-footprint/glass-memory-2026-10-18.md 9.7). The candidate cause is pacing — the probe reads
 * back to back, the application reads once every 67 ms — and only a read timed inside the application, at the pace the
 * application keeps, settles it. The frame rate and the CPU budget are both set by the answer, so this is measured
 * before either is touched (GLASS-CAPTURE-PLAN.md, stage 0).
 *
 * The tally is the log line: durations of the two stages that could be the wall, and the counters that show what the
 * refresh budget really delivered. It carries milliseconds and counts only. No pixels, no rectangle, no surface name
 * beyond the sheet the caller already logs elsewhere, so nothing here widens what the log knows about the reader's
 * screen (SECURITY.md, docs/adr/0034).
 */

#include <QMutex>
#include <QString>

#include <cstddef>
#include <deque>

namespace lens::app {

/**
 * @brief A thread-safe tally of the glass pipeline's durations and counts.
 *
 * Written from three threads: the GUI thread (a sheet just taken, the refresh ticks), the reader's worker thread (the
 * desktop read) and the image loader's worker threads (the blur and the map). Read from the GUI thread when the line
 * is logged.
 */
class GlassStats {
public:
    /// How many durations each stage keeps for its percentiles. Longer than one summary window holds at the refresh
    /// rate (about 150 reads in ten seconds), so the median, the percentile and the worst case describe the same
    /// stretch of time; the cap is only what stops the tally growing without bound.
    static constexpr std::size_t kKeep = 256;

    /// @brief Record one desktop read of @p ms milliseconds, and count it.
    /// @note Duration and count are one call on purpose: a read that was timed was a read that happened, and keeping
    ///       them apart lets a line report a median with no reads behind it.
    void addRead(double ms);
    /// @brief Record one image-provider request of @p ms milliseconds: the copy out of the sheet, the blur, the map.
    void addRequest(double ms);

    /// @brief Record one refresh timer tick.
    void countTick();
    /// @brief Record a tick that found the previous read still running and did nothing.
    void countBusySkip();
    /// @brief Record a picture put in force.
    void countPublish();
    /// @brief Record a read whose fingerprint matched the picture in force, so nothing was published.
    void countIdenticalSkip();

    /// @return The line covering the window since the last call, and start a fresh window.
    /// @param windowSeconds How long the window was, as the caller counts it: it goes on the line.
    /// @return Empty when nothing happened in the window, so a quiet desktop logs nothing.
    QString takeSummaryLine(int windowSeconds);

private:
    /// The durations of one stage, and how many were added in all since the window opened.
    struct Samples {
        std::deque<double> kept;
        std::size_t        total = 0;
        double             max   = 0.0;

        void add(double ms);
        /// @return The median of the kept samples, or -1 when none.
        double median() const;
        /// @return The sample at the 95th percentile of those kept, or -1 when none.
        double p95() const;
        void   clear();
    };

    /// @brief One stage's three numbers as they read in the line: median, 95th percentile, worst.
    /// @param label The stage's name, as the line spells it.
    /// @param out Where to write the three numbers.
    void appendStage(char const* label, Samples const& samples, QString& out) const;
    /// @brief Drop the samples and zero every counter. Called with the lock held.
    void clearWindow();

    mutable QMutex mutex_;
    Samples        read_;
    Samples        request_;
    long           ticks_          = 0;
    long           busySkips_      = 0;
    long           reads_          = 0;
    long           publishes_      = 0;
    long           identicalSkips_ = 0;
    long           requests_       = 0;
};

/**
 * @brief The one tally of this process.
 * @return The tally every part of the glass pipeline writes to.
 * @note A single instance is the point: the question is what one machine's pipeline costs in total, not per object.
 */
GlassStats& glassStats();

} // namespace lens::app
