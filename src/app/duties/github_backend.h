#pragma once

#include <QObject>
#include <QString>

#include <filesystem>

#include "app/duties/sync_backend.h"
#include "app/duties/sync_transport.h"
#include "core/domain/sync_partition.h"
#include "sync/gist_client.h"
#include "sync/github_auth.h"
#include "sync/sync_config.h"

class QNetworkAccessManager;

/**
 * @file github_backend.h
 * @brief The GitHub backend: the encrypted cloud copy is one secret gist and sign-in is the device
 *        flow (docs/adr/0042).
 *
 * Everything the round cannot decide for itself is GitHub's here: the transport that maps the
 * round's requests onto gist calls, the device code the reader types on github.com, the login read
 * back from the fresh token, and the failures mapped to the page's error codes. Moved out of
 * SyncDuty so a second backend can stand beside it (docs/adr/0156).
 */

namespace lens::app {

class GithubBackend final : public SyncBackend {
    Q_OBJECT

public:
    /**
     * @brief Bind to the settings document and read the OAuth App's client id.
     * @param dataDir   Directory `sync.json` is read from, the same one `update.json` is.
     * @param manager   Network manager to post through, or null to make one; a test supplies a scripted one.
     * @param partition The `sync` object the tokens and the login are stored into.
     */
    GithubBackend(const std::filesystem::path& dataDir, QNetworkAccessManager* manager, core::sync::SyncPartition& partition, QObject* parent = nullptr);

    QString                id() const override;
    bool                   configured() const override;
    core::sync::Transport& transport() override;
    SignInPhase            signInPhase() const override;
    QString                userCode() const override;
    QString                verificationUri() const override;

    void startLogin() override;
    void cancelLogin() override;
    void abortSignIn() override;

private:
    /// The sign-in flow's phase; a sync round's own phase is SyncRound's.
    enum class Phase { Idle,
                       CodePending,
                       /// The tokens are in; the login name is being read.
                       Naming };

    void onNamed(const QString& loginName);
    /// @brief The login name could not be read: end the flow and report it.
    void onLoginGistFailed(lens::sync::GistFailure why);

    lens::sync::SyncConfig     config_;
    lens::sync::GithubAuth     auth_;
    lens::sync::GistClient     gist_;
    SyncTransport              transport_;
    core::sync::SyncPartition& partition_;
    Phase                      phase_ = Phase::Idle;
    QString                    userCode_;
    QString                    verificationUri_;
};

}
