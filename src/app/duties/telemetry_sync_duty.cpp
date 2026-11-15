/**
 * @file telemetry_sync_duty.cpp
 * @brief The event channel's network sequence behind telemetry_sync_duty.h.
 */

#include "telemetry_sync_duty.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <exception>
#include <sstream>
#include <utility>

#include "app/duties/sync_duty.h"
#include "app/duties/telemetry_duty.h"
#include "core/crypto/hash.h"
#include "core/crypto/vault.h"
#include "sync/gist_client.h"
#include "sync/webdav_client.h"
#include "util/log.h"

namespace lens::app {
namespace {

constexpr char kWebdavCollection[] = "Lens";

std::string failureName(lens::sync::GistFailure const why)
{
    switch (why) {
        case lens::sync::GistFailure::Unauthorised: return "unauthorised";
        case lens::sync::GistFailure::NotFound: return "not found";
        case lens::sync::GistFailure::Generic: return "generic";
        case lens::sync::GistFailure::Proxy: return "proxy";
    }
    return "unknown";
}

std::string failureName(lens::sync::WebDavFailure const why)
{
    switch (why) {
        case lens::sync::WebDavFailure::Unauthorised: return "unauthorised";
        case lens::sync::WebDavFailure::NotFound: return "not found";
        case lens::sync::WebDavFailure::Generic: return "generic";
        case lens::sync::WebDavFailure::Proxy: return "proxy";
    }
    return "unknown";
}

} // namespace

TelemetrySyncDuty::TelemetrySyncDuty(TelemetryDuty& telemetry, SyncDuty& sync, QNetworkAccessManager* manager, Clock clock, QObject* parent)
    : QObject(parent), telemetry_(telemetry), sync_(sync), gist_(std::make_unique<lens::sync::GistClient>(manager)), dav_(std::make_unique<lens::sync::WebDavClient>(manager)), clock_(std::move(clock))
{
    if (!clock_) {
        clock_ = [] {
            const QDate now = QDate::currentDate();
            return Now{now.toString(QStringLiteral("yyyy-MM")).toStdString(), now.toString(Qt::ISODate).toStdString()};
        };
    }

    connect(&sync_, &SyncDuty::synced, this, [this](bool manual) { run(manual); });
    connect(&sync_, &SyncDuty::cloudDeleted, this, &TelemetrySyncDuty::onCloudDeleted);
    connect(&telemetry_, &TelemetryDuty::cleared, this, [this] { run(false); });

    connect(gist_.get(), &lens::sync::GistClient::listedFiles, this, [this](quint64 callId, lens::sync::Gist const& gist) {
        if (phase_ != Phase::Listing or callId != call_)
            return;
        rawUrls_.clear();
        std::vector<std::string> remote;
        for (auto it = gist.files.cbegin(); it != gist.files.cend(); ++it) {
            remote.push_back(it.key().toStdString());
            rawUrls_[it.key().toStdString()] = it.value().rawUrl.toString().toStdString();
        }
        onListed(remote);
    });
    connect(gist_.get(), &lens::sync::GistClient::updated, this, [this](quint64 callId, lens::sync::Gist const&) {
        if (callId != call_ or (phase_ != Phase::Uploading and phase_ != Phase::Deleting))
            return;
        if (phase_ == Phase::Uploading)
            onUploaded();
        else
            onDeleted();
    });
    connect(gist_.get(), &lens::sync::GistClient::gotRawFile, this, [this](quint64 callId, QString const& content) {
        if (phase_ == Phase::Downloading and callId == call_)
            onDownloaded(content.toStdString());
    });
    connect(gist_.get(), &lens::sync::GistClient::failed, this, [this](quint64 callId, lens::sync::GistFailure why) {
        if (callId == call_ and phase_ != Phase::Idle)
            fail("a gist request failed: " + failureName(why));
    });

    connect(dav_.get(), &lens::sync::WebDavClient::listed, this, [this](quint64 callId, QStringList const& names) {
        if (phase_ != Phase::Listing or callId != call_)
            return;
        std::vector<std::string> remote;
        for (QString const& name : names)
            remote.push_back(name.toStdString());
        onListed(remote);
    });
    connect(dav_.get(), &lens::sync::WebDavClient::putDone, this, [this](quint64 callId, QString const&) {
        if (phase_ == Phase::Uploading and callId == call_)
            onUploaded();
    });
    connect(dav_.get(), &lens::sync::WebDavClient::deleted, this, [this](quint64 callId, QString const&) {
        if (phase_ == Phase::Deleting and callId == call_)
            onDeleted();
    });
    connect(dav_.get(), &lens::sync::WebDavClient::gotFile, this, [this](quint64 callId, QString const& content) {
        if (phase_ == Phase::Downloading and callId == call_)
            onDownloaded(content.toStdString());
    });
    connect(dav_.get(), &lens::sync::WebDavClient::failed, this, [this](quint64 callId, lens::sync::WebDavFailure why) {
        if (callId == call_ and phase_ != Phase::Idle)
            fail("a WebDAV request failed: " + failureName(why));
    });
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
    phase_   = Phase::Listing;
    call_    = 0;
    telemetry_.flush();

    provider_ = sync_.partition().provider();
    key_      = sync_.partition().storedKey();
    if (not key_) {
        LENS_DEBUG("telemetry sync: no derived key on this device; the event channel waits");
        finish();
        return;
    }

    if (provider_ == "webdav") {
        server_      = sync_.partition().server();
        username_    = sync_.partition().username();
        appPassword_ = sync_.partition().appPassword();
        if (server_.empty() or username_.empty() or appPassword_.empty()) {
            finish();
            return;
        }
    } else {
        token_  = sync_.partition().token();
        gistId_ = sync_.partition().gistId();
        if (token_.empty() or gistId_.empty()) {
            finish();
            return;
        }
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
    phase_ = Phase::Listing;
    if (provider_ == "webdav") {
        call_ = dav_->list(QString::fromStdString(server_), QString::fromStdString(username_), QString::fromStdString(appPassword_), QString::fromLatin1(kWebdavCollection));
        return;
    }
    call_ = gist_->listGistFiles(QString::fromStdString(token_), QString::fromStdString(gistId_));
}

void TelemetrySyncDuty::onListed(std::vector<std::string> const& remote)
{
    if (mode_ == Mode::Download) {
        downloadRefs_ = core::telemetry::latestEventFiles(remote, downloadMonth_);
        if (downloadRefs_.empty()) {
            finishDownload();
            return;
        }
        phase_         = Phase::Downloading;
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
    phase_ = Phase::Uploading;

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

    if (provider_ == "webdav") {
        call_ = dav_->put(QString::fromStdString(server_),
                          QString::fromStdString(username_),
                          QString::fromStdString(appPassword_),
                          QString::fromStdString(webdavPath(action.file)),
                          QString::fromStdString(sealed));
        return;
    }
    call_ = gist_->updateGistFiles(QString::fromStdString(token_),
                                   QString::fromStdString(gistId_),
                                   {{QString::fromStdString(action.file), QString::fromStdString(sealed)}});
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
    phase_ = Phase::Deleting;
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
    // turned into a path or a gist key to delete.
    if (not core::telemetry::parseEventFileName(file)) {
        fail("refusing a queued deletion whose name is not an event file");
        return;
    }
    if (provider_ == "webdav") {
        call_ = dav_->del(QString::fromStdString(server_),
                          QString::fromStdString(username_),
                          QString::fromStdString(appPassword_),
                          QString::fromStdString(webdavPath(file)));
        return;
    }
    call_ = gist_->updateGistFiles(QString::fromStdString(token_), QString::fromStdString(gistId_), {{QString::fromStdString(file), std::nullopt}});
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
    running_ = true;
    ok_      = true;
    phase_   = Phase::Listing;
    call_    = 0;

    provider_ = sync_.partition().provider();
    key_      = sync_.partition().storedKey();
    if (not key_) {
        finishDownload();
        return;
    }
    if (provider_ == "webdav") {
        server_      = sync_.partition().server();
        username_    = sync_.partition().username();
        appPassword_ = sync_.partition().appPassword();
        if (server_.empty() or username_.empty() or appPassword_.empty()) {
            finishDownload();
            return;
        }
    } else {
        token_  = sync_.partition().token();
        gistId_ = sync_.partition().gistId();
        if (token_.empty() or gistId_.empty()) {
            finishDownload();
            return;
        }
    }
    rawUrls_.clear();
    listRemote();
}

void TelemetrySyncDuty::sendDownload()
{
    if (downloadIndex_ >= downloadRefs_.size()) {
        finishDownload();
        return;
    }
    phase_          = Phase::Downloading;
    auto const& ref = downloadRefs_[downloadIndex_];
    if (provider_ == "webdav") {
        call_ = dav_->get(QString::fromStdString(server_),
                          QString::fromStdString(username_),
                          QString::fromStdString(appPassword_),
                          QString::fromStdString(webdavPath(ref.name)));
        return;
    }
    auto const raw = rawUrls_.find(ref.name);
    if (raw == rawUrls_.end() or raw->second.empty()) {
        fail("the gist listing carried no raw address for a month file");
        return;
    }
    call_ = gist_->getRawFile(QString::fromStdString(token_), QUrl{QString::fromStdString(raw->second)});
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
    phase_   = Phase::Idle;
    call_    = 0;
    key_.reset();
    rawUrls_.clear();

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
    if (sync_.partition().provider() != "webdav") {
        state_ = core::telemetry::syncStateFromJson(telemetry_.log().state());
        state_.uploaded.clear();
        state_.pendingDeletes.clear();
        state_.deleteAll = false;
        saveState();
        return;
    }

    server_      = sync_.partition().server();
    username_    = sync_.partition().username();
    appPassword_ = sync_.partition().appPassword();
    if (server_.empty() or username_.empty() or appPassword_.empty())
        return;

    provider_        = "webdav";
    mode_            = Mode::CloudDelete;
    running_         = true;
    ok_              = true;
    phase_           = Phase::Listing;
    call_            = 0;
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

void TelemetrySyncDuty::fail(std::string_view what)
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
    phase_   = Phase::Idle;
    call_    = 0;
    key_.reset();
    localText_.clear();
    local_.clear();
    uploads_.clear();
    uploadIndex_ = 0;
    deletes_.clear();
    deleteIndex_ = 0;
    rawUrls_.clear();

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

std::string TelemetrySyncDuty::webdavPath(std::string_view const file)
{
    return std::string(kWebdavCollection) + "/" + std::string(file);
}

}
