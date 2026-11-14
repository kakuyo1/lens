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
 *       artifacts; dbghelp is loaded with the process, never inside the crash. The marker the
 *       handler leaves is the next launch's news -- AppController::reportStartupCrash reads it.
 */
void installCrashDumpHandler(const std::filesystem::path& logDir);

}
