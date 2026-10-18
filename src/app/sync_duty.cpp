/**
 * @file sync_duty.cpp
 * @brief The sign-in flow and the sync run, for sync_duty.h.
 */

#include "app/sync_duty.h"

#include <nlohmann/json.hpp>

#include "app/sync_state.h"
#include "core/sync_merge.h"
#include "core/sync_run.h"
#include "core/vault.h"
#include "util/log.h"

#include <QDateTime>
#include <QUuid>

#include <algorithm>
#include <optional>

namespace lens::app {

namespace vault   = core::vault;
namespace syncrun = core::syncrun;

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
        // 401 from api.github.com: the access token is no longer accepted. The device code that
        // bought it is long gone, so what expired is the sign-in, not the code -- and that is a
        // different thing to tell the reader, and a different thing to ask them to do about it.
        case lens::sync::GistFailure::Unauthorised: return QStringLiteral("signInExpired");
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
        // The refresh token GitHub was handed is itself refused: nothing is left to renew with.
        case lens::sync::AuthFailure::BadRefresh: return QStringLiteral("signInExpired");
        case lens::sync::AuthFailure::Generic: break;
    }
    return QStringLiteral("generic");
}

QString nowLocal()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/// @return The derived key and its parameters, or std::nullopt when either is missing or malformed.
std::optional<syncrun::StoredKey> storedKeyOf(const core::SettingsSection& section)
{
    const auto key        = vault::decodeBase64(section.value<std::string>("key", ""));
    const auto salt       = vault::decodeBase64(section.value<std::string>("salt", ""));
    const int  iterations = section.value<int>("iterations", 0);
    if (!key || !salt || key->size() != 32 || iterations <= 0)
        return std::nullopt;
    return syncrun::StoredKey{*key, vault::KdfParams{*salt, static_cast<unsigned>(iterations)}};
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
    connect(&auth_, &lens::sync::GithubAuth::granted, this, [this](lens::sync::Token token) {
        storeTokens(token);
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Working;
        step_  = Step::Naming;
        emit stateChanged();
        gist_.getUser(token.access);
    });
    connect(&auth_, &lens::sync::GithubAuth::refreshed, this, &SyncDuty::onRefreshed);
    connect(&auth_, &lens::sync::GithubAuth::failed, this, [this](lens::sync::AuthFailure why) {
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Idle;
        step_  = Step::None;
        // A refused refresh token is proof the stored sign-in is dead: forget it, so the page offers
        // the sign-in button instead of a Sync now that cannot work.
        if (why == lens::sync::AuthFailure::BadRefresh) {
            forgetTokens();
            resumeAfterRefresh_ = Step::None;
        }
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

QString SyncDuty::refreshToken() const
{
    return QString::fromStdString(syncSection().value<std::string>("refreshToken", ""));
}

void SyncDuty::storeTokens(lens::sync::Token const& token)
{
    auto section = syncSection();
    section.set<std::string>("token", token.access.toStdString());
    // An app whose user tokens do not expire sends no refresh token, and an empty one would make the
    // next 401 look renewable when it is not.
    if (token.refresh.isEmpty())
        section.remove("refreshToken");
    else
        section.set<std::string>("refreshToken", token.refresh.toStdString());
}

void SyncDuty::forgetTokens()
{
    auto section = syncSection();
    section.remove("token");
    section.remove("refreshToken");
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
    resumeAfterRefresh_ = Step::None;
    refreshedThisRun_   = false;

    auto section = syncSection();
    for (const char* key : {"token", "refreshToken", "login", "gistId", "key", "salt", "iterations", "settingsSnapshot", "lastSyncedAt"})
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
    refreshedThisRun_ = false;
    phase_            = Phase::Working;
    step_             = Step::Deleting;
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
    // One refresh per run, so a new run gets its own.
    refreshedThisRun_ = false;
    phase_            = Phase::Working;
    step_             = Step::Listing;
    emit stateChanged();
    gist_.listGists(token());
}

void SyncDuty::onListed(const QByteArray& body)
{
    const auto id = lens::sync::findGistId(body, QString::fromLatin1(kDescription));
    if (!id) {
        // No gist on this account yet: the merge starts from this device alone.
        process(QString(), std::string());
        return;
    }
    step_ = Step::Reading;
    gist_.getGist(token(), *id);
}

void SyncDuty::onGot(const lens::sync::Gist& gist)
{
    // The cloud copy as the gist file carries it, which is nothing at all on a gist that has no
    // file yet; openCloudCopy() reads whatever text it is handed.
    process(gist.id, gist.files.value(QString::fromLatin1(kFileName)).content.toStdString());
}

void SyncDuty::process(const QString& gistId, const std::string& envelopeText)
{
    try {
        const auto opened = syncrun::openCloudCopy({.envelopeText = envelopeText,
                                                    .passphrase   = passphrase_,
                                                    .stored       = storedKeyOf(syncSection())});
        // Written before the error is read, because a run that stopped with "generic" may have
        // derived a key that opened the cloud copy, and that key is this device's either way.
        if (opened.keyToStore) {
            auto section = syncSection();
            section.set<std::string>("key", vault::encodeBase64(opened.keyToStore->key));
            section.set<std::string>("salt", vault::encodeBase64(opened.keyToStore->params.salt));
            section.set<int>("iterations", static_cast<int>(opened.keyToStore->params.iterations));
        }
        if (!opened.error.empty() || !opened.key) {
            // A run that stopped has no key to seal under. An empty code beside a missing key is
            // something other than what the reader did, so it is reported as the generic failure.
            fail(opened.error.empty() ? "generic" : opened.error.c_str());
            return;
        }

        const std::string    device = deviceId();
        const auto           local  = syncstate::buildLocalDocument(storage_.knownStore(), storage_.statsStore(), storedSnapshot(storage_.knownStore().document()), device, QDateTime::currentMSecsSinceEpoch());
        const nlohmann::json merged = core::sync::merge(local, opened.remote, device);

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

        const auto upload = syncrun::sealUpload({.key                 = *opened.key,
                                                 .merged              = merged,
                                                 .gistId              = gistId.toStdString(),
                                                 .manual              = manual_,
                                                 .fingerprint         = pendingFingerprint_,
                                                 .uploadedFingerprint = uploadedFingerprint_});
        if (upload.command == syncrun::Upload::Command::None) {
            // Nothing to send is still a sync that worked: it clears a backoff left by an earlier failure.
            setSyncedAt();
            retryAttempt_ = 0;
            retry_.stop();
            finish();
            return;
        }

        const QMap<QString, QString> files{{QString::fromLatin1(kFileName), QString::fromStdString(upload.sealed)}};
        if (upload.command == syncrun::Upload::Command::Create) {
            step_ = Step::Creating;
            gist_.createGist(token(), QString::fromLatin1(kDescription), files);
        } else {
            step_ = Step::Writing;
            gist_.updateGist(token(), QString::fromStdString(upload.gistId), files);
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

void SyncDuty::onRefreshed(lens::sync::Token token)
{
    storeTokens(token);
    // Back to what the 401 interrupted, once: the sync re-lists (the gist id it had is still good, and
    // listing is where every run starts), the delete asks for the id it was given again.
    const Step resume   = resumeAfterRefresh_;
    resumeAfterRefresh_ = Step::None;
    phase_              = Phase::Working;
    if (resume == Step::Deleting) {
        const QString id = QString::fromStdString(syncSection().value<std::string>("gistId", ""));
        if (id.isEmpty()) {
            fail("noGist");
            return;
        }
        step_ = Step::Deleting;
        gist_.deleteGist(token.access, id);
        return;
    }
    step_ = Step::Listing;
    gist_.listGists(token.access);
}

void SyncDuty::onGistFailed(lens::sync::GistFailure why)
{
    // An access token GitHub stopped accepting. An OAuth app whose user tokens expire answers this
    // eight hours after every sign-in (docs/adr/0042); the refresh token buys the next one, and the
    // run carries on from where the 401 stopped it. ponytail: no expiry is stored and nothing is
    // renewed early -- a 401 is the trigger. Store `expires_in` and renew ahead of time if the
    // wasted round trip ever shows up.
    if (syncrun::shouldRenewSignIn(why == lens::sync::GistFailure::Unauthorised, not refreshToken().isEmpty(), refreshedThisRun_)) {
        refreshedThisRun_   = true;
        resumeAfterRefresh_ = step_ == Step::Deleting ? Step::Deleting : Step::Listing;
        // No gist call is in flight now, and the answers to the ones that were are not wanted.
        step_ = Step::None;
        LENS_INFO("the gist call was refused; renewing the sign-in token and carrying on");
        auth_.refresh(config_.clientId, refreshToken());
        return;
    }
    // The gist is gone: forget its id, so the next sync makes a new one.
    if (why == lens::sync::GistFailure::NotFound)
        syncSection().remove("gistId");
    // A 401 that no refresh answered -- none is stored, or the one there was has been spent -- means
    // the sign-in is dead for good: forget it, so the page offers the sign-in button rather than a
    // Sync now that cannot work.
    if (why == lens::sync::GistFailure::Unauthorised) {
        LENS_WARN("the GitHub sign-in is no longer accepted and cannot be renewed; the reader has to sign in again");
        forgetTokens();
    }
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
