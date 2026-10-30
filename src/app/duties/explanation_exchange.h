#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

#include "app/duties/card_session.h"
#include "app/duties/explanation_queue.h"
#include "app/duties/pending_selection.h"
#include "core/document/explanation_cache.h"
#include "core/document/known_store.h"
#include "llm/llm_client.h"

/**
 * @file explanation_exchange.h
 * @brief One explanation transaction from the reader's ask to the card it raises: the cache's answer,
 *        the budget's gate, the request's shape, the batch's settlement and the card standing.
 *
 * The four doors into an explanation -- the action bar, the word list's scan, the word list's hover
 * and the pet's review answer -- each spelled "look, budget, request, store, show" for themselves,
 * and three members held the card and the request in flight. This module owns that transaction; the
 * duty is its adapter: it reads the settings, sends what this module asks for, stores what it
 * answers and draws the card it raises.
 *
 * Value in, value out: no QObject, no clock, no network. It reads the KnownStore the application
 * already holds; the LLM client, the budget duty and the event loop stay with the duty.
 */

namespace lens::app {

/// @brief The reader's settings one decision reads.
struct ExchangeFacts {
    /// @brief The language and sense mode in force now.
    core::CacheContext now;
    /// @brief Whether today's budget lets a request go out at all.
    bool mayAsk = true;
    /// @brief Whether the example sentence is asked for; a new batch captures it.
    bool example = false;
};

/// @brief Why the reader is asking. The origin decides what a stored answer is worth and what a miss
///        does: the action bar may buy, the scan only counts, the hover shows, the pet's sign is quiet.
enum class AskOrigin : std::uint8_t {
    Selection,
    Scan,
    Review,
    ReviewAnswer,
};

class ExplanationExchange {
public:
    /// @brief What the view draws for the card the exchange raised.
    struct CardView {
        PendingSelection   selection;
        llm::Explanation   answer;
        core::CacheContext context;
        CardKind           kind           = CardKind::Selection;
        bool               sameCard       = false;
        bool               countsDown     = false;
        bool               offersVerdicts = false;
        bool               reveal         = true;
        bool               notifyPet      = true;
        /// The single-sense word card's cycle after this answer, when it has one.
        std::optional<CardSession::CycleView> cycle;
    };

    /// @brief What one transaction asks the adapter to do.
    struct Outcome {
        enum class Kind : std::uint8_t { None,
                                         Card,
                                         Chain,
                                         Send,
                                         Notice } kind = Kind::None;
        CardView            card;        ///< Card
        PendingSelection    selection;   ///< Chain
        llm::Explanation    answer;      ///< Chain
        core::CacheContext  context;     ///< Chain
        llm::ExplainRequest request;     ///< Send
        ExplanationBatch    batch;       ///< Send: the items the adapter counts and saves
        QString             noticeLemma; ///< Notice: the review's miss
        /// Non-empty when the answer came out of the cache and counts as a pop, card or not.
        QString popLemma;
    };

    /// @param store The document whose cache this transaction reads and writes.
    explicit ExplanationExchange(core::KnownStore& store);

    /// @brief The reader's selection moved on: another was taken, or this one was acted on.
    void setSelection(PendingSelection selection);
    /// @brief The reader copied the selection instead of explaining it: nothing on its way is theirs.
    void                                  clearSelection();
    [[nodiscard]] PendingSelection const& selection() const
    {
        return selection_;
    }

    [[nodiscard]] core::CacheContext readerContext() const
    {
        return cache_.readerContext();
    }
    /// @return Whether an answer is on its way that this exchange is waiting for.
    [[nodiscard]] bool inFlight() const
    {
        return queue_.inFlight();
    }

    /**
     * @brief One door into an explanation, as the transaction it starts.
     * @param selection What the reader pointed at.
     * @param origin Which door it came through; the origin settles a hit and a miss.
     * @param facts The settings the decision reads.
     * @return A card or chain answer out of the cache, a notice for the hover's miss, nothing when the
     *         request waits behind the one in flight or joins the scan queue, or a queued request the
     *         adapter dispatches with take().
     */
    Outcome ask(PendingSelection selection, AskOrigin origin, ExchangeFacts const& facts);

    /// @brief The cache's whole answer for a selection, with no request and no queue change.
    Outcome stored(PendingSelection selection);

    /**
     * @brief The next queued request, as a batch to send or a card answered out of the cache first.
     * @param facts The settings and the budget as they stand now.
     * @param etymology Whether the reader wants the origin, which only a word asks for.
     */
    Outcome take(ExchangeFacts const& facts, bool etymology);

    /**
     * @brief The answer to the batch in flight: what is stored, and the card it is shown as.
     * @return The card or chain answer still the reader's; nothing when none is, after the storing.
     */
    Outcome settle(QVector<llm::Explanation> const& results, ExchangeFacts const& facts);
    /// @return The request to report a failure about, or nothing when it is not the reader's to hear.
    std::optional<PendingSelection> refuse();
    /// @return Whether the standing card was the deleted word and is now down.
    bool invalidateWord(QString const& lemma);
    /// @return The item the answer in flight is still the reader's to hear about, given the title it
    ///         has named so far; nothing when the reader moved on or the batch is the scan's and unnamed.
    std::optional<PendingSelection> liveSubject(QString const& title, core::CacheContext const& now) const;

    /**
     * @brief Raise or refresh a card whose answer is not its own transaction: a stream, a breakdown,
     *        a correction. The transaction paths raise through ask(), take() and settle().
     * @param title The title the card draws, which a chain step names its parent by.
     */
    CardView raise(PendingSelection          selection,
                   QString const&            title,
                   llm::Explanation const&   answer,
                   core::CacheContext const& context,
                   CardKind                  kind,
                   bool                      reveal,
                   bool                      notifyPet);

    /// @brief The held globe, with the free Chinese pair the cache may already hold for the card.
    CardSession::PeekStep requestPeek(QString const& mother, bool mayAsk, core::CacheContext const& now);

    [[nodiscard]] CardSession& card()
    {
        return session_;
    }
    [[nodiscard]] CardSession const& card() const
    {
        return session_;
    }

private:
    /// @brief The card a stored entry raises, or the chain answer it makes; a hit the reader has moved
    ///        on from still counts its pop.
    Outcome fromEntry(PendingSelection          selection,
                      core::WordCache const&    entry,
                      core::CacheContext const& context,
                      CardKind                  kind,
                      bool                      reveal,
                      bool                      notifyPet,
                      bool                      pop);
    /// @brief The cache's whole answer for a selection under @p context.
    Outcome storedIn(PendingSelection selection, core::CacheContext const& context);
    /// @return The request the batch goes out as: its channel from the kind, its words from its items.
    static llm::ExplainRequest requestFor(ExplanationBatch const& batch, bool etymology);

    core::ExplanationCache cache_;
    ExplanationQueue       queue_;
    CardSession            session_;
    PendingSelection       selection_;
};

}
