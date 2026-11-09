/**
 * @file transport_queue_test.cpp
 * @brief One request out at a time, and each answer belongs to the call that asked.
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "app/duties/transport_queue.h"

namespace {

using lens::app::TransportQueue;
using lens::core::sync::Answer;
using lens::core::sync::Request;

class FakeTransport final : public TransportQueue {
public:
    std::vector<std::uint64_t> dispatched;

    /// @brief Answer as the wire would, carrying the id the dispatch was given.
    void deliver(std::uint64_t callId)
    {
        answer(callId, Answer{});
    }

protected:
    void dispatch(Request const&, std::uint64_t& callId) override
    {
        callId = ++next_;
        dispatched.push_back(callId);
    }

private:
    std::uint64_t next_ = 0;
};

}

TEST(TransportQueue, OneRequestGoesOutAtATime)
{
    FakeTransport    queue;
    std::vector<int> answered;
    queue.send(Request{.kind = Request::Kind::List}, [&](Answer) { answered.push_back(1); });
    queue.send(Request{.kind = Request::Kind::Read}, [&](Answer) { answered.push_back(2); });
    ASSERT_EQ(queue.dispatched.size(), 1u) << "the second request waits for the first answer";

    queue.deliver(queue.dispatched[0]);
    EXPECT_EQ(answered, std::vector<int>{1});
    ASSERT_EQ(queue.dispatched.size(), 2u);

    queue.deliver(queue.dispatched[1]);
    EXPECT_EQ(answered, (std::vector<int>{1, 2}));
}

TEST(TransportQueue, AnAnswerCarryingAnotherIdIsDropped)
{
    FakeTransport queue;
    int           answered = 0;
    queue.send(Request{}, [&](Answer) { ++answered; });
    const auto callId = queue.dispatched.front();

    queue.deliver(callId + 1);
    EXPECT_EQ(answered, 0) << "an answer for another call is not this call's";
    EXPECT_EQ(queue.dispatched.size(), 1u) << "the wrong answer did not free the queue";

    queue.deliver(callId);
    EXPECT_EQ(answered, 1);
}

TEST(TransportQueue, AnAnswerAfterCompletionIsDropped)
{
    FakeTransport queue;
    int           answered = 0;
    queue.send(Request{}, [&](Answer) { ++answered; });
    const auto callId = queue.dispatched.front();

    queue.deliver(callId);
    queue.deliver(callId);
    EXPECT_EQ(answered, 1) << "a reply is delivered exactly once";
}

/// A client that refuses a request before returning its call id answers while the queue has not
/// recorded an id yet. That answer still belongs to the request being dispatched, and must not
/// leave it waiting forever.
TEST(TransportQueue, ACallThatAnswersBeforeReturningStillReachesTheFront)
{
    class ImmediateTransport final : public TransportQueue {
    protected:
        void dispatch(Request const&, std::uint64_t& callId) override
        {
            callId = 7;
            answer(7, Answer{}); // the client refused and returned only after the answer arrived
        }
    };

    ImmediateTransport queue;
    int                answered = 0;
    queue.send(Request{}, [&](Answer) { ++answered; });
    EXPECT_EQ(answered, 1);
}
