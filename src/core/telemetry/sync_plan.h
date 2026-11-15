#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/telemetry/event_log.h"

/**
 * @file sync_plan.h
 * @brief The pure reconciliation behind the event cloud channel: which local months go up, which
 *        remote versions come down, and which files are deleted.
 *
 * A month file is named `lens-events-<dev8>-YYYY-MM-r<NNNN>-<sha1-8>.json.enc`. The hash is the
 * first eight hex digits of the *plaintext* month, so a file whose content did not change keeps its
 * name and a changed month gets a new revision. The remote file list is the index; there is no
 * manifest in the main sync document.
 *
 * The plan never touches a network or a file: the duty reads the local months and the remote names,
 * asks this module what to do, and carries the actions out. That keeps the retention, revision,
 * daily-cap and state-rebuild rules assertable without a socket.
 */

namespace lens::core::telemetry {

/// @brief The event file name's prefix and suffix.
inline constexpr std::string_view kEventFilePrefix = "lens-events-";
inline constexpr std::string_view kEventFileSuffix = ".json.enc";

/// @brief One event file as its name spells it.
struct EventFileRef {
    std::string name;
    std::string device; ///< The short device id.
    std::string month;  ///< "YYYY-MM".
    int         revision = 0;
    std::string hash; ///< The eight hex digits of the month's plaintext digest.
};

/// @return The file's parts, or nothing when @p name is not an event file name.
std::optional<EventFileRef> parseEventFileName(std::string_view name);

/// @return The file name for @p device, @p month, @p revision and @p hash; empty when a part is
///         malformed or the revision is outside 1 through 9999.
std::string eventFileName(std::string_view device, std::string_view month, int revision, std::string_view hash); // NOLINT(bugprone-easily-swappable-parameters): the file name is positional; the device, month and hash cannot be reordered without producing a different name

/// @brief What one successful upload left for a month.
struct UploadedMonth {
    std::string file;
    std::string hash;
    int         revision = 0;
    /// The local date of the upload, "YYYY-MM-DD"; empty when the record was rebuilt from the remote
    /// list and the day is unknown.
    std::string day;
};

/// @brief The event channel's bookkeeping, kept in the event log's state.json.
struct SyncState {                                 // NOLINT(bugprone-exception-escape): the members are containers whose destruction is noexcept; the diagnostic reads the aggregate's implicit copy shape, not a throwing destructor
    std::map<std::string, UploadedMonth> uploaded; ///< Month to the file believed to be the newest.
    std::set<std::string>                pendingDeletes;
    /// The cloud copy was deleted: every event file the remote lists goes, whatever device or month
    /// it belongs to. Kept until the last delete succeeds, so an offline attempt is retried.
    bool deleteAll = false;
};

/**
 * @brief Read the event channel's state out of the log's state document.
 *
 * A malformed record is dropped rather than guessed at: the next plan rebuilds it from the remote
 * file list. The document's other keys (the event sequence among them) are not this function's.
 */
SyncState syncStateFromJson(nlohmann::json const& state);

/// @brief Write @p sync into @p state, replacing only the event channel's two keys.
void writeSyncState(nlohmann::json& state, SyncState const& sync);

/// @brief One local month the duty read: its name and the plaintext digest's first eight hex digits.
struct LocalMonth {
    std::string month;
    std::string hash;
};

/// @brief One upload the plan asks for. The duty seals the month's content and writes it at @c file.
struct UploadAction {
    std::string              month;
    std::string              file;
    std::string              hash;
    int                      revision = 0;
    std::string              day;    ///< The local date the upload runs on.
    std::vector<std::string> retire; ///< Older remote versions to delete once this upload lands.
};

/// @brief What one reconciliation asks the duty to do, and the state to keep while doing it.
struct Plan { // NOLINT(bugprone-exception-escape): the members are containers whose destruction is noexcept; the diagnostic reads the aggregate's implicit copy shape, not a throwing destructor
    std::vector<UploadAction> uploads;
    /// Files safe to delete before or after the uploads; they never carry a local month's current
    /// content.
    std::vector<std::string> deletes;
    SyncState                state;
};

/// @brief What the reconciliation reads.
struct PlanInput {                   // NOLINT(bugprone-exception-escape): the members are strings and containers whose destruction is noexcept; the diagnostic reads the aggregate's implicit copy shape, not a throwing destructor
    std::string              device; ///< This device's short id.
    std::string              currentMonth;
    std::string              today;          ///< The local date, for the current month's daily cap.
    bool                     manual = false; ///< A reader-asked sync ignores the current month's daily cap.
    std::vector<LocalMonth>  local;          ///< The retained local months, hashes included.
    std::vector<std::string> remote;         ///< Every event file name the cloud lists.
    SyncState                state;
    int                      retentionMonths = kRetentionMonths;
};

/**
 * @brief Reconcile the local months, the remote list and the state.
 *
 * Closed months upload once while their content stays unchanged; a late event gives them a new
 * revision. The current month uploads at most once per local day, unless @c manual is set. A month
 * file whose hash matches the newest remote version is adopted, which is how a lost state document
 * rebuilds itself without re-uploading or deleting the copy that is already there.
 *
 * @return The actions and the state to save before they run. Upload records are written only after
 *         an upload succeeds; deletions from pending or stale periods are there for the duty to
 *         carry out and then commit.
 */
Plan planEventSync(PlanInput input);

/// @brief Record @p upload as the newest file and queue the versions it retires.
void commitUpload(SyncState& state, UploadAction const& upload);

/// @brief Record that @p file was deleted.
void commitDelete(SyncState& state, std::string const& file);

/**
 * @brief The files a month report should download: the highest revision of each device's copy.
 * @param remote Every event file name the cloud lists.
 * @param month  The month to collect, "YYYY-MM".
 * @return One reference per device, ordered by device id.
 */
std::vector<EventFileRef> latestEventFiles(std::vector<std::string> const& remote, std::string_view month);

}
