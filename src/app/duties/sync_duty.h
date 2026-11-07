#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <filesystem>
#include <memory>

#include "app/duties/sync_backend.h"
#include "core/domain/sync_partition.h"
#include "core/domain/sync_round.h"
#include "storage_duty.h"

class QNetworkAccessManager;

/**
 * @file sync_duty.h
 * @brief Keeps the encrypted cloud copy in step with this device, over the storage backend the
 *        `sync` section's provider names (docs/adr/0042, 0156, 0157).
 *
 * The round itself -- the order of the requests, the 401's renewal, the backoff and the run a change
 * during one asks for -- lives in core/domain/sync_round.h, Qt-free and answerable offline. This is
 * the adapter: the QObject the settings page reads, the timers, and the sign-in buttons, which hand
 * to the backend the flow that produces the credentials. The provider the page picks is stored and
 * the backend is rebuilt around it; each side's credentials stay on this device until a sign-out
 * forgets them. Nothing starts on construction: sign-in is a user action, and the first sync waits
 * for a launch or a local change, so a surface test with no token never reaches the network.
 */

namespace lens::app {

class SyncDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.syncDuty")
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString provider READ provider NOTIFY providerChanged)
    Q_PROPERTY(QString login READ login NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY stateChanged)
    Q_PROPERTY(QString lastSyncedAt READ lastSyncedAt NOTIFY stateChanged)
    Q_PROPERTY(bool hasPassphrase READ hasPassphrase NOTIFY stateChanged)
    Q_PROPERTY(QString webdavServer READ webdavServer NOTIFY webdavChanged)
    Q_PROPERTY(QString webdavUsername READ webdavUsername NOTIFY webdavChanged)
    Q_PROPERTY(bool hasWebdavPassword READ hasWebdavPassword NOTIFY webdavChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    /**
     * @brief Bind the sync to the settings document and make the backend the provider names.
     * @param storage The document owner; the `sync` object is this duty's section of it.
     * @param dataDir Directory `sync.json` is read from, the same one `update.json` is.
     * @param manager Network manager to post through, or null to make one; a test supplies a scripted one.
     */
    SyncDuty(StorageDuty& storage, const std::filesystem::path& dataDir, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @return "unconfigured", "signedOut", "codePending", "signedIn" or "syncing".
    QString state() const;
    /// @return "github" or "webdav"; the provider the page's picker shows.
    QString provider() const;
    QString login() const;
    /// @return The code the reader types on the backend's site, while sign-in is waiting for it; otherwise empty.
    QString userCode() const;
    QString verificationUri() const;
    /// @return The local minute of the last successful sync, in util::kMinuteFormat, or empty.
    QString lastSyncedAt() const;
    /// @return Whether a derived key is stored on this device.
    bool hasPassphrase() const;
    /// @return The WebDAV server address and username as stored; the app password only as a flag.
    QString webdavServer() const;
    QString webdavUsername() const;
    bool    hasWebdavPassword() const;
    /// @return "", "proxy", "passphrase", "generic", "expired", "denied", "signInExpired" or "noGist".
    QString error() const;

    /// @brief Store the provider the page picked and rebuild the backend around it. Both providers'
    ///        credentials stay on this device; a sign-out is what forgets them.
    Q_INVOKABLE void useProvider(QString provider);
    /// @brief Start the backend's sign-in flow; for WebDAV this is one reachability check.
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

    /// @brief Store one WebDAV form field. An empty app password clears it.
    Q_INVOKABLE void setWebdavServer(QString server);
    Q_INVOKABLE void setWebdavUsername(QString username);
    Q_INVOKABLE void setWebdavAppPassword(QString appPassword);

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
    void providerChanged();
    /// @brief A WebDAV form field was stored; the fields and the filled flag re-read.
    void webdavChanged();
    void errorChanged();
    /// @brief A sync wrote remote state into this device's stores. Emitted inside the apply, so the
    ///        listeners' save-driven schedule is ignored and the surfaces redraw.
    void remoteApplied();

private:
    /// @brief Make the backend the partition's provider names, and a round on its transport.
    void rebuildBackend();
    /// @brief Bind this adapter to the current backend's signals.
    void connectBackend();
    /// @brief What the round tells this adapter; every hook is a member so it can reach the surfaces.
    core::sync::Hooks roundHooks();

    /// @brief Run one sync. An empty @p passphrase uses the stored key.
    /// @param asked True when the reader pressed a button; a timer passes false.
    void run(const QString& passphrase, bool asked);
    /// @brief Set an error code (empty for none) and notify.
    void setError(const QString& code);

    StorageDuty&                           storage_;
    std::filesystem::path                  dataDir_;
    QNetworkAccessManager*                 manager_;
    std::unique_ptr<SyncBackend>           backend_;
    std::unique_ptr<core::sync::SyncRound> round_;
    core::sync::SyncPartition              partition_;
    QTimer                                 debounce_;
    QTimer                                 poll_;  ///< Pulls the cloud copy while signed in.
    QTimer                                 retry_; ///< Re-runs a failed automatic sync, with backoff.
    QString                                error_;
};

}
