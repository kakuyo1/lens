#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "capture_duty.h"
#include "core/domain/pet/pet_state.h"
#include "core/document/explanation_cache.h"
#include "explanation_queue.h"
#include "llm/llm_client.h"

/**
 * @file explanation_duty.h
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 *
 * The scheduling and the staleness rule live in ExplanationQueue: which request leaves next and
 * which answer is still the reader's. What is left here is the doing -- what goes on the wire,
 * what is stored, and what is drawn.
 */

namespace lens::app {

class CostDuty;

class ExplanationDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.explanationDuty")
public:
    ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, QObject* parent = nullptr);
    void setSelection(PendingSelection selection);
    /// @brief Admit locally filtered scan candidates; cached words are consumed locally first.
    void enqueueScanCandidates(QVariantList candidates, QPoint anchor);
    /// @brief Retry queued scan work after a budget rollover or a raised cap.
    void resumeQueued();
    /// @brief Invalidate queued or in-flight results for a word deleted locally.
    void             invalidateWord(QString const& lemma);
    Q_INVOKABLE void runSelectionAction(QString action, QString text);
    /**
     * @brief Ask again for the bubble standing, under the explanation language now in force.
     *
     * The bubble's language shortcut changes the setting first and then calls this (AppController).
     * A copy already stored in the new language is put up without a request; otherwise the standing
     * card is asked for once more. The subject keeps the display flags it was raised with, so a
     * review answer replayed this way still carries no verdict buttons. A translation has nothing to
     * ask -- its one answer is English whatever the setting says -- and raises nothing here.
     */
    Q_INVOKABLE void reexplain();
    Q_INVOKABLE void mark(QString lemma, bool learned);
    /**
     * @brief Show what is already stored for a word the reader pointed at, and nothing else.
     *
     * No request is made on this path: a pointer crossing the word list is not a request to spend
     * money, and the stored copy is what the reader came to see again. A word with nothing stored in
     * the current explanation language gets a notice saying so, with the one button that does ask
     * the model, so the reader chooses to spend rather than hovering their way into it.
     *
     * @param lemma      The word, as the row draws it.
     * @param anchor     The word's top-left, in the physical pixels the other surfaces anchor with.
     * @param anchorSize The word's size in physical pixels, so the card can sit above it whole or flip below it.
     */
    void reviewWord(QString lemma, QPoint anchor, QSize anchorSize);
    /**
     * @brief Show the stored explanation of a word the reader has just answered about (docs/adr/0050).
     *
     * Like reviewWord() it makes no request and records no pop, and the pet hears nothing: the answer is
     * already the event. Unlike it, the card counts down on its own and offers no Known / New buttons.
     * Nothing shows when the word has no stored copy in the current explanation language.
     *
     * @param lemma  The word the question was about.
     * @param anchor Where the pet's sign is, in the physical pixels the other surfaces anchor with.
     */
    void showReviewAnswer(QString const& lemma, QPoint anchor);
    /// @brief Take down a bubble raised by reviewWord(); one raised anywhere else is left alone.
    ///
    /// The row decides this and only the row does. The bubble's own hover state is not read here on
    /// purpose: it flaps (TODO.md, the bubble-hover jitter item), and hanging this bubble's life off
    /// a signal that flaps is what made it appear and disappear while the pointer stood still. The
    /// word the pointer is on is the one thing that is not in doubt.
    Q_INVOKABLE void dismissReview();
    /// @brief Ask the model about a lemma the reader asked for by name, anchored where it was read.
    void             explainLemma(QString lemma);
    Q_INVOKABLE void dismissBubble();
    Q_INVOKABLE void dismissNotice();
    /**
     * @brief Ask for one step of the definition chain: the word the reader tapped on the standing card.
     *
     * The answer comes back through chainAnswered(), or chainFailed() when it cannot. It is drawn on the chain
     * under the card and never on the card itself, and only while the card still stands on the same word.
     */
    Q_INVOKABLE void explainChainWord(QString lemma);
    /**
     * @brief Ask again for the chain step standing, under the explanation language now in force.
     *
     * The bubble's language shortcut changes the setting first and then calls this (AppController).
     * The answer replaces the step that is drawn, and the steps above it stay: the reader was
     * reading this word, not the one the chain started from. A copy already stored in the new
     * language is put up without a request; otherwise the step is asked for once more.
     *
     * @param lemma The word the drawn step stands on.
     */
    Q_INVOKABLE void reexplainChainStep(QString lemma);
    /**
     * @brief Put a reason card up where no explanation can be.
     * @param title  What the notice is about.
     * @param body   Reader-facing reason, already translated.
     * @param kind   kNoticeInfo or kNoticeError.
     * @param action Label for the one thing the reader can do about it, empty for none.
     * @param lemma  The word that action would explain, when there is one.
     */
    void        showNotice(QString const& title, QString const& body, QString const& kind, QString const& action = {}, QString const& lemma = {});
    QVariantMap bubble() const;
    QVariantMap notice() const;
    QString     busyLabel() const;

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)

