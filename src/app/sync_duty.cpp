/**
 * @file sync_duty.cpp
 * @brief The sign-in flow and the sync run, for sync_duty.h.
 */

#include "app/sync_duty.h"

#include "app/sync_state.h"
#include "core/sync_merge.h"
#include "core/vault.h"
#include "util/log.h"

#include <QDateTime>
#include <QUuid>

#include <algorithm>
#include <optional>

namespace lens::app {

namespace vault = core::vault;

namespace {

/// The gist's description and the file in it; both are part of the cloud format (docs/adr/0042).
constexpr auto kDescription = "lens-sync";
constexpr auto kFileName    = "lens-sync.json";
/// Quiet time after the last local change before a sync runs.
constexpr int kDebounceMs = 10000;
/// Delay after launch before the first sync, so start-up work is not competing with it.
constexpr int kLaunchMs = 5000;
/// How often a signed-in device pulls the cloud copy, so another device's changes arrive unprompted.
constexpr int kPollMs = 10 * 60 * 1000;
/// Backoff for a failed automatic sync: 30 s, 1 min, 2 min, 4 min, then 8 min for each attempt after.
/// Capped, so a sync that keeps failing (no network, no proxy) costs one request per 8 minutes.
constexpr int kRetryBaseMs = 30 * 1000;
constexpr int kRetryMaxMs  = 8 * 60 * 1000;

/// Holds the applying flag for the life of the scope, so a throw inside the apply cannot leave it set.
class ApplyScope {
public:
    explicit ApplyScope(bool& flag)
        : flag_(flag)
    {
        flag_ = true;
    }
    ~ApplyScope()
    {
        flag_ = false;
    }
    ApplyScope(const ApplyScope&)            = delete;
    ApplyScope& operator=(const ApplyScope&) = delete;

private:
    bool& flag_;
};

QString errorForGist(lens::sync::GistFailure why)
{
    switch (why) {
        case lens::sync::GistFailure::Unauthorised: return QStringLiteral("expired");
        case lens::sync::GistFailure::NotFound: return QStringLiteral("noGist");
        case lens::sync::GistFailure::Proxy: return QStringLiteral("proxy");
        case lens::sync::GistFailure::Generic: break;
    }
    return QStringLiteral("generic");
}

QString errorForAuth(lens::sync::AuthFailure why)
{
    switch (why) {
        case lens::sync::AuthFailure::Expired: return QStringLiteral("expired");
        case lens::sync::AuthFailure::Denied: return QStringLiteral("denied");
        case lens::sync::AuthFailure::Proxy: return QStringLiteral("proxy");
        case lens::sync::AuthFailure::Generic: break;
    }
    return QStringLiteral("generic");
}

QString nowLocal()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/// @return The derived key and its parameters, or std::nullopt when either is missing or malformed.
std::optional<std::pair<vault::Bytes, vault::KdfParams>> storedKeyOf(const core::SettingsSection& section)
{
    const auto key        = vault::decodeBase64(section.value<std::string>("key", ""));
    const auto salt       = vault::decodeBase64(section.value<std::string>("salt", ""));
    const int  iterations = section.value<int>("iterations", 0);
    if (!key || !salt || key->size() != 32 || iterations <= 0)
        return std::nullopt;
    return std::make_pair(*key, vault::KdfParams{*salt, static_cast<unsigned>(iterations)});
}

/// @return The settings object the last successful sync left behind, or an empty object.
nlohmann::json storedSnapshot(const nlohmann::json& document)
{
    if (!document.is_object() || !document.contains("sync") || !document.at("sync").is_object())
        return nlohmann::json::object();
    return document.at("sync").value("settingsSnapshot", nlohmann::json::object());
}

}

SyncDuty::SyncDuty(StorageDuty& storage, const std::filesystem::path& dataDir, QObject* parent)
    : QObject(parent),
      storage_(storage),
      config_(lens::sync::loadSyncConfig(dataDir / "sync.json")),
      auth_(nullptr, this),
      gist_(nullptr, this),
      debounce_(this),
      poll_(this),
      retry_(this)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    connect(&debounce_, &QTimer::timeout, this, [this] { run(QString(), false); });

    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, [this] { run(QString(), false); });

    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, [this] { run(QString(), false); });

    connect(&auth_, &lens::sync::GithubAuth::codeReady, this, [this](lens::sync::DeviceCode code) {
        userCode_        = code.userCode;
        verificationUri_ = code.verificationUri;
        emit stateChanged();
    });
    connect(&auth_, &lens::sync::GithubAuth::granted, this, [this](QString accessToken) {
        syncSection().set<std::string>("token", accessToken.toStdString());
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Working;
        step_  = Step::Naming;
        emit stateChanged();
        gist_.getUser(accessToken);
    });
    connect(&auth_, &lens::sync::GithubAuth::failed, this, [this](lens::sync::AuthFailure why) {
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Idle;
        setError(errorForAuth(why));
        emit stateChanged();
    });

    connect(&gist_, &lens::sync::GistClient::listed, this, [this](QByteArray body) {
        if (step_ == Step::Listing)
            onListed(body);
    });
    connect(&gist_, &lens::sync::GistClient::gotGist, this, [this](lens::sync::Gist gist) {
        if (step_ == Step::Reading)
            onGot(gist);
    });
    connect(&gist_, &lens::sync::GistClient::created, this, [this](lens::sync::Gist gist) {
        if (step_ == Step::Creating)
            onCreated(gist);
    });
    connect(&gist_, &lens::sync::GistClient::updated, this, [this](lens::sync::Gist) {
        if (step_ == Step::Writing)
            onWritten();
    });
    connect(&gist_, &lens::sync::GistClient::gotUser, this, [this](QString login) {
        if (step_ == Step::Naming)
            onNamed(login);
    });
    connect(&gist_, &lens::sync::GistClient::deleted, this, [this](QString) {
        if (step_ == Step::Deleting)
            onDeleted();
    });
    connect(&gist_, &lens::sync::GistClient::failed, this, [this](lens::sync::GistFailure why) {
        if (step_ != Step::None)
            onGistFailed(why);
    });
}

