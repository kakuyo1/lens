#pragma once

#include <QObject>
#include <QString>

#include <filesystem>
#include <memory>
#include <string_view>

#include "core/domain/sync_round.h"

class QNetworkAccessManager;

/**
 * @file sync_backend.h
 * @brief The provider seam behind the sync duty: one storage backend's transport and sign-in flow
 *        (docs/adr/0156).
 *
 * The round is provider-agnostic: it asks Transport for List, Read, Create, Update, Delete and
 * Refresh, and merges below the seam. What a provider decides is the transport that carries those
 * requests and the sign-in flow that produces the credentials. GitHub Gist is the first backend
 * (docs/adr/0042); a second one stands beside it without the duty changing.
 */

namespace lens::app {

/// @brief Where a backend's sign-in flow stands; the duty composes it into the page's state string.
enum class SignInPhase {
    Idle,     ///< Nothing is going on.
    Pending,  ///< Waiting for the reader to act; GitHub's device code is being entered.
    Finishing ///< Credentials are in and the backend is completing the sign-in; GitHub is reading the login.
};

/**
 * @brief One sync storage backend: the transport the round runs on, plus the sign-in flow.
 *
 * The duty owns the round, the timers and the page's properties. The backend owns everything a
 * provider decides: its clients, its credential persistence through the partition it is given, and
 * its failures, which arrive as the page's error codes.
 */
class SyncBackend : public QObject {
    Q_OBJECT

public:
    explicit SyncBackend(QObject* parent = nullptr);

    /// @return The provider id this backend answers for, e.g. "github".
    [[nodiscard]] virtual QString id() const = 0;
    /// @return Whether the backend has what it needs to try anything at all; false is the page's "unconfigured".
    [[nodiscard]] virtual bool configured() const = 0;
    /// @return Whether this device holds credentials that can run a round: the page's signed-in state.
    ///         What "credentials" are is the backend's to say -- a GitHub token pair, or a WebDAV
    ///         server, username and app password.
    [[nodiscard]] virtual bool signedIn() const = 0;
    /// @return The network the sync round runs on; alive for as long as the backend is.
    [[nodiscard]] virtual core::sync::Transport& transport()         = 0;
    [[nodiscard]] virtual SignInPhase            signInPhase() const = 0;
    /// @return What the page shows while the sign-in waits for the reader; empty when nothing waits.
    [[nodiscard]] virtual QString userCode() const        = 0;
    [[nodiscard]] virtual QString verificationUri() const = 0;

    /// @brief Begin the sign-in flow. A no-op when not configured or a sign-in is under way.
    virtual void startLogin() = 0;
    /// @brief Stop waiting for the reader. Nothing is reported.
    virtual void cancelLogin() = 0;
    /// @brief Abandon any sign-in in flight, whatever its phase, and say nothing; a sign-out uses this.
    virtual void abortSignIn() = 0;

signals:
    /// @brief The phase or the display data changed; the page should re-read its state.
    void changed();
    /// @brief A sign-in failure, already mapped to a page error code ("proxy", "expired", ...).
    void failed(const QString& code);
    /// @brief The sign-in finished: credentials are stored and the identity is resolved.
    void completed();
};

/**
 * @brief Make the backend the `sync` section's provider names.
 * @param provider  The stored value; an unknown one warns and falls back to the default, so an old
 *                  document or a hand-edited one never leaves the duty without a backend.
 * @param dataDir   Directory `sync.json` is read from, the same one `update.json` is.
 * @param manager   Network manager to post through, or null to make one; a test supplies a scripted one.
 */
[[nodiscard]] std::unique_ptr<SyncBackend> makeSyncBackend(std::string_view provider, const std::filesystem::path& dataDir, QNetworkAccessManager* manager, core::sync::SyncPartition& partition, QObject* parent = nullptr);

}
