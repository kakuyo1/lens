#pragma once

#include <QString>

#include <cstdint>

#include "core/domain/sync_round.h"
#include "transport_queue.h"

namespace lens::sync {
class GistClient;
class GithubAuth;
}

namespace lens::app {

/**
 * @file sync_transport.h
 * @brief The sync round's network: gist calls and the one token refresh, over the real clients.
 *
 * The queue and the call identity are TransportQueue's; this maps each request onto GistClient or
 * GithubAuth and hands the answer back as a plain value.
 */
class SyncTransport final : public TransportQueue {
public:
    /**
     * @brief Bind to the clients that carry the calls.
     * @param gist     The gist requests, owned by the caller.
     * @param auth     The sign-in client, which also spends a refresh token.
     * @param clientId The OAuth app a refresh is asked for.
     */
    SyncTransport(lens::sync::GistClient& gist, lens::sync::GithubAuth& auth, QString clientId, QObject* parent = nullptr);

protected:
    void dispatch(core::sync::Request const& request, std::uint64_t& callId) override;

private:
    lens::sync::GistClient& gist_;
    lens::sync::GithubAuth& auth_;
    QString                 clientId_;
};

}
