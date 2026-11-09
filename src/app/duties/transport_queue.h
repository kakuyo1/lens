#pragma once

#include <QObject>

#include <cstdint>
#include <deque>
#include <utility>

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
 * client (docs/adr/0170).
 */

namespace lens::app {

class TransportQueue : public QObject, public core::sync::Transport {
public:
    explicit TransportQueue(QObject* parent = nullptr)
        : QObject(parent)
    {}

    void send(core::sync::Request const& request, Reply reply) final
    {
        queue_.push_back(Pending{.serial = nextSerial_++, .request = request, .reply = std::move(reply)});
        pump();
    }

protected:
    /// @brief Put the request now at the front on the wire.
    /// @param callId Set to the client call's id, which its answers will carry.
    virtual void dispatch(core::sync::Request const& request, std::uint64_t& callId) = 0;

    /// @brief Answer the request in flight; an answer carrying another id is stale and dropped.
    void answer(std::uint64_t callId, core::sync::Answer answer)
    {
        if (not inFlight_ or queue_.empty())
            return;
        // A call that refuses before returning answers while the id is still unrecorded; the front
        // is then the only request the answer can belong to.
        if (queue_.front().callId != 0 and queue_.front().callId != callId)
            return;
        answerCurrent(std::move(answer));
    }

    /// @brief Answer the request being dispatched now, for a call that never reaches the wire.
    void answerCurrent(core::sync::Answer answer)
    {
        if (not inFlight_ or queue_.empty())
            return;
        Pending pending = std::move(queue_.front());
        queue_.pop_front();
        inFlight_ = false;
        pending.reply(std::move(answer));
        pump();
    }

    /// @return The request in flight when @p callId names it, nullptr otherwise.
    [[nodiscard]] core::sync::Request const* current(std::uint64_t callId) const
    {
        if (not inFlight_ or queue_.empty())
            return nullptr;
        // The same unrecorded-id window answer() accepts: the front is the call being dispatched.
        if (queue_.front().callId != 0 and queue_.front().callId != callId)
            return nullptr;
        return &queue_.front().request;
    }

private:
    void pump()
    {
        if (inFlight_ or queue_.empty())
            return;
        inFlight_                  = true;
        const std::uint64_t serial = queue_.front().serial;
        std::uint64_t       callId = 0;
        dispatch(queue_.front().request, callId);
        // A dispatch that answered at once has moved the queue on; only the same front keeps its id.
        if (not queue_.empty() and queue_.front().serial == serial)
            queue_.front().callId = callId;
    }

    struct Pending {
        std::uint64_t       serial = 0;
        std::uint64_t       callId = 0;
        core::sync::Request request;
        Reply               reply;
    };

    std::deque<Pending> queue_;
    std::uint64_t       nextSerial_ = 1;
    bool                inFlight_   = false;
};

}
