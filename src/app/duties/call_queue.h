#pragma once

#include <QObject>

#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

/**
 * @file call_queue.h
 * @brief One request out at a time, each stamped with an identity -- the body every transport
 *        queue shares (docs/adr/0170).
 *
 * The queue is the seam's calling discipline, not a provider's: a request waits behind the one in
 * flight, each gets an id from the client call, and its answer must carry that id back. The
 * parameter types are the request, the answer and the reply the interface names; transport_queue.h
 * and file_transport_queue.h bind them for the round's Transport and the event channel's
 * FileTransport.
 */

namespace lens::app {

template <class Req, class Ans>
class CallQueue : public QObject {
public:
    explicit CallQueue(QObject* parent = nullptr)
        : QObject(parent)
    {}

protected:
    /// @brief Put @p request behind whatever is in flight and call @p reply once its answer lands.
    void enqueue(const Req& request, std::function<void(const Ans&)> reply)
    {
        queue_.push_back(Pending{.serial = nextSerial_++, .request = request, .reply = std::move(reply)});
        pump();
    }

    /// @brief Put the request now at the front on the wire.
    /// @param callId Set to the client call's id, which its answers will carry.
    virtual void dispatch(const Req& request, std::uint64_t& callId) = 0;

    /// @brief Answer the request in flight; an answer carrying another id is stale and dropped.
    void answer(std::uint64_t callId, const Ans& answer)
    {
        if (not inFlight_ or queue_.empty())
            return;
        // A call that refuses before returning answers while the id is still unrecorded; the front
        // is then the only request the answer can belong to.
        if (queue_.front().callId != 0 and queue_.front().callId != callId)
            return;
        answerCurrent(answer);
    }

    /// @brief Answer the request being dispatched now, for a call that never reaches the wire.
    void answerCurrent(const Ans& answer)
    {
        if (not inFlight_ or queue_.empty())
            return;
        Pending pending = std::move(queue_.front());
        queue_.pop_front();
        inFlight_ = false;
        pending.reply(answer);
        pump();
    }

    /// @return The request in flight when @p callId names it, nullptr otherwise.
    [[nodiscard]] const Req* current(std::uint64_t callId) const
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
        std::uint64_t                   serial = 0;
        std::uint64_t                   callId = 0;
        Req                             request;
        std::function<void(const Ans&)> reply;
    };

    std::deque<Pending> queue_;
    std::uint64_t       nextSerial_ = 1;
    bool                inFlight_   = false;
};

}
