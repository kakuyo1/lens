/**
 * @file sync_duty.cpp
 * @brief The sign-in flow and the adapter around one sync round, for sync_duty.h.
 */

#include "app/duties/sync_duty.h"

#include <chrono>

namespace lens::app {

namespace {

/// Quiet time after the last local change before a sync runs.
constexpr int kDebounceMs = 10000;
/// Delay after launch before the first sync, so start-up work is not competing with it.
constexpr int kLaunchMs = 5000;
/// How often a signed-in device pulls the cloud copy, so another device's changes arrive unprompted.
constexpr int kPollMs = 10 * 60 * 1000;

QString errorForGist(lens::sync::GistFailure why)
{
    switch (why) {
        // 401 from api.github.com: the access token is no longer accepted. The device code that
        // bought it is long gone, so what expired is the sign-in, not the code -- a different thing
        // to tell the reader and a different thing to ask them to do about it.
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

}

SyncDuty::SyncDuty(StorageDuty& storage, const std::filesystem::path& dataDir, QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      config_(lens::sync::loadSyncConfig(dataDir / "sync.json")),
      auth_(manager, this),
      gist_(manager, this),
      transport_(gist_, auth_, config_.clientId, this),
      partition_(storage.knownStore()),
      round_(transport_, storage.knownStore(), storage.statsStore(), partition_, {}, roundHooks()),
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
        if (phase_ != Phase::CodePending)
            return;
        userCode_        = code.userCode;
        verificationUri_ = code.verificationUri;
        emit stateChanged();
    });
    connect(&auth_, &lens::sync::GithubAuth::granted, this, [this](lens::sync::Token token) {
        if (phase_ != Phase::CodePending)
            return;
        partition_.storeTokens(token.access.toStdString(), token.refresh.toStdString());
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Naming;
        emit stateChanged();
        gist_.getUser(token.access);
    });
    connect(&auth_, &lens::sync::GithubAuth::failed, this, [this](lens::sync::AuthFailure why) {
        // A refresh's failure is the round's port's to report; this handler is the sign-in's.
        if (phase_ != Phase::CodePending)
            return;
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Idle;
        // A refused refresh token is proof the stored sign-in is dead: forget it, so the page offers
        // the sign-in button instead of a Sync now that cannot work.
        if (why == lens::sync::AuthFailure::BadRefresh)
            partition_.forgetTokens();
        setError(errorForAuth(why));
        emit stateChanged();
    });

    connect(&gist_, &lens::sync::GistClient::gotUser, this, [this](QString loginName) {
        if (phase_ == Phase::Naming)
            onNamed(loginName);
    });
    connect(&gist_, &lens::sync::GistClient::failed, this, [this](lens::sync::GistFailure why) {
        if (phase_ == Phase::Naming)
            onLoginGistFailed(why);
    });
}

core::sync::Hooks SyncDuty::roundHooks()
{
    core::sync::Hooks hooks;
    hooks.changed  = [this] { emit stateChanged(); };
    hooks.error    = [this](std::string code) { setError(QString::fromStdString(code)); };
    hooks.finished = [this](bool succeeded) {
        retry_.stop();
        if (succeeded)
            startPolling();
    };
    hooks.retry   = [this](std::chrono::milliseconds delay) { retry_.start(static_cast<int>(delay.count())); };
    hooks.rerun   = [this] { scheduleSync(); };
    hooks.applied = [this] { emit remoteApplied(); };
    return hooks;
}

QString SyncDuty::state() const
{
    if (config_.clientId.isEmpty())
        return QStringLiteral("unconfigured");
    if (round_.busy() || phase_ == Phase::Naming)
        return QStringLiteral("syncing");
    if (phase_ == Phase::CodePending)
        return QStringLiteral("codePending");
    return partition_.signedIn() ? QStringLiteral("signedIn") : QStringLiteral("signedOut");
}

QString SyncDuty::login() const
{
    return QString::fromStdString(partition_.login());
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
    return QString::fromStdString(partition_.lastSyncedAt());
}

bool SyncDuty::hasPassphrase() const
{
    return partition_.hasKey();
}

QString SyncDuty::error() const
{
    return error_;
}

void SyncDuty::startLogin()
{
    if (config_.clientId.isEmpty() || phase_ != Phase::Idle || round_.busy())
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
    round_.forget();
    partition_.forgetAll();
    phase_ = Phase::Idle;
    userCode_.clear();
    verificationUri_.clear();
    setError(QString());
    emit stateChanged();
}

void SyncDuty::deleteCloudData()
{
    // The login's name read uses the same gist client; a round would share its failure signal.
    if (phase_ != Phase::Idle)
        return;
    round_.removeCloud();
}

void SyncDuty::startAtLaunch()
{
    if (config_.clientId.isEmpty() || !partition_.signedIn() || !partition_.hasKey())
        return;
    QTimer::singleShot(kLaunchMs, this, [this] { run(QString(), false); });
    startPolling();
}

void SyncDuty::startPolling()
{
    if (config_.clientId.isEmpty() || !partition_.signedIn() || !partition_.hasKey() || poll_.isActive())
        return;
    poll_.start();
}

void SyncDuty::scheduleSync()
{
    if (round_.applying() || config_.clientId.isEmpty() || !partition_.signedIn() || !partition_.hasKey())
        return;
    // A change that leaves what a sync carries untouched (a hotkey, an API key, a bubble setting, or
    // a pop that the snapshot already counts) has nothing to upload, so it does not wake the timer.
    if (!round_.carriesNewContent())
        return;
    // A sign-in's last read has its own end, which schedules this; a round in flight books another.
    if (phase_ != Phase::Idle)
        return;
    if (round_.busy()) {
        round_.requestRun();
        return;
    }
    debounce_.start();
}

void SyncDuty::run(const QString& passphrase, bool asked)
{
    if (round_.busy()) {
        round_.requestRun();
        return;
    }
    if (phase_ != Phase::Idle)
        return;
    if (config_.clientId.isEmpty() || !partition_.signedIn())
        return;
    round_.begin({passphrase.toStdString(), asked});
}

void SyncDuty::onNamed(const QString& loginName)
{
    partition_.setLogin(loginName.toStdString());
    phase_ = Phase::Idle;
    emit stateChanged();
    // A change made while the name was read still has to go up; the fingerprint decides that.
    scheduleSync();
}

void SyncDuty::onLoginGistFailed(lens::sync::GistFailure why)
{
    phase_ = Phase::Idle;
    // The token a sign-in just bought is not even enough to read the login name: it is dead.
    if (why == lens::sync::GistFailure::Unauthorised)
        partition_.forgetTokens();
    setError(errorForGist(why));
    emit stateChanged();
}

void SyncDuty::setError(const QString& code)
{
    if (error_ == code)
        return;
    error_ = code;
    emit errorChanged();
}

}
