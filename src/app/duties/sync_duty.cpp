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
      partition_(storage.knownStore()),
      debounce_(this),
      poll_(this),
      retry_(this)
{
    backend_ = makeSyncBackend(partition_.provider(), dataDir, manager, partition_, this);
    round_   = std::make_unique<core::sync::SyncRound>(backend_->transport(), storage.knownStore(), storage.statsStore(), partition_, core::sync::SyncRound::Clock{}, roundHooks());

    connect(backend_.get(), &SyncBackend::changed, this, &SyncDuty::stateChanged);
    connect(backend_.get(), &SyncBackend::failed, this, [this](const QString& code) {
        setError(code);
        emit stateChanged();
    });
    connect(backend_.get(), &SyncBackend::completed, this, [this] {
        emit stateChanged();
        // A change made while the name was read still has to go up; the fingerprint decides that.
        scheduleSync();
    });

    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    connect(&debounce_, &QTimer::timeout, this, [this] { run(QString(), false); });

    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, [this] { run(QString(), false); });

    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, [this] { run(QString(), false); });
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
    return partition_.signedIn() ? QStringLiteral("signedIn") : QStringLiteral("signedOut");
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
    emit stateChanged();
}

void SyncDuty::deleteCloudData()
{
    // A sign-in in flight shares the backend's clients and their failure paths; deleting waits for it.
    if (backend_->signInPhase() != SignInPhase::Idle)
        return;
    round_->removeCloud();
}

void SyncDuty::startAtLaunch()
{
    if (!backend_->configured() || !partition_.signedIn() || !partition_.hasKey())
        return;
    QTimer::singleShot(kLaunchMs, this, [this] { run(QString(), false); });
    startPolling();
}

void SyncDuty::startPolling()
{
    if (!backend_->configured() || !partition_.signedIn() || !partition_.hasKey() || poll_.isActive())
        return;
    poll_.start();
}

void SyncDuty::scheduleSync()
{
    if (round_->applying() || !backend_->configured() || !partition_.signedIn() || !partition_.hasKey())
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
    if (!backend_->configured() || !partition_.signedIn())
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
