/**
 * @file glass_stats.cpp
 * @brief The tallies behind the glass pipeline's summary line (see glass_stats.h).
 */

#include "glass_stats.h"

#include <QMutexLocker>

#include <algorithm>
#include <vector>

namespace lens::app {

namespace {

/// The fraction the worst-case number is taken at. One sample in twenty is allowed to be slower than this.
constexpr double kPercentile = 0.95;

/// @return The middle of @p sorted, the average of the two middles when there is no single one.
double middleOf(std::vector<double> const& sorted)
{
    auto const n = sorted.size();
    if (n == 0) return -1.0;
    if (n % 2 == 1) return sorted[n / 2];
    return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}

} // namespace

void GlassStats::Samples::add(double const ms)
{
    kept.push_back(ms);
    if (kept.size() > kKeep) kept.pop_front();
    ++total;
    max = std::max(max, ms);
}

double GlassStats::Samples::median() const
{
    auto sorted = std::vector<double>(kept.begin(), kept.end());
    std::sort(sorted.begin(), sorted.end());
    return middleOf(sorted);
}

double GlassStats::Samples::p95() const
{
    auto const n = kept.size();
    if (n == 0) return -1.0;
    auto sorted = std::vector<double>(kept.begin(), kept.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted[static_cast<std::size_t>(kPercentile * static_cast<double>(n - 1) + 0.5)];
}

void GlassStats::Samples::clear()
{
    kept.clear();
    total = 0;
    max   = 0.0;
}

void GlassStats::addRead(double const ms)
{
    QMutexLocker const lock(&mutex_);
    read_.add(ms);
    ++reads_;
}

void GlassStats::addFrame(double const ms)
{
    QMutexLocker const lock(&mutex_);
    frame_.add(ms);
    ++frames_;
}

void GlassStats::addRequest(double const ms)
{
    QMutexLocker const lock(&mutex_);
    request_.add(ms);
    ++requests_;
}

void GlassStats::countTick()
{
    QMutexLocker const lock(&mutex_);
    ++ticks_;
}

void GlassStats::countBusySkip()
{
    QMutexLocker const lock(&mutex_);
    ++busySkips_;
}

void GlassStats::countPublish()
{
    QMutexLocker const lock(&mutex_);
    ++publishes_;
}

void GlassStats::countIdenticalSkip()
{
    QMutexLocker const lock(&mutex_);
    ++identicalSkips_;
}

void GlassStats::countFrameSkip()
{
    QMutexLocker const lock(&mutex_);
    ++frameSkips_;
}

void GlassStats::appendStage(char const* const label, Samples const& samples, QString& out) const
{
    if (samples.total == 0) {
        out += QStringLiteral(" | %1 ms: no sample").arg(QString::fromLatin1(label));
        return;
    }
    out += QStringLiteral(" | %1 ms med %2 p95 %3 worst %4")
               .arg(QString::fromLatin1(label))
               .arg(samples.median(), 0, 'f', 1)
               .arg(samples.p95(), 0, 'f', 1)
               .arg(samples.max, 0, 'f', 1);
}

void GlassStats::clearWindow()
{
    read_.clear();
    frame_.clear();
    request_.clear();
    ticks_          = 0;
    busySkips_      = 0;
    reads_          = 0;
    frames_         = 0;
    frameSkips_     = 0;
    publishes_      = 0;
    identicalSkips_ = 0;
    requests_       = 0;
}

QString GlassStats::takeSummaryLine(int const windowSeconds)
{
    auto read           = Samples{};
    auto frame          = Samples{};
    auto request        = Samples{};
    auto tickCount      = long{};
    auto busySkipCount  = long{};
    auto readCount      = long{};
    auto frameCount     = long{};
    auto frameSkipCount = long{};
    auto publishCount   = long{};
    auto identicalCount = long{};
    auto requestCount   = long{};
    {
        QMutexLocker const lock(&mutex_);
        // Anything at all counts as activity. A sheet taken on the GUI thread never ticks, so the reads alone are a
        // window worth reporting: that is the one path where a desktop read happens with no tick behind it. A window
        // that only polled a still duplicated output has the tick and the skipped frame to show for it.
        if (ticks_ + requests_ + static_cast<long>(read_.total) + static_cast<long>(frame_.total) + static_cast<long>(request_.total) == 0) return {};
        read           = read_;
        frame          = frame_;
        request        = request_;
        tickCount      = ticks_;
        busySkipCount  = busySkips_;
        readCount      = reads_;
        frameCount     = frames_;
        frameSkipCount = frameSkips_;
        publishCount   = publishes_;
        identicalCount = identicalSkips_;
        requestCount   = requests_;
        clearWindow();
    }
    auto out = QStringLiteral("glass %1s: ticks %2 busySkips %3 reads %4 frames %5 noFrame %6 publishes %7 sameSkips %8 requests %9")
                   .arg(windowSeconds)
                   .arg(tickCount)
                   .arg(busySkipCount)
                   .arg(readCount)
                   .arg(frameCount)
                   .arg(frameSkipCount)
                   .arg(publishCount)
                   .arg(identicalCount)
                   .arg(requestCount);
    appendStage("desktop read", read, out);
    appendStage("frame copy", frame, out);
    appendStage("image request", request, out);
    return out;
}

GlassStats& glassStats()
{
    static GlassStats stats;
    return stats;
}

} // namespace lens::app
