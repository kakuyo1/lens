#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/telemetry/event.h"

/**
 * @file event_log.h
 * @brief The local behavior-event log: one JSON line per event, one file per month.
 *
 * The log owns its directory and nothing else. It is Qt-free: the caller passes the device id and
 * the local month, so the application supplies what only it knows and the tests pin both.
 *
 * @note Not thread-safe. The application serializes writes on its own thread; a reader may call
 *       readMonth() while a writer appends, and sees whatever has been flushed.
 */

namespace lens::core::telemetry {

/// @brief The month file name's prefix and suffix; the month sits between them.
inline constexpr std::string_view kFilePrefix = "events-";
inline constexpr std::string_view kFileSuffix = ".jsonl";

/// @brief The state file's name, beside the month files.
inline constexpr std::string_view kStateFileName = "state.json";

/// @brief The state document's format version.
inline constexpr int kStateFormat = 1;

/// @brief Months the log keeps, the newest included.
inline constexpr int kRetentionMonths = 12;

/// @brief The size one month file may reach before a warning is logged once.
inline constexpr std::uintmax_t kMonthSoftLimitBytes = 50ULL * 1024 * 1024;

/**
 * @brief The local event log: append, month rollover, retention, and the state file.
 */
class EventLog { // NOLINT(bugprone-exception-escape): the throws are on failure paths the application cannot recover from; swallowing one would hide a lost event
public:
    /**
     * @brief Open the log under @p root, recovering the sequence number when the state is gone.
     *
     * @param root     Directory holding the month files and state.json; created on the first write.
     * @param deviceId This device's id; the log writes the first eight SHA-1 hex digits as `dev`.
     */
    EventLog(std::filesystem::path root, std::string_view deviceId);

    /**
     * @brief Validate @p data against @p name, assign the next sequence, and append one line.
     *
     * @param name  A registered event name.
     * @param tsMs  Event time in milliseconds since the epoch.
     * @param month Local month, `YYYY-MM`, choosing the file the line lands in.
     * @param data  The event's payload; every key must be listed by the event's spec.
     * @return False when the event or its payload fails the registry's checks; nothing is written.
     * @throws std::runtime_error If the month file cannot be opened or written.
     */
    bool append(std::string_view name, long long tsMs, std::string_view month, nlohmann::json data);

    /// @brief Flush the open month file and write the state document.
    /// @throws std::runtime_error If either write fails.
    void flush();

    /**
     * @brief Delete month files older than the retention window.
     *
     * The current month and the eleven before it are kept; every older file is removed. The
     * application calls this at start-up and when the local month changes.
     *
     * @param currentMonth The local month, `YYYY-MM`.
     * @return The months removed, oldest first. An invalid @p currentMonth removes nothing.
     */
    std::vector<std::string> prune(std::string_view currentMonth);

    /// @return The month files present, oldest first.
    std::vector<std::string> months() const;

    /**
     * @brief Read one month's events in file order.
     * @param month The month, `YYYY-MM`.
     * @return The events; malformed lines are skipped. A missing file reads as empty.
     */
    std::vector<Event> readMonth(std::string_view month) const;

    /// @return The month file's bytes, exactly as stored; empty when the file is missing.
    std::string readMonthText(std::string_view month) const;

    /// @return The bytes all month files hold.
    std::uintmax_t totalBytes() const;

    /// @brief Close and remove every month file. The sequence and the state stay, so the next event
    ///        still has a number larger than any this device wrote before.
    void clear();

    /// @return The state document, which the sync channel extends with upload bookkeeping.
    nlohmann::json& state()
    {
        return state_;
    }

    /// @return The same document, read-only.
    const nlohmann::json& state() const
    {
        return state_;
    }

    /// @brief Write the current sequence and the rest of the state document atomically.
    /// @throws std::runtime_error If the state cannot be written.
    void saveState();

    /// @return The directory the month files and state.json live in.
    const std::filesystem::path& root() const
    {
        return root_;
    }

    /// @return The short device id written as `dev`.
    const std::string& device() const
    {
        return device_;
    }

private:
    /// @return The month file's path.
    std::filesystem::path monthFile(std::string_view month) const;

    /// @brief Close the open file, make the directory, and open @p month for appending.
    void openMonth(std::string_view month);

    /// @brief Make the root directory, or throw when it cannot exist.
    void ensureRoot() const;

    /// @return The largest sequence any month file ends with; zero when there is none.
    unsigned long long recoverSequence() const;

    std::filesystem::path root_;
    std::string           device_;
    std::ofstream         out_;
    std::string           openMonth_;
    unsigned long long    seq_         = 0;
    nlohmann::json        state_       = nlohmann::json::object();
    bool                  limitWarned_ = false;
};

}
