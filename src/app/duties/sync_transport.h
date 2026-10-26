#pragma once

#include <QObject>
#include <QString>

#include <deque>

#include "core/domain/sync_round.h"

namespace lens::sync {
class GistClient;
class GithubAuth;
}

namespace lens::app {

/**
 * @file sync_transport.h
 * @brief The sync round's network: gist calls and the one token refresh, over the real clients.
 *
 * The round asks for one request at a time; this maps each onto GistClient or GithubAuth and hands the
 * answer back as a plain value. A request that arrives while an earlier one is still out waits in
 * order, which only happens after a sign-out abandoned a run mid-call.
 */
class SyncTransport final : public QObject, public core::sync::Transport {
public:
    /**
     * @brief Bind to the clients that carry the calls.
     * @param gist     The gist requests, owned by the caller.
     * @param auth     The sign-in client, which also spends a refresh token.
     * @param clientId The OAuth app a refresh is asked for.
     */
    SyncTransport(lens::sync::GistClient& gist, lens::sync::GithubAuth& auth, QString clientId, QObject* parent = nullptr);

    void send(const core::sync::Request& request, Reply reply) override;

private:
    struct Pending {
        core::sync::Request request;
        Reply               reply;
    };

    /// @return Whether the request in flight is @p kind, so a signal from another call is ignored.
    [[nodiscard]] bool expecting(core::sync::Request::Kind kind) const;
    /// @brief Issue the first queued request when nothing is out.
    void pump();
    /// @brief Take the first queued request out and hand @p answer to its reply once.
    void complete(core::sync::Answer answer);

    lens::sync::GistClient& gist_;
    lens::sync::GithubAuth& auth_;
    QString                 clientId_;
    std::deque<Pending>     queue_;
    bool                    inFlight_ = false;
};

}
