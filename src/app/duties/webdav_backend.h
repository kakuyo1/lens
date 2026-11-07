#pragma once

#include <QObject>
#include <QString>

#include "app/duties/sync_backend.h"
#include "app/duties/webdav_transport.h"
#include "core/domain/sync_partition.h"
#include "sync/webdav_client.h"

class QNetworkAccessManager;

/**
 * @file webdav_backend.h
 * @brief The WebDAV backend: the encrypted cloud copy is one file on the reader's own server
 *        (docs/adr/0157).
 *
 * Everything the round cannot decide for itself is WebDAV's here: the transport that maps the
 * round's requests onto WebDAV verbs, the credentials read from the partition, and the one
 * reachability check that stands in for a sign-in -- there is no device code to wait for, so the
 * page's code-pending states never come up. Failures are mapped to the page's error codes.
 */

namespace lens::app {

class WebdavBackend final : public SyncBackend {
    Q_OBJECT

public:
    /**
     * @brief Bind to the settings document the credentials are read from.
     * @param partition The `sync` object the server address, username and app password live in.
     * @param manager   Network manager to post through, or null to make one; a test supplies a scripted one.
     */
    WebdavBackend(core::sync::SyncPartition& partition, QNetworkAccessManager* manager, QObject* parent = nullptr);

    QString                id() const override;
    bool                   configured() const override;
    bool                   signedIn() const override;
    core::sync::Transport& transport() override;
    SignInPhase            signInPhase() const override;
    QString                userCode() const override;
    QString                verificationUri() const override;

    void startLogin() override;
    void cancelLogin() override;
    void abortSignIn() override;

private:
    /// The only waiting this backend does: a probe of the server is out.
    enum class Phase { Idle,
                       Verifying };

    /// @brief The probe answered: end the verify one way or the other.
    void onProbed(bool reached);
    /// @brief The probe stopped without an answer: report it mapped, and leave the flow.
    void onVerifyFailed(lens::sync::WebDavFailure why);

    lens::sync::WebDavClient   dav_;
    WebdavTransport            transport_;
    core::sync::SyncPartition& partition_;
    Phase                      phase_ = Phase::Idle;
};

}