QString SyncDuty::state() const
{
    if (config_.clientId.isEmpty())
        return QStringLiteral("unconfigured");
    if (phase_ == Phase::Working)
        return QStringLiteral("syncing");
    if (phase_ == Phase::CodePending)
        return QStringLiteral("codePending");
    return signedIn() ? QStringLiteral("signedIn") : QStringLiteral("signedOut");
}

QString SyncDuty::login() const
{
    return QString::fromStdString(syncSection().value<std::string>("login", ""));
}

QString SyncDuty::userCode() const
{
    return userCode_;
}

QString SyncDuty::verificationUri() const
{
    return verificationUri_;
}

QString SyncDuty::lastSyncedAt() const
{
    return QString::fromStdString(syncSection().value<std::string>("lastSyncedAt", ""));
}

bool SyncDuty::hasPassphrase() const
{
    return !syncSection().value<std::string>("key", "").empty();
}

QString SyncDuty::error() const
{
    return error_;
}

core::SettingsSection SyncDuty::syncSection() const
{
    return storage_.section("sync");
}

QString SyncDuty::token() const
{
    return QString::fromStdString(syncSection().value<std::string>("token", ""));
}

bool SyncDuty::signedIn() const
{
    return !token().isEmpty();
}

std::string SyncDuty::deviceId()
{
    auto        section = syncSection();
    std::string id      = section.value<std::string>("deviceId", "");
    if (id.empty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        section.set<std::string>("deviceId", id);
    }
    return id;
}

void SyncDuty::startLogin()
{
    if (config_.clientId.isEmpty() || phase_ != Phase::Idle)
        return;
    setError(QString());
    phase_ = Phase::CodePending;
    emit stateChanged();
    auth_.requestCode(config_.clientId);
}

void SyncDuty::cancelLogin()
{
    if (phase_ != Phase::CodePending)
        return;
    auth_.cancel();
    phase_ = Phase::Idle;
    userCode_.clear();
    verificationUri_.clear();
    emit stateChanged();
}

void SyncDuty::setPassphrase(QString passphrase)
{
    if (passphrase.isEmpty()) {
        setError(QStringLiteral("passphrase"));
        return;
    }
    run(passphrase, true);
}

void SyncDuty::syncNow()
{
    run(QString(), true);
}

void SyncDuty::logOut()
{
    auth_.cancel();
    debounce_.stop();
    poll_.stop();
    retry_.stop();
    retryAttempt_ = 0;
    uploadedFingerprint_.clear();
    pendingFingerprint_.clear();
    dirty_ = false;
    passphrase_.clear();
    phase_ = Phase::Idle;
    step_  = Step::None;
    userCode_.clear();
    verificationUri_.clear();

    auto section = syncSection();
    for (const char* key : {"token", "login", "gistId", "key", "salt", "iterations", "settingsSnapshot", "lastSyncedAt"})
        section.remove(key);
    setError(QString());
    emit stateChanged();
}

void SyncDuty::deleteCloudData()
{
    if (phase_ == Phase::Working)
        return;
    const QString id = QString::fromStdString(syncSection().value<std::string>("gistId", ""));
    if (id.isEmpty()) {
        setError(QStringLiteral("noGist"));
        return;
    }
    if (!signedIn())
        return;
    setError(QString());
    phase_ = Phase::Working;
    step_  = Step::Deleting;
    emit stateChanged();
    gist_.deleteGist(token(), id);
}

