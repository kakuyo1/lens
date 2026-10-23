#pragma once

/**
 * @file explanation_queue.h
 * @brief What the reader has asked for and not yet been answered, and when an answer is theirs.
 *
 * The queue holds the decision, not the doing: which request leaves next and which answer still
 * belongs to anything; the duty sends it, stores it and draws it. It was split out of
 * ExplanationDuty's private members because nothing behind them could be asserted -- reaching any
 * of it meant a real CostDuty around a real StatsStore, a faked QNetworkReply and an event loop.
 * Everything the queue needs from the outside arrives as an argument -- the budget's verdict and
 * the context of the moment -- so this holds no reference to a duty, a store or a client, and a
 * case can put every branch of the rule on the table without a network, a document or a clock.
 */

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <optional>
#include <utility>

#include "capture_duty.h"
#include "core/document/explanation_cache.h"

namespace lens::app {

/// @brief The next thing to ask the model, with the context the reader's settings imply for it.
struct ExplanationBatch {
    /// @brief The items to ask about, in the order they should be sent. Never empty: a batch that
    ///        would carry nothing to ask about is not a batch.
    QVector<PendingSelection> items;
    /// @brief The language and sense mode the request goes out under, captured now so that an
    ///        answer can be told apart from the settings having moved while it was away.
    core::CacheContext context;
    /// @brief Whether the request asked for an example sentence, captured when it went out. Read back
    ///        at the answer so that an example already paid for is stored even if the setting was
    ///        turned off while the request was away (docs/adr/0033).
    bool example = false;
    /// @brief True when the word list filled this in on its own rather than the reader asking.
    bool fromScan = false;
};

/// @brief What the answer to the batch in flight is good for.
struct ExplanationSettlement {
    /// @brief The batch the answer belongs to, exactly as take() handed it out: what an answer is
    ///        stored against, and under which context.
    ExplanationBatch batch;
    /// @brief For each item of the batch in flight, the index in the answer that belongs to it, or
    ///        -1 when none does -- an answer for a word the reader has since deleted is stored
    ///        against nothing. Parallel to ExplanationBatch::items throughout.
    QVector<int> resultFor;
    /// @brief The item the answer is shown about, or nothing when it is not the reader's any more.
    ///
    /// Held apart from @c resultFor on purpose: the reader moving on takes the answer off the
    /// screen and leaves it every bit as worth storing -- the request was sent either way.
    std::optional<PendingSelection> subject;
};

/**
 * @brief The requests waiting, the batch in flight, and the answer that is still the reader's.
 *
 * A caller drives it in three moves: take() a batch, send it, and when the answer arrives read
 * settled() or refusalSubject() -- then complete(). Nothing is handed out between a take() and its
 * complete(), which is what keeps a second request out of the air while the first is still landing.
 */
class ExplanationQueue {
public:
    /// @brief The cap on one scan batch, so a scan tick cannot turn into an unbounded request.
    static constexpr int kMaximumScanBatch = 5;

    /// @brief The reader's selection moved on: another was taken, or this one was acted on.
    ///
    /// This is what makes an answer on its way no longer the reader's. A selection that is copied
    /// rather than explained counts too -- the reader has said what they wanted with it.
    void selectionChanged()
    {
        ++selectionGeneration_;
    }

    /// @brief A request the reader made, already known not to be in the cache.
    /// @param selection The word, sentence or entity the reader asked about.
    /// @note The latest one taken wins: a reader who points at three words in a second meant the
    ///       third.
    void request(PendingSelection selection)
    {
        heldRequest_ = std::move(selection);
    }

    /// @brief Admit candidates the word list filtered and the cache did not answer.
    /// @param candidates The words to queue, in the order the word list met them.
    /// @note A word already queued is not queued twice, compared as written.
    void addScan(QVector<PendingSelection> candidates)
    {
        for (auto& candidate : candidates) {
            const auto queued = std::any_of(scanQueue_.cbegin(), scanQueue_.cend(), [&](PendingSelection const& waiting) {
                return waiting.lemma == candidate.lemma;
            });
            if (not queued)
                scanQueue_.push_back(std::move(candidate));
        }
    }

