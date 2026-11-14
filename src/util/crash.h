#pragma once

/**
 * @file crash.h
 * @brief The pure and filesystem side of crash forensics: where dumps live, what they are
 *        called, and what a crash hands to the next launch.
 *
 * The Win32 half that writes the minidump itself lives in app/platform/crash_dump -- dbghelp is
 * an application concern. Everything else is here, Qt-free, so the crash path and the
 * next-launch reader share one definition and the tests run offline: the dump directory and
 * file names, the summary a crash writes, and the retention rule.
 */

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace lens::util {

/// Name of the directory under the log directory that holds the dumps and the crash marker.
inline constexpr const char* kCrashDumpDirName = "dumps";

/// Name of the marker file a crash leaves behind. Its presence is what the next launch reads;
/// the notice that reads it removes it, so one crash is announced once.
inline constexpr const char* kCrashMarkerName = "last-crash.txt";

/// Dump files kept before the oldest is removed; the summary beside each one goes with it.
inline constexpr std::size_t kCrashDumpsKept = 5;

/// @brief Everything known about one crash, shared by the summary text and the marker.
struct CrashSummary {
    std::string version; ///< Application version that crashed.
    std::string time;    ///< Local time of the crash, "YYYY-MM-DD HH:MM:SS".
    std::string code;    ///< Exception or funnel code, "0xC0000005" style.
    std::string address; ///< Faulting instruction; empty when unknown.
    std::string module;  ///< Module holding @p address, file name only; empty when unresolved.
    std::string dump;    ///< Dump file name the summary sits beside; empty when none was written.
};

/// @brief The directory holding dumps under @p logDir.
std::filesystem::path crashDumpDir(const std::filesystem::path& logDir);

/// @brief The file-name stem one crash writes under: lens-<version>-<compact time>-<pid>.
/// @param version   Application version, dots and all.
/// @param localTime Compact local time, "YYYYMMDD-HHMMSS", as the crash path formats it.
/// @param processId The crashing process, so two crashes within one second still get distinct files.
std::string crashDumpStem(std::string_view version, std::string_view localTime, unsigned long processId);

/// @brief Serialize to the "key: value" lines the summary text and the marker carry.
/// @return The lines, empty fields omitted, with a trailing newline after each kept field.
std::string serializeCrashSummary(const CrashSummary& summary);

/// @brief Read back what serializeCrashSummary wrote.
/// @note Unknown keys are skipped and missing fields stay empty, so an older summary still parses.
CrashSummary parseCrashSummary(std::string_view text);

/// @brief Remove dump files beyond @p keep, oldest first, each summary beside a removed dump with it.
/// @return How many files were removed; a file that refuses removal is left and not counted.
/// @note A summary whose dump never landed carries nothing the marker does not, and goes too.
std::size_t pruneCrashDumps(const std::filesystem::path& dumpDir, std::size_t keep = kCrashDumpsKept);

/// @brief The prefilled GitHub issue URL a crash report opens, in both the facade and the reporter.
/// @param summary The crash the issue is about; empty fields leave their line out.
/// @return "https://github.com/kakuyo1/lens/issues/new?title=...&body=...", fully percent-encoded.
/// @note Lives here rather than beside either caller so the facade and the reporter process share
///       one template and cannot drift; the body quotes facts only, and the dump travels because
///       the reader attaches it (SECURITY.md).
std::string crashIssueUrl(const CrashSummary& summary);

}
