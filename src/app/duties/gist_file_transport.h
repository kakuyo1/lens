#pragma once

#include <QMap>
#include <QUrl>

#include <cstdint>

#include "core/domain/file_transport.h"
#include "file_transport_queue.h"

namespace lens::sync {
class GistClient;
}

namespace lens::core::sync {
class SyncPartition;
}

namespace lens::app {

/**
 * @file gist_file_transport.h
 * @brief The event channel's month files on GitHub: the files of the sync gist, over the real
 *        client (docs/adr/0180, 0181).
 *
 * The queue and the call identity are FileTransportQueue's; this maps each request onto a gist
 * call and hands the answer back as a plain value. The token and the gist id are read from the
 * partition on every dispatch, so a sign-in or a rebuilt copy takes effect on the next request.
 * A month file is read back through the raw address its listing carried -- provider shape that
 * stays inside this adapter.
 */
class GistFileTransport final : public FileTransportQueue {
public:
    /**
     * @brief Bind to the client that carries the calls and the document the credentials live in.
     * @param gist      The gist requests, owned by the caller; the round calls it too.
     * @param partition The `sync` object the access token and the gist id are read from.
     */
    GistFileTransport(lens::sync::GistClient& gist, core::sync::SyncPartition& partition, QObject* parent = nullptr);

protected:
    void dispatch(const core::sync::FileRequest& request, std::uint64_t& callId) override;

private:
    lens::sync::GistClient&    gist_;
    core::sync::SyncPartition& partition_;
    /// The raw addresses the last listing carried, by file name; a Get reads through them.
    QMap<QString, QUrl> rawUrls_;
};

}