    /// @brief A word deleted locally: it leaves the queue and no answer for it is ever stored.
    /// @param lemma The word as the reader wrote it; must not be empty.
    /// @note Removal is compared without case -- the reader deleted the word, not a spelling of it
    ///       -- while what is remembered is the spelling the deletion arrived with.
    void invalidate(QString const& lemma)
    {
        invalidated_.insert(lemma);
        scanQueue_.erase(std::remove_if(scanQueue_.begin(), scanQueue_.end(), [&](PendingSelection const& item) {
                             return QString::compare(item.lemma, lemma, Qt::CaseInsensitive) == 0;
                         }),
                         scanQueue_.end());
        if (heldRequest_ and QString::compare(heldRequest_->lemma, lemma, Qt::CaseInsensitive) == 0)
            heldRequest_.reset();
        ++selectionGeneration_;
    }

    /// @return Whether an answer is on its way that this queue is waiting for. A failed request to
    ///         fetch the model list is not one, and must not be reported as an explanation that did
    ///         not arrive.
    bool inFlight() const
    {
        return sent_.has_value();
    }

    /**
     * @brief The next request to send, if there is one and one may be sent.
     * @param maySend Whether the budget lets a request go out at all this moment.
     * @param now     The language and sense mode in force, which become the batch's context.
     * @param example Whether the example setting is on, which the batch is asked under.
     * @return Nothing while an answer is still outstanding, or when there is nothing to send, or
     *         when @p maySend is false -- a request the reader made and the budget then ran out
     *         under is not thrown away, it waits the way the word list's own work does.
     * @note Taking a batch makes it the batch in flight: call complete() once its answer has been
     *       disposed of, or nothing will ever leave again.
     */
    std::optional<ExplanationBatch> take(bool maySend, core::CacheContext const& now, bool example = false)
    {
        if (sent_ or not maySend)
            return {};
        if (heldRequest_) {
            PendingSelection asked = std::move(*heldRequest_);
            heldRequest_.reset();
            return open({std::move(asked)}, now, example, false);
        }
        if (scanQueue_.isEmpty())
            return {};

        QVector<PendingSelection> batch;
        while (not scanQueue_.isEmpty() and batch.size() < kMaximumScanBatch) {
            batch.push_back(std::move(scanQueue_.front()));
            scanQueue_.removeFirst();
        }
        return open(std::move(batch), now, example, true);
    }

    /**
     * @brief What the answer to the batch in flight is good for.
     * @param titles  The titles the answer came back with, in the order it named them.
     * @param now     The language and sense mode in force since the answer went out.
     * @return The pairing, and the item the answer belongs on screen to -- nothing when the reader
     *         has selected something else, or changed the language or the sense mode, since the
     *         request was sent. Only the showing is withheld; what the answer is worth storing
     *         does not depend on the reader still waiting for it.
     */
    ExplanationSettlement settled(QStringList const& titles, core::CacheContext const& now) const
    {
        ExplanationSettlement out;
        if (titles.isEmpty() or not sent_)
            return out;

        out.batch         = *sent_;
        const auto& items = sent_->items;
        out.resultFor.reserve(items.size());
        for (PendingSelection const& item : items) {
            // A word is named by its lemma; anything else has no lemma to be named by, so it is
            // the batch's own text that identifies it -- and such a batch is one item long.
            const auto named = std::find_if(titles.cbegin(), titles.cend(), [&](QString const& title) {
                return isWordKind(item.kind) ? QString::compare(title, item.lemma, Qt::CaseInsensitive) == 0
                                             : item.text == items.front().text;
            });
            out.resultFor.push_back(named == titles.cend() or invalidated_.contains(item.lemma)
                                        ? -1
                                        : static_cast<int>(std::distance(titles.cbegin(), named)));
        }

        if (not stillTheReaders(now))
            return out;

        if (not sent_->fromScan) {
            out.subject = items.front();
            return out;
        }
        // Five words came back at once and the card takes one: the one the first answer names.
        const auto named = std::find_if(items.cbegin(), items.cend(), [&](PendingSelection const& candidate) {
            return QString::compare(titles.front(), candidate.lemma, Qt::CaseInsensitive) == 0;
        });
        if (named != items.cend())
            out.subject = *named;
        return out;
    }

