#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <filesystem>

#include "core/domain/sync_partition.h"
#include "core/domain/sync_round.h"
#include "storage_duty.h"
#include "sync_transport.h"
#include "sync/gist_client.h"
#include "sync/github_auth.h"
#include "sync/sync_config.h"

class QNetworkAccessManager;

/**
 * @file sync_duty.h
 * @brief Signs the reader in with GitHub and keeps the encrypted cloud copy in step with this device
 *        (docs/adr/0042).
 *
 * The round itself -- the order of the requests, the 401's renewal, the backoff and the run a change
 * during one asks for -- lives in core/domain/sync_round.h, Qt-free and answerable offline. This is
 * the adapter: the QObject the settings page reads, the timers, and the sign-in flow, which is not a
 * sync round. Nothing starts on construction: sign-in is a user action, and the first sync waits for
 * a launch or a local change, so a surface test with no token never reaches the network.
 */

namespace lens::app {

class SyncDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.syncDuty")
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString login READ login NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY stateChanged)
    Q_PROPERTY(QString lastSyncedAt READ lastSyncedAt NOTIFY stateChanged)
    Q_PROPERTY(bool hasPassphrase READ hasPassphrase NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    /**
     * @brief Bind the sync to the settings document and read the OAuth App's client id.
     * @param storage The document owner; the `sync` object is this duty's section of it.
     * @param dataDir Directory `sync.json` is read from, the same one `update.json` is.
     * @param manager Network manager to post through, or null to make one; a test supplies a scripted one.
     */
    SyncDuty(StorageDuty& storage, const std::filesystem::path& dataDir, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @return "unconfigured", "signedOut", "codePending", "signedIn" or "syncing".
    QString state() const;
    QString login() const;
    /// @return The code the reader types on github.com, while sign-in is waiting for it; otherwise empty.
    QString userCode() const;
    QString verificationUri() const;
    /// @return The local minute of the last successful sync, in util::kMinuteFormat, or empty.
    QString lastSyncedAt() const;
    /// @return Whether a derived key is stored on this device.
    bool hasPassphrase() const;
    /// @return "", "proxy", "passphrase", "generic", "expired", "denied", "signInExpired" or "noGist".
    QString error() const;

    /// @brief Ask GitHub for a device code and wait for the reader to enter it.
    Q_INVOKABLE void startLogin();
    /// @brief Stop waiting for the device code. Nothing is reported.
    Q_INVOKABLE void cancelLogin();
    /// @brief Derive the key from the passphrase and sync. An existing cloud copy must open with it.
    Q_INVOKABLE void setPassphrase(QString passphrase);
    /// @brief Sync now, with the stored key.
    Q_INVOKABLE void syncNow();
    /// @brief Forget the token and key on this device. The cloud copy is left where it is.
    Q_INVOKABLE void logOut();
    /// @brief Delete the cloud copy. The token stays, so the reader can sync again.
    Q_INVOKABLE void deleteCloudData();

    /// @brief Run one sync a few seconds after launch, when signed in and a key is stored.
    void startAtLaunch();
    /// @brief A local change was made: sync after a short quiet period, if what a sync carries has
    ///        changed since the last one. Ignored while a sync is writing remote state into the stores,
    ///        so a sync does not schedule the next.
    void scheduleSync();
    /// @brief Pull and merge the cloud copy on a timer, so another device's changes arrive without a
    ///        local action. Started once the reader is signed in with a key; idempotent.
    void startPolling();

signals:
    void stateChanged();
    void errorChanged();
    /// @brief A sync wrote remote state into this device's stores. Emitted inside the apply, so the
    ///        listeners' save-driven schedule is ignored and the surfaces redraw.
    void remoteApplied();

private:
    /// The sign-in flow's phase; a sync round's own phase is SyncRound's.
    enum class Phase { Idle,
                       CodePending,
                       /// The tokens are in; the login name is being read.
                       Naming };

    /// @brief What the round tells this adapter; every hook is a member so it can reach the surfaces.
    core::sync::Hooks roundHooks();

    /// @brief Run one sync. An empty @p passphrase uses the stored key.
    /// @param asked True when the reader pressed a button; a timer passes false.
    void run(const QString& passphrase, bool asked);
    void onNamed(const QString& login);
    /// @brief The login name could not be read: report it and leave the flow.
    void onLoginGistFailed(lens::sync::GistFailure why);
    /// @brief Set an error code (empty for none) and notify.
    void setError(const QString& code);

    lens::sync::SyncConfig    config_;
    lens::sync::GithubAuth    auth_;
    lens::sync::GistClient    gist_;
    SyncTransport             transport_;
    core::sync::SyncPartition partition_;
    core::sync::SyncRound     round_;
    QTimer                    debounce_;
    QTimer                    poll_;  ///< Pulls the cloud copy while signed in.
    QTimer                    retry_; ///< Re-runs a failed automatic sync, with backoff.
    Phase                     phase_ = Phase::Idle;
    QString                   userCode_;
    QString                   verificationUri_;
    QString                   error_;
};

}
