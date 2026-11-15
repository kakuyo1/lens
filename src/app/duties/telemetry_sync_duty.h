#pragma once

#include <QObject>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/domain/sync_run.h"
#include "core/telemetry/event.h"
#include "core/telemetry/sync_plan.h"

/**
 * @file telemetry_sync_duty.h
 * @brief The event channel over the same cloud the learning document uses: upload, download and
 *        delete the encrypted month files after the main sync has succeeded (docs/adr/0180).
 *
 * The reconciliation is core/telemetry/sync_plan.h's; this is the adapter that reads the local
 * months, seals them with the same derived key, talks to the provider's client, and keeps the
 * upload bookkeeping in the event log's state document. A failure is logged and leaves the state
 * as it was; it never changes the main sync's error or retry behaviour.
 */

class QNetworkAccessManager;

namespace lens::sync {
class GistClient;
class WebDavClient;
}

namespace lens::app {

class SyncDuty;
class TelemetryDuty;

class TelemetrySyncDuty final : public QObject {
    Q_OBJECT
public:
    /// @brief The local month and date one run stamps its uploads with.
    struct Now {
        std::string month;
        std::string day;
    };
    using Clock        = std::function<Now()>;
    using DownloadDone = std::function<void(std::string month, std::vector<core::telemetry::Event> events)>;

    /**
     * @brief Bind the channel to the log, the sync credentials and the network.
     *
     * @param telemetry The behavior log whose months and state this channel carries.
     * @param sync      The main sync; its success and its cloud deletion are this channel's timing.
     * @param manager   Network manager to post through, or null to make one per client.
     * @param clock     The local month and date; empty uses the system clock.
     */
    TelemetrySyncDuty(TelemetryDuty& telemetry, SyncDuty& sync, QNetworkAccessManager* manager = nullptr, Clock clock = {}, QObject* parent = nullptr);
    ~TelemetrySyncDuty() override;

    /**
     * @brief Run one reconciling sync: list, upload, delete.
     *
     * @param manual True when the reader asked for the main sync that triggered this one; the
     *               current month's once-a-day cap is then ignored.
     * @note A run in flight books one more; the channel never overlaps two reconciliations.
     */
    void run(bool manual = false);

    /**
     * @brief Download every device's newest copy of @p month and open it in memory.
     *
     * @param month The month, "YYYY-MM".
     * @param done  Receives the events, or empty when nothing could be read. A run in flight answers
     *              empty immediately; the caller should ask again when the channel is idle.
     */
    void downloadMonth(std::string month, DownloadDone done);

signals:
    /// @brief A reconciling run ended; false when a request or a state write failed.
    void finished(bool ok);

private:
    enum class Mode : std::uint8_t { None,
                                     Sync,
                                     Download,
                                     CloudDelete };
    enum class Phase : std::uint8_t { Idle,
                                      Listing,
                                      Uploading,
                                      Deleting,
                                      Downloading };

    /// @brief Read the credentials and local months, then list the cloud's event files.
    void start();
    /// @brief List the cloud's event files for the mode in flight.
    void listRemote();
    /// @brief Apply the plan to a remote list, or start the download selection.
    void onListed(std::vector<std::string> const& remote);

    /// @brief Seal and send the next upload.
    void startUpload();
    void onUploaded();
    /// @brief Delete the files the plan marked safe, or every file the state has queued after the
    ///        uploads landed.
    /// @param allPending True when every queued file may go now, not just the plan's safe set.
    void startDeletes(bool allPending);
    void sendDelete();
    void onDeleted();

    /// @brief Pick each device's newest copy of the month and fetch the first.
    void startDownload();
    void sendDownload();
    void onDownloaded(std::string const& content);
    void finishDownload();

    /// @brief Remove every event file after the main cloud copy was deleted, or forget them on GitHub.
    void onCloudDeleted();

    /// @brief Write the event channel's state; false when the write failed.
    [[nodiscard]] bool saveState();
    /// @brief Log @p what and end the mode in flight without a user-facing error.
    void fail(std::string_view what);
    /// @brief End the reconciling run, then start the one a new trigger asked for.
    void finish();

    /// @return The path a WebDAV file lives at under the collection.
    static std::string webdavPath(std::string_view file);

    TelemetryDuty&                            telemetry_;
    SyncDuty&                                 sync_;
    std::unique_ptr<lens::sync::GistClient>   gist_;
    std::unique_ptr<lens::sync::WebDavClient> dav_;
    Clock                                     clock_;

    Mode  mode_          = Mode::None;
    Phase phase_         = Phase::Idle;
    bool  running_       = false;
    bool  pendingRun_    = false;
    bool  pendingManual_ = false;
    bool  manual_        = false;
    bool  ok_            = true;
    /// A cloud deletion asked for while a run was in flight; run it once that one ends.
    bool cloudDeletePending_ = false;

    std::string                             provider_;
    std::string                             token_;
    std::string                             gistId_;
    std::string                             server_;
    std::string                             username_;
    std::string                             appPassword_;
    std::optional<core::syncrun::StoredKey> key_;

    std::string                                currentMonth_;
    std::string                                today_;
    std::map<std::string, std::string>         localText_;
    std::vector<core::telemetry::LocalMonth>   local_;
    core::telemetry::SyncState                 state_;
    std::vector<core::telemetry::UploadAction> uploads_;
    std::size_t                                uploadIndex_ = 0;
    std::vector<std::string>                   deletes_;
    std::size_t                                deleteIndex_ = 0;

    /// The raw addresses a GitHub listing returned, by file name; a download reads through them.
    std::map<std::string, std::string>         rawUrls_;
    std::string                                downloadMonth_;
    DownloadDone                               downloadDone_;
    std::vector<core::telemetry::EventFileRef> downloadRefs_;
    std::size_t                                downloadIndex_ = 0;
    std::vector<core::telemetry::Event>        downloadEvents_;

    /// The call the request in flight was given; an answer with another id is not this run's.
    std::uint64_t call_ = 0;
};

}