    /**
     * @brief The item the answer in flight is still the reader's to hear about, given the title
     *        it has named so far, or nothing when it is not.
     *
     * The staleness rule does not bend for a stream: an answer for a word the reader has moved on
     * from must not paint a bubble, mid-answer included. What differs from settled() is only that
     * there is no finished answer to pair yet, so the titles are whatever has arrived so far --
     * and a batch the word list filled in on its own is still waiting for the title that says
     * which of its words the card is for.
     *
     * @param title The title the answer has named so far; empty while none has.
     * @param now   The language and sense mode in force now, the same reading settled() compares.
     */
    std::optional<PendingSelection> liveSubject(QString const& title, core::CacheContext const& now) const
    {
        if (not stillTheReaders(now))
            return {};
        if (not sent_->fromScan)
            return sent_->items.front();
        if (title.isEmpty())
            return {};
        const auto named = std::find_if(sent_->items.cbegin(), sent_->items.cend(), [&](PendingSelection const& candidate) {
            return QString::compare(title, candidate.lemma, Qt::CaseInsensitive) == 0;
        });
        return named == sent_->items.cend() ? std::nullopt : std::optional<PendingSelection>{*named};
    }

    /// @return The request to report a failure about, or nothing when it is not the reader's to
    ///         hear: the word list asking on its own earns no card, and neither does a request the
    ///         reader has since moved on from.
    std::optional<PendingSelection> refusalSubject() const
    {
        if (not sent_ or sent_->fromScan or requestedGeneration_ != selectionGeneration_)
            return {};
        return sent_->items.front();
    }

    /// @brief The batch in flight has been disposed of, its answer or its failure alike.
    ///
    /// Whatever the reader deleted while it was away has been dealt with by now, so nothing about
    /// it is held against the next request for the same word.
    void complete()
    {
        if (not sent_)
            return;
        forgetInvalidations(sent_->items);
        sent_.reset();
    }

private:
    /// @return Whether the answer in flight is still the one the reader is waiting for: the same
    ///         selection, and the same language and sense mode, as when it went out.
    bool stillTheReaders(core::CacheContext const& now) const
    {
        if (not sent_ or requestedGeneration_ != selectionGeneration_)
            return false;
        // The sense mode counts only on the word channel, so the reader's "now" is masked by the
        // kind the batch went out under before the two are compared.
        return sent_->context == core::ExplanationCache::masked(now, sent_->items.front().kind);
    }

    /// @brief Make @p items the batch in flight and hand back what the caller is to send.
    /// @param items    What to ask about; must not be empty.
    /// @param now      The settings in force, from which the batch's own context is taken.
    /// @param example  Whether the example setting is on for this batch.
    /// @param fromScan Whether the word list filled this in on its own.
    /// @return The batch, as the caller is to send it.
    ExplanationBatch open(QVector<PendingSelection> items, core::CacheContext const& now, bool example, bool fromScan)
    {
        forgetInvalidations(items);
        const core::CacheContext context = core::ExplanationCache::masked(now, items.front().kind);
        requestedGeneration_             = selectionGeneration_;
        sent_                            = ExplanationBatch{std::move(items), context, example, fromScan};
        return *sent_;
    }

    /// @brief These words are no longer deleted: the request that arrived with the deletion is over.
    /// @param items The words a batch carries, at its start and again when its answer has landed.
    /// @note A word asked for again after being deleted is asked for in earnest, so the deletion is
    ///       spent where the request is made and where its answer is put away -- same rule, and
    ///       both sites need it.
    void forgetInvalidations(QVector<PendingSelection> const& items)
    {
        for (PendingSelection const& item : items)
            invalidated_.remove(item.lemma);
    }

    /// The reader's selection, as a count of how many times it has moved: an answer stamped with an
    /// older count is one the reader is no longer waiting for.
    unsigned selectionGeneration_ = 0;
    /// The count the batch in flight went out under.
    unsigned requestedGeneration_ = 0;

    QVector<PendingSelection> scanQueue_;
    /// The reader's own request, waiting for the batch in flight to land.
    std::optional<PendingSelection> heldRequest_;
    /// The batch in flight. Holding it whole is also what says one is: nothing is handed out
    /// between the take() that set it and the complete() that clears it.
    std::optional<ExplanationBatch> sent_;
    QSet<QString>                   invalidated_;
};

}