signals:
    void bubbleChanged();
    void noticeChanged();
    void busyChanged();
    void statsChanged();
    /// @brief What the desktop pet should react to: a request, a bubble coming or going, a verdict. Carries no text.
    void petEvent(lens::core::pet::PetEvent event);
    /// @brief One step of the definition chain is ready. Carries the entry the card's chain draws.
    void chainAnswered(QVariantMap entry);
    /// @brief A chain step could not be answered. The card stays as it was.
    void chainFailed(QString lemma);

private:
    void explain(PendingSelection const& pending);
    /// @brief Raise the bubble from the copy already stored for @p pending, and count the pop.
    /// @param reveal Whether a copy shown this way writes itself out again. False for the cards the
    ///        word list raises whole; true when a language switch replays the new language, where
    ///        the reader expects the answer to arrive the same way it originally did.
    /// @return True when there was one; a word that has never been explained is not it, and neither
    ///         is a sentence, which is not something the cache holds.
    bool showStored(PendingSelection const& pending, core::CacheContext const& context, bool reveal = false);
    /// @brief Put one batch on the wire, under the context the queue captured for it.
    void send(ExplanationBatch batch);
    /**
     * @brief Send whatever the queue gives out next, until it gives out nothing.
     *
     * A request the reader made that waited for a batch to land is answered out of the cache first
     * when it can be: the batch that has just finished may be what stored that very word, and the
     * cache answer avoids paying for the word twice.
     */
    void sendNext();
    void onBatchFinished(QVector<llm::Explanation> results, llm::Usage usage);
    void onFailed(QString const& message);
    /// @brief Draw an answer that is still arriving, on the card of the request in flight.
    /// @param partial Whatever fields have completed so far; the rest are absent.
    void onPartial(llm::Explanation partial);
    /// The countdown and verdict flags live on @p selection: they are part of how this card
    /// behaves, and travel with a replay through the queue the same way its text does.
    /// @param notifyPet Whether the pet hears about this card. A stream repaints the same card as
    ///        it grows and stays quiet; only the finished answer is an arrival to react to.
    /// @param reveal Whether the card writes its answer out a character at a time. False for the
    ///        cards raised whole out of the cache: nothing is arriving there, and the word list puts
    ///        one up for every row the pointer crosses.
    void    showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool notifyPet = true, bool reveal = true);
    QString noticeTitle(PendingSelection const& pending) const;
    ///
    /// One reader of this, two uses: the request asks the model for it, and the card decides
    /// whether to draw it. Turned off, the definition alone is what is bought and shown; turned
    /// back on, the entries already stored are the ones served -- see WordCache for why the origin
    /// sits outside the cache key.
    bool etymologyEnabled() const;
    /// @brief Whether the reader wants an example sentence. Read when a request goes out, and again
    ///        when the card is drawn: a stored entry without one is a miss while it is on (docs/adr/0033).
    bool exampleEnabled() const;
    /// @brief Whether the card draws one optional block, @p key, whose switch defaults to on.
    ///
    /// A display switch only: the field was bought with the definition and stays in the cache;
    /// this decides whether the payload carries it to the card.
    bool showsCardBlock(char const* key) const;
    /// @brief Put one chain step through the cache, the budget gate and the queue.
    void askChainStep(PendingSelection pending);
    /// @return Whether the card still stands on the word a chain step was asked from.
    bool chainStanding(PendingSelection const& selection) const;
    /// @return The chain entry for one answered step, in the explanation language, with its links.
    QVariantMap chainEntry(llm::Explanation const& explanation, core::CacheContext const& context) const;
    /// @return The words of an English definition the chain offers, as the UTF-16 spans ChainText draws with.
    QVariantList spansOf(QString const& text) const;
    void         clearBubble();
    void         clearNotice();

    StorageDuty&    storage_;
    llm::LlmClient& llm_;
    CostDuty&       cost_;
    /// The rules of the explanation cache: the context a request goes out under, and what a stored
    /// entry is worth to the reader now. Every ask goes through it; nothing here builds a context.
    core::ExplanationCache cache_;
    /// What the reader has asked for and not yet been answered. It holds nothing the duty's own
    /// decisions do not read: the selection standing is the selection bar's, and it is told here
    /// only that the reader has moved on.
    ExplanationQueue queue_;
    PendingSelection pending_;
    /// The selection the standing bubble was raised for. The language shortcut re-asks for this,
    /// not for whatever selection is pending now.
    PendingSelection bubbleSubject_;
    QVariantMap      bubble_;
    QVariantMap      notice_;
    QString          busyLabel_;
    /// Where the row the reader is pointing at sits, so the explanation a notice's button asks for
    /// lands in the same place the review bubble did.
    QPoint reviewAnchor_;
    /// Whether the bubble standing is the one reviewWord() raised. Only that one is the pointer's
    /// to take down: a scan or a selection bubble outlives the pointer leaving a word-list row.
    bool reviewRaised_ = false;
    /// Whether the request in flight has previewed anything yet. One line is what says whether the
    /// reader's provider streams at all; the rest of the chunks would be a repaint per line.
    bool previewSeen_ = false;
};

}
