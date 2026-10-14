#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

#include "storage_duty.h"
#include "sync/github_auth.h"
#include "sync/gist_client.h"
#include "sync/sync_config.h"

/**
 * @file sync_duty.h
 * @brief Signs the reader in with GitHub and keeps the encrypted cloud copy in step with this device
 *        (docs/adr/0042).
 *
 * The pure mapping lives in sync_state.h and the merge in core/sync_merge.h; this is the QObject that
 * runs the requests in order, holds the state the settings page reads, and decides when a sync is due.
 * Nothing starts on construction: sign-in is a user action, and the first sync waits for a launch or
 * a local change, so a surface test with no token never reaches the network.
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
     * @param parent  QObject parent.
     */
    SyncDuty(StorageDuty& storage, const std::filesystem::path& dataDir, QObject* parent = nullptr);

    /// @return "unconfigured", "signedOut", "codePending", "signedIn" or "syncing".
    QString state() const;
    QString login() const;
    /// @return The code the reader types on github.com, while sign-in is waiting for it; otherwise empty.
    QString userCode() const;
    QString verificationUri() const;
    /// @return Local "yyyy-MM-dd HH:mm" of the last successful sync, or empty.
    QString lastSyncedAt() const;
    /// @return Whether a derived key is stored on this device.
    bool hasPassphrase() const;
    /// @return "", "proxy", "passphrase", "generic", "expired", "denied" or "noGist".
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
    enum class Phase { Idle,
                       CodePending,
                       Working };
    /// Which request is in flight, so a reply that does not match it is dropped.
    enum class Step { None,
                      Listing,
                      Reading,
                      Creating,
                      Writing,
                      Naming,
                      Deleting };

    /// @return The `sync` object as a section of the document.
    core::SettingsSection syncSection() const;
    /// @return This device's id, made on first use.
    std::string deviceId();
    /// @return The GitHub token, or empty when the reader is not signed in.
    QString token() const;
    bool    signedIn() const;
    /// @brief Run one sync. An empty @p passphrase uses the stored key.
    /// @param asked True when the reader pressed a button; a timer passes false.
    void run(const QString& passphrase, bool asked);
    /// @brief Given the cloud envelope (null when there is none), merge and push.
    void process(const QString& gistId, const nlohmann::json& envelope);
    void onListed(const QByteArray& body);
    void onGot(const lens::sync::Gist& gist);
    void onCreated(const lens::sync::Gist& gist);
    void onWritten();
    /// @brief Record a successful upload: its fingerprint, the time, and a cleared retry count.
    void onUploaded();
    /// @brief Stamp the time of the last successful sync, which the settings page shows.
    void setSyncedAt();
    void onNamed(const QString& login);
    void onDeleted();
    void onGistFailed(lens::sync::GistFailure why);
    /// @brief Set the error and end the run.
    void fail(const char* code);
    /// @brief Set an error code (empty for none) and notify.
    void setError(const QString& code);
    /// @brief Return to Idle, notify, and run a sync that was requested while this one was busy.
    void finish();
    /// @brief End a run that failed with @p code, and retry it with backoff when it was automatic and
    ///        the failure can clear by itself. A success clears the backoff (see onUploaded).
    void endFailedRun(const QString& code);

    StorageDuty&           storage_;
    lens::sync::SyncConfig config_;
    lens::sync::GithubAuth auth_;
    lens::sync::GistClient gist_;
    QTimer                 debounce_;
    QTimer                 poll_;  ///< Pulls the cloud copy while signed in.
    QTimer                 retry_; ///< Re-runs a failed automatic sync, with backoff.
    Phase                  phase_    = Phase::Idle;
    Step                   step_     = Step::None;
    bool                   applying_ = false;
    bool                   dirty_    = false;
    /// Whether the run in flight was asked for by the reader rather than by a timer. A failed
    /// automatic run is retried; a failed run the reader asked for is reported and left alone.
    bool manual_       = false;
    int  retryAttempt_ = 0;
    /// What the last successful upload carried, as sync_state's contentFingerprint(). Empty until
    /// one has succeeded in this session, so the first sync after launch always uploads.
    std::string uploadedFingerprint_;
    /// The fingerprint of the upload in flight. It becomes uploadedFingerprint_ only when the upload
    /// succeeds, so a failed upload leaves the last good one in place and the next change still syncs.
    std::string pendingFingerprint_;
    std::string passphrase_; ///< Held only for the run that was given it; cleared when the run ends.
    QString     userCode_;
    QString     verificationUri_;
    QString     error_;
};

}
