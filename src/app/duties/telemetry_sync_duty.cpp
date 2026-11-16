/**
 * @file telemetry_sync_duty.cpp
 * @brief The event channel's network sequence behind telemetry_sync_duty.h.
 */

#include "telemetry_sync_duty.h"

#include <QDate>
#include <QString>

#include <exception>
#include <sstream>
#include <utility>

#include "app/duties/sync_duty.h"
#include "app/duties/telemetry_duty.h"
#include "core/crypto/hash.h"
#include "core/crypto/vault.h"
#include "util/log.h"

namespace lens::app {
namespace {

std::string failureName(core::sync::FileAnswer::Failure const why)
{
    switch (why) {
        case core::sync::FileAnswer::Failure::Unauthorised: return "unauthorised";
        case core::sync::FileAnswer::Failure::NotFound: return "not found";
        case core::sync::FileAnswer::Failure::Proxy: return "proxy";
        case core::sync::FileAnswer::Failure::Generic: return "generic";
        case core::sync::FileAnswer::Failure::Unconfigured: return "not configured";
    }
    return "unknown";
}

} // namespace

TelemetrySyncDuty::TelemetrySyncDuty(TelemetryDuty& telemetry, SyncDuty& sync, Clock clock, QObject* parent)
    : QObject(parent), telemetry_(telemetry), sync_(sync), clock_(std::move(clock))
{
    if (!clock_) {
        clock_ = [] {
            const QDate now = QDate::currentDate();
            return Now{now.toString(QStringLiteral("yyyy-MM")).toStdString(), now.toString(Qt::ISODate).toStdString()};
        };
    }

    connect(&sync_, &SyncDuty::synced, this, [this](bool manual) { run(manual); });
    connect(&sync_, &SyncDuty::cloudDeleted, this, &TelemetrySyncDuty::onCloudDeleted);
    // The backend carrying this run's requests is being replaced; the answers never arrive, so end
    // the run rather than leave it in flight.
    connect(&sync_, &SyncDuty::providerChanged, this, [this] {
        if (running_)
            fail("the sync provider changed while a run was in flight");
    });
    connect(&telemetry_, &TelemetryDuty::cleared, this, [this] { run(false); });
}

TelemetrySyncDuty::~TelemetrySyncDuty() = default;

void TelemetrySyncDuty::run(bool const manual)
{
    if (running_) {
        pendingRun_    = true;
        pendingManual_ = pendingManual_ or manual;
        return;
    }
    mode_   = Mode::Sync;
    manual_ = manual;
    start();
}

void TelemetrySyncDuty::start()
{
    running_ = true;
    ok_      = true;
    telemetry_.flush();

    key_ = sync_.partition().storedKey();
    if (not key_) {
        LENS_DEBUG("telemetry sync: no derived key on this device; the event channel waits");
        finish();
        return;
    }

    localText_.clear();
    local_.clear();
    for (std::string const& month : telemetry_.log().months()) {
        std::string text = telemetry_.log().readMonthText(month);
        if (text.empty())
            continue;
        const std::string hash = core::crypto::sha1Hex(text).substr(0, 8);
        if (hash.size() != 8) {
            LENS_WARN("telemetry sync: cannot hash the month '{}'; it waits for the next run", month);
            continue;
        }
        localText_[month] = std::move(text);
        local_.push_back(core::telemetry::LocalMonth{month, hash});
    }

    const Now now = clock_();
    if (now.month.empty() or now.day.empty()) {
        fail("the local clock has no date");
        return;
    }
    currentMonth_ = now.month;
    today_        = now.day;
    state_        = core::telemetry::syncStateFromJson(telemetry_.log().state());
    listRemote();
}

void TelemetrySyncDuty::listRemote()
{
    core::sync::FileRequest request;
    request.kind = core::sync::FileRequest::Kind::List;
    files().send(request, [this](core::sync::FileAnswer const& answer) {
        if (refused(answer))
            return;
        onListed(answer.names);
    });
}

void TelemetrySyncDuty::onListed(std::vector<std::string> const& remote)
{
    if (mode_ == Mode::Download) {
        downloadRefs_ = core::telemetry::latestEventFiles(remote, downloadMonth_);
        if (downloadRefs_.empty()) {
            finishDownload();
            return;
        }
        downloadIndex_ = 0;
        sendDownload();
        return;
    }

    if (mode_ == Mode::CloudDelete) {
        deletes_.clear();
        state_.uploaded.clear();
        state_.pendingDeletes.clear();
        state_.deleteAll = true;
        for (std::string const& name : remote) {
            if (core::telemetry::parseEventFileName(name)) {
                deletes_.push_back(name);
                state_.pendingDeletes.insert(name);
            }
        }
        deleteIndex_ = 0;
        if (not saveState()) {
            fail("cannot save the cloud deletion queue");
            return;
        }
        startDeletes(false);
        return;
    }

    core::telemetry::PlanInput input{.device       = telemetry_.log().device(),
                                     .currentMonth = currentMonth_,
                                     .today        = today_,
                                     .manual       = manual_,
                                     .local        = local_,
                                     .remote       = remote,
                                     .state        = state_};
    core::telemetry::Plan      plan = core::telemetry::planEventSync(std::move(input));
    state_                          = std::move(plan.state);
    uploads_                        = std::move(plan.uploads);
    uploadIndex_                    = 0;
    deletes_                        = std::move(plan.deletes);
    deleteIndex_                    = 0;
    if (not saveState()) {
        fail("cannot save the event sync state");
        return;
    }
    if (uploads_.empty())
        startDeletes(false);
    else
        startUpload();
}

void TelemetrySyncDuty::startUpload()
{
    if (uploadIndex_ >= uploads_.size()) {
        startDeletes(true);
        return;
    }

    core::telemetry::UploadAction const& action = uploads_[uploadIndex_];
    auto const                           text   = localText_.find(action.month);
    if (text == localText_.end()) {
        fail("the month being uploaded is no longer local");
        return;
    }
    std::string sealed;
    try {
        sealed = core::vault::seal(key_->key, text->second, key_->params).dump();
    } catch (std::exception const& error) {
        fail(std::string{"cannot seal the month: "} + error.what());
        return;
    }

    core::sync::FileRequest request;
    request.kind    = core::sync::FileRequest::Kind::Put;
    request.name    = action.file;
    request.content = std::move(sealed);
    files().send(request, [this](core::sync::FileAnswer const& answer) {
        if (refused(answer))
            return;
        onUploaded();
    });
}

void TelemetrySyncDuty::onUploaded()
{
    core::telemetry::commitUpload(state_, uploads_[uploadIndex_]);
    if (not saveState()) {
        fail("cannot save the uploaded month");
        return;
    }
    ++uploadIndex_;
    startUpload();
}

void TelemetrySyncDuty::startDeletes(bool const allPending)
{
    if (allPending) {
        deletes_.assign(state_.pendingDeletes.begin(), state_.pendingDeletes.end());
        deleteIndex_ = 0;
    }
    if (deleteIndex_ >= deletes_.size()) {
        if (state_.deleteAll) {
            state_.deleteAll = false;
            if (not saveState()) {
                fail("cannot clear the cloud deletion flag");
                return;
            }
        }
        finish();
        return;
    }
    sendDelete();
}

void TelemetrySyncDuty::sendDelete()
{
    if (deleteIndex_ >= deletes_.size()) {
        finish();
        return;
    }
    std::string const& file = deletes_[deleteIndex_];
    // A state file is local and editable; only a name this channel could have written is ever
    // turned into a request to delete.
    if (not core::telemetry::parseEventFileName(file)) {
        fail("refusing a queued deletion whose name is not an event file");
        return;
    }

    core::sync::FileRequest request;
    request.kind = core::sync::FileRequest::Kind::Delete;
    request.name = file;
    files().send(request, [this](core::sync::FileAnswer const& answer) {
        if (refused(answer))
            return;
        onDeleted();
    });
}

void TelemetrySyncDuty::onDeleted()
{
    core::telemetry::commitDelete(state_, deletes_[deleteIndex_]);
    if (not saveState()) {
        fail("cannot save the deleted file");
        return;
    }
    ++deleteIndex_;
    if (deleteIndex_ >= deletes_.size() and state_.deleteAll) {
        state_.deleteAll = false;
        if (not saveState()) {
            fail("cannot clear the cloud deletion flag");
            return;
        }
    }
    sendDelete();
}

void TelemetrySyncDuty::downloadMonth(std::string month, DownloadDone done)
{
    if (month.empty() or not done) {
        return;
    }
    if (running_) {
        LENS_DEBUG("telemetry sync: a download was asked for while another run is in flight");
        done(std::move(month), {});
        return;
    }

    mode_          = Mode::Download;
    downloadMonth_ = std::move(month);
    downloadDone_  = std::move(done);
    downloadRefs_.clear();
    downloadEvents_.clear();
    downloadIndex_ = 0;
    running_       = true;
    ok_            = true;

    key_ = sync_.partition().storedKey();
    if (not key_) {
        finishDownload();
        return;
    }
    listRemote();
}

void TelemetrySyncDuty::sendDownload()
{
    if (downloadIndex_ >= downloadRefs_.size()) {
        finishDownload();
        return;
    }

    core::sync::FileRequest request;
    request.kind = core::sync::FileRequest::Kind::Get;
    request.name = downloadRefs_[downloadIndex_].name;
    files().send(request, [this](core::sync::FileAnswer const& answer) {
        if (refused(answer))
            return;
        onDownloaded(answer.content);
    });
}

void TelemetrySyncDuty::onDownloaded(std::string const& content)
{
    core::telemetry::EventFileRef const& ref      = downloadRefs_[downloadIndex_];
    nlohmann::json const                 envelope = nlohmann::json::parse(content, nullptr, false);
    if (envelope.is_discarded() or not envelope.is_object()) {
        LENS_WARN("telemetry sync: the month file '{}' is not a sealed envelope", ref.name);
    } else if (auto const plain = core::vault::open(key_->key, envelope)) {
        if (core::crypto::sha1Hex(*plain).substr(0, 8) != ref.hash) {
            LENS_WARN("telemetry sync: the month file '{}' does not carry the hash its name claims", ref.name);
        } else {
            std::istringstream lines{*plain};
            std::string        line;
            while (std::getline(lines, line)) {
                if (line.empty())
                    continue;
                if (auto event = core::telemetry::parseLine(line))
                    downloadEvents_.push_back(std::move(*event));
            }
        }
    } else {
        LENS_WARN("telemetry sync: this device's key does not open the month file '{}'", ref.name);
    }
    ++downloadIndex_;
    sendDownload();
}

void TelemetrySyncDuty::finishDownload()
{
    running_ = false;
    mode_    = Mode::None;
    key_.reset();

    auto const month  = downloadMonth_;
    auto       done   = std::move(downloadDone_);
    auto       events = std::move(downloadEvents_);
    downloadMonth_.clear();
    downloadRefs_.clear();
    downloadEvents_.clear();
    downloadIndex_ = 0;
    if (done)
        done(month, std::move(events));

    if (pendingRun_) {
        const bool manual = pendingManual_;
        pendingRun_       = false;
        pendingManual_    = false;
        run(manual);
    }
}

void TelemetrySyncDuty::onCloudDeleted()
{
    if (running_) {
        cloudDeletePending_ = true;
        return;
    }
    if (sync_.backend().eventFilesDieWithTheCopy()) {
        state_ = core::telemetry::syncStateFromJson(telemetry_.log().state());
        state_.uploaded.clear();
        state_.pendingDeletes.clear();
        state_.deleteAll = false;
        saveState();
        return;
    }

    mode_            = Mode::CloudDelete;
    running_         = true;
    ok_              = true;
    state_           = core::telemetry::syncStateFromJson(telemetry_.log().state());
    state_.deleteAll = true;
    if (not saveState()) {
        fail("cannot save the cloud deletion flag");
        return;
    }
    deletes_.clear();
    listRemote();
}

bool TelemetrySyncDuty::saveState()
{
    try {
        core::telemetry::writeSyncState(telemetry_.log().state(), state_);
        telemetry_.log().saveState();
        return true;
    } catch (std::exception const& error) {
        LENS_ERROR("telemetry sync: cannot write the event log state: {}", error.what());
        ok_ = false;
        return false;
    }
}

void TelemetrySyncDuty::fail(std::string_view const what)
{
    LENS_WARN("telemetry sync: {}", std::string(what));
    ok_ = false;
    if (mode_ == Mode::Download)
        finishDownload();
    else
        finish();
}

void TelemetrySyncDuty::finish()
{
    running_ = false;
    mode_    = Mode::None;
    key_.reset();
    localText_.clear();
    local_.clear();
    uploads_.clear();
    uploadIndex_ = 0;
    deletes_.clear();
    deleteIndex_ = 0;

    emit finished(ok_);

    if (cloudDeletePending_) {
        cloudDeletePending_ = false;
        onCloudDeleted();
        return;
    }
    if (pendingRun_) {
        const bool manual = pendingManual_;
        pendingRun_       = false;
        pendingManual_    = false;
        run(manual);
    }
}

core::sync::FileTransport& TelemetrySyncDuty::files()
{
    return sync_.backend().fileTransport();
}

bool TelemetrySyncDuty::refused(core::sync::FileAnswer const& answer)
{
    if (answer.failure == core::sync::FileAnswer::Failure::None)
        return false;
    if (answer.failure == core::sync::FileAnswer::Failure::Unconfigured) {
        // The reader has not finished signing in; nothing to report and nothing to retry.
        if (mode_ == Mode::Download)
            finishDownload();
        else
            finish();
        return true;
    }
    fail("an event file request failed: " + failureName(answer.failure));
    return true;
}

}
