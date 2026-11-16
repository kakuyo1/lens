#pragma once

#include <utility>

#include "call_queue.h"
#include "core/domain/file_transport.h"

/**
 * @file file_transport_queue.h
 * @brief The round's calling discipline for month files: one request out at a time, each with an
 *        identity (docs/adr/0170, 0181).
 *
 * The queue body is CallQueue's, the same one the sync transports run the round on; a provider
 * adapter overrides dispatch and answers the client signals through it.
 */

namespace lens::app {

class FileTransportQueue : public CallQueue<core::sync::FileRequest, core::sync::FileAnswer>, public core::sync::FileTransport {
public:
    explicit FileTransportQueue(QObject* parent = nullptr)
        : CallQueue(parent)
    {}

    void send(const core::sync::FileRequest& request, Reply reply) final
    {
        enqueue(request, std::move(reply));
    }
};

}
