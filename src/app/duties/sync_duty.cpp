/**
 * @file sync_duty.cpp
 * @brief The adapter around one sync round and its backend, for sync_duty.h.
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

}

SyncDuty::SyncDuty(StorageDuty& storage, const std::filesystem::path& dataDir, QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      storage_(storage),
      dataDir_(dataDir),
      manager_(manager),
      partition_(storage.knownStore()),
      debounce_(this),
      poll_(this),
      retry_(this)
{
    rebuildBackend();

    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    connect(&debounce_, &QTimer::timeout, this, [this] { run(QString(), false); });

    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, [this] { run(QString(), false); });

    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, [this] { run(QString(), false); });
}

void SyncDuty::rebuildBackend()
{
    debounce_.stop();
    poll_.stop();
    retry_.stop();
    // The round holds the old transport by reference; it dies before the backend does.
    round_.reset();
    backend_ = makeSyncBackend(partition_.provider(), dataDir_, manager_, partition_, this);
    connectBackend();
    round_ = std::make_unique<core::sync::SyncRound>(backend_->transport(), storage_.knownStore(), storage_.statsStore(), partition_, core::sync::SyncRound::Clock{}, roundHooks());
    setError(QString());
    webdavConnected_ = false;
    emit providerChanged();
    emit webdavChanged();
    emit stateChanged();
}

void SyncDuty::connectBackend()
{
    connect(backend_.get(), &SyncBackend::changed, this, &SyncDuty::stateChanged);
    connect(backend_.get(), &SyncBackend::failed, this, [this](const QString& code) {
        // A refused probe takes the form's confirmation line back down with it.
        if (webdavConnected_) {
            webdavConnected_ = false;
            emit webdavChanged();
        }
        setError(code);
        emit stateChanged();
    });
    connect(backend_.get(), &SyncBackend::completed, this, [this] {
        // The WebDAV probe's success is the form's "Connected" line; GitHub's is the sign-in itself.
        if (provider() == QStringLiteral("webdav") && !webdavConnected_) {
            webdavConnected_ = true;
            emit webdavChanged();
        }
        emit stateChanged();
        // A change made while the name was read still has to go up; the fingerprint decides that.
        scheduleSync();
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
    if (!backend_->configured())
        return QStringLiteral("unconfigured");
    if (round_->busy() || backend_->signInPhase() == SignInPhase::Finishing)
        return QStringLiteral("syncing");
    if (backend_->signInPhase() == SignInPhase::Pending)
        return QStringLiteral("codePending");
    return backend_->signedIn() ? QStringLiteral("signedIn") : QStringLiteral("signedOut");
}

QString SyncDuty::provider() const
{
    return QString::fromStdString(partition_.provider());
}

void SyncDuty::useProvider(QString provider)
{
    if (provider.toStdString() == partition_.provider())
        return;
    partition_.setProvider(provider.toStdString());
    rebuildBackend();
}

QString SyncDuty::login() const
{
    return QString::fromStdString(partition_.login());
}

QString SyncDuty::userCode() const
{
    return backend_->userCode();
}

QString SyncDuty::verificationUri() const
{
    return backend_->verificationUri();
}

QString SyncDuty::lastSyncedAt() const
{
    return QString::fromStdString(partition_.lastSyncedAt());
}

bool SyncDuty::hasPassphrase() const
{
    return partition_.hasKey();
}

QString SyncDuty::webdavServer() const
{
    return QString::fromStdString(partition_.server());
}

QString SyncDuty::webdavUsername() const
{
    return QString::fromStdString(partition_.username());
}

bool SyncDuty::hasWebdavPassword() const
{
    return not partition_.appPassword().empty();
}

bool SyncDuty::webdavConnected() const
{
    return webdavConnected_;
}

QString SyncDuty::error() const
{
    return error_;
}

void SyncDuty::startLogin()
{
    if (!backend_->configured() || backend_->signInPhase() != SignInPhase::Idle || round_->busy())
        return;
    setError(QString());
    backend_->startLogin();
}

void SyncDuty::cancelLogin()
{
    backend_->cancelLogin();
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
    backend_->abortSignIn();
    debounce_.stop();
    poll_.stop();
    retry_.stop();
    round_->forget();
    partition_.forgetAll();
    setError(QString());
    webdavConnected_ = false;
    emit stateChanged();
    emit webdavChanged();
}

void SyncDuty::deleteCloudData()
{
    // A sign-in in flight shares the backend's clients and their failure paths; deleting waits for it.
    if (backend_->signInPhase() != SignInPhase::Idle)
        return;
    round_->removeCloud();
}

// Every form field trims what it stores: a paste that carries an invisible tab or a trailing
// newline would otherwise sit in the credential and be refused as part of it (the OCR credential
// writer trims for the same reason).

void SyncDuty::setWebdavServer(QString server)
{
    partition_.setServer(server.trimmed().toStdString());
    // The verdict answered the old credentials; a changed field takes the line back down.
    webdavConnected_ = false;
    emit webdavChanged();
    emit stateChanged();
}

void SyncDuty::setWebdavUsername(QString username)
{
    partition_.setUsername(username.trimmed().toStdString());
    webdavConnected_ = false;
    emit webdavChanged();
    emit stateChanged();
}

void SyncDuty::setWebdavAppPassword(QString appPassword)
{
    partition_.setAppPassword(appPassword.trimmed().toStdString());
    webdavConnected_ = false;
    emit webdavChanged();
    emit stateChanged();
}

void SyncDuty::startAtLaunch()
{
    if (!backend_->configured() || !backend_->signedIn() || !partition_.hasKey())
        return;
    QTimer::singleShot(kLaunchMs, this, [this] { run(QString(), false); });
    startPolling();
}

void SyncDuty::startPolling()
{
    if (!backend_->configured() || !backend_->signedIn() || !partition_.hasKey() || poll_.isActive())
        return;
    poll_.start();
}

void SyncDuty::scheduleSync()
{
    if (round_->applying() || !backend_->configured() || !backend_->signedIn() || !partition_.hasKey())
        return;
    // A change that leaves what a sync carries untouched (a hotkey, an API key, a bubble setting, or
    // a pop that the snapshot already counts) has nothing to upload, so it does not wake the timer.
    if (!round_->carriesNewContent())
        return;
    // A sign-in's last read has its own end, which schedules this; a round in flight books another.
    if (backend_->signInPhase() != SignInPhase::Idle)
        return;
    if (round_->busy()) {
        round_->requestRun();
        return;
    }
    debounce_.start();
}

void SyncDuty::run(const QString& passphrase, bool asked)
{
    if (round_->busy()) {
        round_->requestRun();
        return;
    }
    if (backend_->signInPhase() != SignInPhase::Idle)
        return;
    if (!backend_->configured() || !backend_->signedIn())
        return;
    round_->begin({passphrase.toStdString(), asked});
}

void SyncDuty::setError(const QString& code)
{
    if (error_ == code)
        return;
    error_ = code;
    emit errorChanged();
}

}
