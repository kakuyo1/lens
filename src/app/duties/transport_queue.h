#pragma once

#include <utility>

#include "call_queue.h"
#include "core/domain/sync_round.h"

/**
 * @file transport_queue.h
 * @brief The queue every sync transport shares: one request out at a time, each with an identity.
 *
 * The round asks for one request at a time, but a sign-out can abandon a run mid-call and a new
 * request then waits behind the old one. Each queued request is stamped with a call id, the client
 * call carries it, and its answer carries it back: an answer that names another id is stale and is
 * dropped, whichever kind it is. This replaces the kind-only match both transports used to make,
 * which could not tell a round's List probe from the backend's own reachability probe on the same
 * client (docs/adr/0170). The queue's body is CallQueue's, shared with the event channel's file
 * queue (docs/adr/0181).
 */

namespace lens::app {

class TransportQueue : public CallQueue<core::sync::Request, core::sync::Answer>, public core::sync::Transport {
public:
    explicit TransportQueue(QObject* parent = nullptr)
        : CallQueue(parent)
    {}

    void send(const core::sync::Request& request, Reply reply) final
    {
        enqueue(request, std::move(reply));
    }
};

}
