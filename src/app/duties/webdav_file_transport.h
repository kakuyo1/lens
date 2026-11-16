#pragma once

#include <QString>

#include <cstdint>

#include "core/domain/file_transport.h"
#include "file_transport_queue.h"

namespace lens::sync {
class WebDavClient;
}

namespace lens::core::sync {
class SyncPartition;
}

namespace lens::app {

/**
 * @file webdav_file_transport.h
 * @brief The event channel's month files on WebDAV: files in the collection, over the real
 *        client (docs/adr/0180, 0181).
 *
 * The queue and the call identity are FileTransportQueue's; this maps each request onto a WebDAV
 * verb and hands the answer back as a plain value. The credentials are read from the partition on
 * every dispatch, so a form edit takes effect on the next request, as the round's transport does
 * (docs/adr/0157).
 */
class WebdavFileTransport final : public FileTransportQueue {
public:
    /**
     * @brief Bind to the client that carries the calls and the document the credentials live in.
     * @param dav       The WebDAV verbs, owned by the caller; the round calls it too.
     * @param partition The `sync` object the server address, username and app password are read from.
     */
    WebdavFileTransport(lens::sync::WebDavClient& dav, core::sync::SyncPartition& partition, QObject* parent = nullptr);

protected:
    void dispatch(const core::sync::FileRequest& request, std::uint64_t& callId) override;

private:
    /// @return The collection the event files live in, as a WebDAV path segment.
    static QString collection();
    /// @return The WebDAV path of an event file: the collection over the file name.
    static QString eventPath(const std::string& name);

    lens::sync::WebDavClient&  dav_;
    core::sync::SyncPartition& partition_;
};

}
