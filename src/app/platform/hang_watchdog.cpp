/**
 * @file hang_watchdog.cpp
 * @brief The pulse, the watcher and the one dump a run gets when the interface stops: see
 *        hang_watchdog.h.
 */

#include "app/platform/hang_watchdog.h"

#include "app/platform/crash_dump.h"
#include "paths.h"
#include "util/crash.h"
#include "util/log.h"

#include <QCoreApplication>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

namespace lens::app {
namespace {

/// How often the GUI thread stamps its pulse. A timer only fires while its thread runs the event
/// loop, which is what makes the stamp an honest signal.
constexpr int kPulseMs = 250;

/// How long a silence means the interface stopped, and how often the watcher reads the pulse.
constexpr std::chrono::seconds kStall{5};
constexpr std::chrono::seconds kPoll{1};

std::atomic<std::uint64_t> g_beats{0};

/// One dump per run, claimed by whichever stall takes it first: a machine a stall recovers on would
/// otherwise collect a dump every few minutes, and the first stall is the one worth reading.
std::atomic_flag g_dumped = ATOMIC_FLAG_INIT;

std::string describeHolder(std::string const& holder)
{
    return holder.empty() ? std::string{"no window holds the frame lock"} : "the frame lock was held by '" + holder + "'";
}

} // namespace

void startHangWatchdog(const std::filesystem::path& logDir, std::function<std::string()> renderingNow)
{
    auto* const pulse = new QTimer(QCoreApplication::instance());
    pulse->setInterval(kPulseMs);
    QObject::connect(pulse, &QTimer::timeout, pulse, [] { g_beats.fetch_add(1, std::memory_order_relaxed); });
    pulse->start();

    const std::filesystem::path dumpDir = lens::util::crashDumpDir(logDir);
    // NOLINTNEXTLINE(bugprone-exception-escape): the only throws here are allocation failures while reporting a stall, a failure path the program cannot recover from; std::terminate routes it into the crash artifacts
    std::thread([dumpDir, renderingNow = std::move(renderingNow)] {
        auto lastBeat = g_beats.load(std::memory_order_relaxed);
        auto since    = std::chrono::steady_clock::now();
        bool reported = false;
        while (true) {
            std::this_thread::sleep_for(kPoll);
            auto const beat = g_beats.load(std::memory_order_relaxed);
            if (beat != lastBeat) {
                lastBeat = beat;
                since    = std::chrono::steady_clock::now();
                reported = false;
                continue;
            }
            // One line per stall episode: the first poll that finds the threshold crossed reports,
            // and the following polls stay quiet until a beat says the interface is back.
            auto const stalled = std::chrono::steady_clock::now() - since;
            if (reported or stalled < kStall) continue;
            reported           = true;
            auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(stalled).count();
            auto const holder  = describeHolder(renderingNow ? renderingNow() : std::string{});
            if (g_dumped.test_and_set()) {
                LENS_WARN("the interface has not ticked for {} s; {}; no dump, this run already wrote one", seconds, holder); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
                continue;
            }
            auto const dump = writeHangDump(dumpDir);
            if (dump.empty())
                LENS_WARN("the interface has not ticked for {} s; {}; the hang dump could not be written", seconds, holder); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            else
                LENS_WARN("the interface has not ticked for {} s; {}; hang dump written to {}", seconds, holder, narrow(dump)); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
        }
    }).detach();
}

} // namespace lens::app