void SyncDuty::startAtLaunch()
{
    if (config_.clientId.isEmpty() || !signedIn() || !hasPassphrase())
        return;
    QTimer::singleShot(kLaunchMs, this, [this] { run(QString(), false); });
    startPolling();
}

void SyncDuty::startPolling()
{
    if (config_.clientId.isEmpty() || !signedIn() || !hasPassphrase() || poll_.isActive())
        return;
    poll_.start();
}

void SyncDuty::scheduleSync()
{
    if (applying_ || config_.clientId.isEmpty() || !signedIn() || !hasPassphrase())
        return;
    // A change that leaves what a sync carries untouched (a hotkey, an API key, a bubble setting, or
    // a pop that the snapshot already counts) has nothing to upload, so it does not wake the timer.
    if (syncstate::contentFingerprint(storage_.knownStore(), storage_.statsStore()) == uploadedFingerprint_)
        return;
    if (phase_ == Phase::Working) {
        dirty_ = true;
        return;
    }
    debounce_.start();
}

void SyncDuty::run(const QString& passphrase, bool asked)
{
    if (phase_ == Phase::Working) {
        dirty_ = true;
        return;
    }
    if (config_.clientId.isEmpty() || !signedIn())
        return;
    passphrase_ = passphrase.toStdString();
    if (passphrase_.empty() && !hasPassphrase()) {
        setError(QStringLiteral("passphrase"));
        return;
    }
    manual_ = asked;
    setError(QString());
    phase_ = Phase::Working;
    step_  = Step::Listing;
    emit stateChanged();
    gist_.listGists(token());
}

void SyncDuty::onListed(const QByteArray& body)
{
    const auto id = lens::sync::findGistId(body, QString::fromLatin1(kDescription));
    if (!id) {
        // No gist on this account yet: the merge starts from this device alone.
        process(QString(), nlohmann::json(nullptr));
        return;
    }
    step_ = Step::Reading;
    gist_.getGist(token(), *id);
}

void SyncDuty::onGot(const lens::sync::Gist& gist)
{
    nlohmann::json envelope; // null: the gist has no cloud copy yet
    const QString  content = gist.files.value(QString::fromLatin1(kFileName)).content;
    if (!content.isEmpty()) {
        envelope = nlohmann::json::parse(content.toStdString(), nullptr, false);
        if (envelope.is_discarded() || !envelope.is_object()) {
            fail("generic");
            return;
        }
    }
    process(gist.id, envelope);
}

void SyncDuty::process(const QString& gistId, const nlohmann::json& envelope)
{
    try {
        const bool                 hasEnvelope = !envelope.is_null();
        vault::KdfParams           params;
        vault::Bytes               key;
        std::optional<std::string> plaintext;

        if (!passphrase_.empty()) {
            if (hasEnvelope) {
                const auto sealedWith = vault::paramsOf(envelope);
                if (!sealedWith) {
                    fail("generic");
                    return;
                }
                params    = *sealedWith;
                key       = vault::deriveKey(passphrase_, params);
                plaintext = vault::open(key, envelope);
                if (!plaintext) {
                    // A wrong passphrase; nothing is stored, so the old key (if any) stays.
                    fail("passphrase");
                    return;
                }
            } else {
                params = vault::newParams();
                key    = vault::deriveKey(passphrase_, params);
            }
            auto section = syncSection();
            section.set<std::string>("key", vault::encodeBase64(key));
            section.set<std::string>("salt", vault::encodeBase64(params.salt));
            section.set<int>("iterations", static_cast<int>(params.iterations));
        } else {
            const auto stored = storedKeyOf(syncSection());
            if (!stored) {
                fail("passphrase");
                return;
            }
            key    = stored->first;
            params = stored->second;
            if (hasEnvelope) {
                plaintext = vault::open(key, envelope);
                if (!plaintext) {
                    fail("passphrase");
                    return;
                }
            }
        }

        nlohmann::json remote = nlohmann::json::object();
        if (plaintext) {
            remote = nlohmann::json::parse(*plaintext, nullptr, false);
            if (remote.is_discarded() || !remote.is_object()) {
                fail("generic");
                return;
            }
        }

        const std::string    device = deviceId();
        const auto           local  = syncstate::buildLocalDocument(storage_.knownStore(), storage_.statsStore(), storedSnapshot(storage_.knownStore().document()), device, QDateTime::currentMSecsSinceEpoch());
        const nlohmann::json merged = core::sync::merge(local, remote, device);

        {
            ApplyScope applying(applying_);
            syncstate::applyMarks(storage_.knownStore(), merged);
            syncstate::applyHistory(storage_.statsStore(), merged);
            syncstate::applyDaily(storage_.statsStore(), merged, device);
            syncstate::applySettings(storage_.knownStore(), storage_.section(), merged);
            syncSection().setRaw("settingsSnapshot", syncstate::snapshotOf(merged.value("settings", nlohmann::json::object())));
            storage_.save();
            emit remoteApplied();
        }

        // Taken here, after the apply: what this upload carries is the state it leaves behind. A change
        // made during the run is not in it, so it still counts as new and schedules the next sync.
        pendingFingerprint_ = syncstate::contentFingerprint(storage_.knownStore(), storage_.statsStore());

        // A timer pass that found nothing new on this device has pulled and merged the cloud's changes
        // above, and has nothing to send back. Skipping the seal and the upload is what keeps the
        // ten-minute poll from re-uploading an unchanged copy. A reader's own sync always uploads.
        if (!manual_ && !gistId.isEmpty() && pendingFingerprint_ == uploadedFingerprint_) {
            // Nothing to send is still a sync that worked: it clears a backoff left by an earlier failure.
            setSyncedAt();
            retryAttempt_ = 0;
            retry_.stop();
            finish();
            return;
        }

        const std::string            body   = merged.dump();
        const nlohmann::json         sealed = vault::seal(key, body, params);
        const QMap<QString, QString> files{{QString::fromLatin1(kFileName), QString::fromStdString(sealed.dump())}};
        if (gistId.isEmpty()) {
            step_ = Step::Creating;
            gist_.createGist(token(), QString::fromLatin1(kDescription), files);
        } else {
            step_ = Step::Writing;
            gist_.updateGist(token(), gistId, files);
        }
    } catch (const std::exception& failure) {
        LENS_ERROR("sync: {}", failure.what());
        fail("generic");
    }
}

