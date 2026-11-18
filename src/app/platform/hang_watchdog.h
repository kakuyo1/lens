#pragma once

#include <filesystem>
#include <functional>
#include <string>

/**
 * @file hang_watchdog.h
 * @brief The GUI thread's pulse, watched from a thread a stall cannot freeze, so a hang leaves a
 *        dump behind (the gap crash_dump leaves by construction: it only fires on a fault).
 */

namespace lens::app {

/**
 * @brief Start stamping the GUI thread's pulse and watching it, once per run.
 * @param logDir Directory the log lives in; the dump lands in crashDumpDir(logDir) beside a crash's own.
 * @param renderingNow Asked from the watchdog thread the moment a stall is found, and may be empty: the name of
 *        the window inside the shared frame lock (surface_graphics.h), which is where a frozen interface most often
 *        sits (docs/adr/0191) -- the log line and the dump then name the same frame.
 * @note Call on the GUI thread after the surfaces are up. A silence of five seconds with no event-loop beat logs a
 *       line; the run's first stall also writes one minidump, because a dump directory the reader never asked for
 *       must not fill with stalls that recovered. The watchdog thread is detached and dies with the process.
 */
void startHangWatchdog(const std::filesystem::path& logDir, std::function<std::string()> renderingNow = {});

}
