/**
 * @file explanation_queue_test.cpp
 * @brief The explanation queue's protocol, offline: the stage between take() and complete(), and
 *        the one batch that is out at a time. What an answer is worth is app_seams_test.cpp's.
 */

#include <gtest/gtest.h>

#include "app/duties/explanation_queue.h"

using lens::app::ExplanationQueue;
using lens::app::PendingSelection;
using lens::core::CacheContext;
using lens::core::SelectionKind;

namespace {

PendingSelection wordOf(QString const& lemma)
{
    PendingSelection selection;
    selection.anchor  = QPoint{200, 400};
    selection.text    = lemma;
    selection.kind    = SelectionKind::Word;
    selection.surface = lemma;
    selection.lemma   = lemma;
    return selection;
}

const CacheContext kNow{"en", false};

}

TEST(ExplanationQueue, OneBatchIsOutAtATime)
{
    ExplanationQueue queue;
    EXPECT_EQ(queue.stage(), ExplanationQueue::Stage::Empty);

    queue.request(wordOf("bank"));
    EXPECT_EQ(queue.stage(), ExplanationQueue::Stage::Waiting);

    ASSERT_TRUE(queue.take(true, kNow).has_value());
    EXPECT_EQ(queue.stage(), ExplanationQueue::Stage::InFlight);
    EXPECT_FALSE(queue.take(true, kNow)) << "nothing leaves while an answer is still out";

    queue.complete();
    EXPECT_EQ(queue.stage(), ExplanationQueue::Stage::Empty);
    EXPECT_FALSE(queue.inFlight());
}

TEST(ExplanationQueue, ARequestBehindTheAnswerWaitsForIt)
{
    ExplanationQueue queue;
    queue.addScan({wordOf("bank")});
    ASSERT_TRUE(queue.take(true, kNow));

    queue.request(wordOf("river"));
    EXPECT_TRUE(queue.hasHeldRequest());
    EXPECT_FALSE(queue.take(true, kNow)) << "the reader's ask waits behind the batch in flight";

    queue.complete();
    auto const next = queue.take(true, kNow);
    ASSERT_TRUE(next.has_value());
    EXPECT_FALSE(next->fromScan) << "the reader's own ask goes before the word list's work";
    EXPECT_EQ(next->items.front().lemma, QStringLiteral("river"));
}

TEST(ExplanationQueue, CompleteWithNothingInFlightChangesNothing)
{
    ExplanationQueue queue;
    queue.complete();
    EXPECT_EQ(queue.stage(), ExplanationQueue::Stage::Empty);

    queue.request(wordOf("bank"));
    queue.complete();
    EXPECT_EQ(queue.stage(), ExplanationQueue::Stage::Waiting) << "a request nobody took is not spent";
}
