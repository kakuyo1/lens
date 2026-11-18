#pragma once

#include <filesystem>

/**
 * @file crash_dump.h
 * @brief The in-process crash handler: a minidump, a summary and a marker in the log's dumps
 *        directory when Lens dies, so a crash leaves more than the log behind.
 */

namespace lens::app {

/**
 * @brief Install the process-wide crash handlers for the rest of the run.
 * @param logDir Directory the log lives in; dumps go to crashDumpDir(logDir), created here.
 * @note Windows only, like the rest of src/app. The SEH unhandled-exception filter and the four
 *       CRT funnels (terminate, pure call, invalid parameter, SIGABRT) all write the same set of
 *       artifacts; dbghelp is loaded with the process, never inside the crash. After writing, the
 *       handler spawns lens_reporter.exe (src/reporter/) for the dialog the reader sees at once,
 *       and leaves a marker that AppController::reportStartupCrash reads on the next launch.
 */
void installCrashDumpHandler(const std::filesystem::path& logDir);

/**
 * @brief Write a minidump of this live process for a hang, into @p dumpDir.
 * @return The file written, or empty when the dump could not be made.
 * @note The crash writer's format without its crash: no summary, no marker and no reporter, because
 *       nothing faulted and the process goes on. The name carries "-hang" so a by-hand round tells
 *       the two apart, and pruneCrashDumps keeps these under the same cap as the crash dumps.
 */
std::filesystem::path writeHangDump(const std::filesystem::path& dumpDir);

}
