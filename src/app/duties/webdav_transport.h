#pragma once

#include <cstdint>

#include "core/domain/sync_round.h"
#include "transport_queue.h"

namespace lens::sync {
class WebDavClient;
}

namespace lens::app {

/**
 * @file webdav_transport.h
 * @brief The sync round's network on WebDAV: one envelope file read and written over the client.
 *
 * The queue and the call identity are TransportQueue's; this maps each request onto WebDavClient and
 * hands the answer back as a plain value. The WebDAV credentials are read from the partition each
 * time, so a form edit takes effect on the next request. Refresh never goes out: Basic auth has
 * nothing to renew, and the round is told so at once (docs/adr/0157).
 */
class WebdavTransport final : public TransportQueue {
public:
    /**
     * @brief Bind to the client that carries the calls.
     * @param dav       The WebDAV verbs, owned by the caller.
     * @param partition The `sync` object the server address, username and app password are read from.
     */
    WebdavTransport(lens::sync::WebDavClient& dav, core::sync::SyncPartition& partition, QObject* parent = nullptr);

protected:
    void dispatch(core::sync::Request const& request, std::uint64_t& callId) override;

private:
    /// @return The WebDAV path of the envelope: the collection over the file name the request names.
    static std::string envelopePath(const std::string& fileName);

    lens::sync::WebDavClient&  dav_;
    core::sync::SyncPartition& partition_;
};

}
