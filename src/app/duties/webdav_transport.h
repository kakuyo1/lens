#pragma once

#include <QObject>

#include <deque>

#include "core/domain/sync_round.h"

namespace lens::sync {
class WebDavClient;
}

namespace lens::app {

/**
 * @file webdav_transport.h
 * @brief The sync round's network on WebDAV: one envelope file read and written over the client.
 *
 * The round asks for one request at a time; this maps each onto WebDavClient and hands the answer
 * back as a plain value. A request that arrives while an earlier one is still out waits in order,
 * which only happens after a sign-out abandoned a run mid-call. The WebDAV credentials are read from
 * the partition each time, so a form edit takes effect on the next request. Refresh never goes out:
 * Basic auth has nothing to renew, and the round is told so at once (docs/adr/0157).
 */
class WebdavTransport final : public QObject, public core::sync::Transport {
public:
    /**
     * @brief Bind to the client that carries the calls.
     * @param dav       The WebDAV verbs, owned by the caller.
     * @param partition The `sync` object the server address, username and app password are read from.
     */
    WebdavTransport(lens::sync::WebDavClient& dav, core::sync::SyncPartition& partition, QObject* parent = nullptr);

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
    /// @return The WebDAV path of the envelope: the collection over the file name the request names.
    static std::string envelopePath(const std::string& fileName);

    lens::sync::WebDavClient&  dav_;
    core::sync::SyncPartition& partition_;
    std::deque<Pending>        queue_;
    bool                       inFlight_ = false;
};

}