void SyncDuty::onCreated(const lens::sync::Gist& gist)
{
    syncSection().set<std::string>("gistId", gist.id.toStdString());
    onUploaded();
}

void SyncDuty::onWritten()
{
    onUploaded();
}

void SyncDuty::setSyncedAt()
{
    syncSection().set<std::string>("lastSyncedAt", nowLocal().toStdString());
}

void SyncDuty::onUploaded()
{
    setSyncedAt();
    uploadedFingerprint_ = pendingFingerprint_;
    retryAttempt_        = 0;
    retry_.stop();
    startPolling();
    finish();
}

void SyncDuty::onNamed(const QString& login)
{
    syncSection().set<std::string>("login", login.toStdString());
    finish();
}

void SyncDuty::onDeleted()
{
    auto section = syncSection();
    for (const char* key : {"gistId", "key", "salt", "iterations", "lastSyncedAt"})
        section.remove(key);
    // The cloud copy is gone, so the last upload no longer describes it: the next sync must upload
    // even when nothing on this device has changed.
    uploadedFingerprint_.clear();
    finish();
}

void SyncDuty::onGistFailed(lens::sync::GistFailure why)
{
    // The gist is gone: forget its id, so the next sync makes a new one.
    if (why == lens::sync::GistFailure::NotFound)
        syncSection().remove("gistId");
    const QString code = errorForGist(why);
    setError(code);
    endFailedRun(code);
}

void SyncDuty::fail(const char* code)
{
    const QString text = QString::fromLatin1(code);
    setError(text);
    endFailedRun(text);
}

void SyncDuty::endFailedRun(const QString& code)
{
    // Read before finish(), which ends the run: only a failure that can clear by itself is retried. A
    // wrong passphrase, an expired sign-in and a refused one need the reader, so retrying them would
    // only repeat the same error. A run the reader asked for is reported, not retried.
    const bool transient = code == QStringLiteral("generic") || code == QStringLiteral("proxy") || code == QStringLiteral("noGist");
    const bool retry     = transient && !manual_;
    finish();
    if (!retry)
        return;
    // Back off from 30 s, doubling, and stop at 8 min so a device with no network costs one request
    // per 8 minutes rather than one per 30 seconds.
    const int delay = std::min(kRetryMaxMs, kRetryBaseMs << std::min(retryAttempt_, 8));
    ++retryAttempt_;
    retry_.start(delay);
}

void SyncDuty::setError(const QString& code)
{
    if (error_ == code)
        return;
    error_ = code;
    emit errorChanged();
}

void SyncDuty::finish()
{
    passphrase_.clear();
    phase_ = Phase::Idle;
    step_  = Step::None;
    emit stateChanged();
    if (dirty_) {
        dirty_ = false;
        scheduleSync();
    }
}

}
