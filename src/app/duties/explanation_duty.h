#pragma once

#include <QObject>
#include <QHash>
#include <QPoint>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "capture_duty.h"
#include "card_session.h"
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
    /// word the pointer is on is the one thing that is not in doubt -- except when the pointer has
    /// moved onto the card itself, which holdReviewCard() says: the Not-the-Sense? entry at its
    /// foot is there to be pressed, and a pointer cannot press what leaving the word takes away,
    /// so the dismissal then waits for the pointer to leave the card as well.
    Q_INVOKABLE void dismissReview();
    /// @brief Take the review card down whatever is holding it: the panel closed, the row is gone.
    Q_INVOKABLE void dismissReviewNow();
    /// @brief Report whether the pointer is on the review card the word list raised.
    ///
    /// Pushed from the card itself (BubbleCard.qml). A held card does not go to a row's dismissal;
    /// the dismissal lands when the hold ends, so the card survives exactly as long as the pointer
    /// is on the word or on the card, and no longer.
    Q_INVOKABLE void holdReviewCard(bool on);
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
    /// @brief Ask for the standing word's meaning in the mother tongue, as the held globe does.
    ///
    /// The peek belongs to the word, sentence or entity card standing when the hold completed, under
    /// the English explanation the globe only exists under. A line this session already paid for, or
    /// the Chinese pair a word answer carries, is emitted without a request; anything else goes
    /// to the peek channel, budget permitting. The answer lands in peekArrived() and failure is
    /// quiet: a held gesture that bought nothing owes the reader no notice (docs/adr/0112).
    Q_INVOKABLE void requestPeek();
    /// @brief Move the standing word card onto its next distinct common sense.
    ///
    /// The Not-the-Sense? entry's click. A sense already fetched this session is swapped in at
    /// once; otherwise one is asked for on the sense channel, budget permitting, and the card
    /// switches when the answer lands. Past the last sense the cycle wraps to the most common
    /// one. The card is not re-raised: the answer travels through senseSwapped() and the window
    /// keeps the place the reader left it in.
    Q_INVOKABLE void cycleSense();
    /// @brief Ask for the sense after the one the standing card shows, without switching to it.
    ///
    /// The entry's hover. A sense the pointer arrives on before the click is one the click swaps
    /// in at once rather than one it waits for; the budget gate and the quietness are the peek's.
    Q_INVOKABLE void ensureNextSense();
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
    /// @brief The mother-tongue line for the word the reader held the globe on. The line belongs
    ///        to the word card standing when it arrives; a hold that has since ended draws nothing.
    void peekArrived(QString line);
    /// @brief The sense cycle changed: the card draws @p swap in place -- the row's own texts and
    ///        pictures when @p swap carries them, the entry's hint and exhaustion either way. The
    ///        window is not re-placed and the countdown is not reset from here; the entry's press
    ///        refills the clock the way any press on the card does.
    void senseSwapped(QVariantMap swap);

private:
    void explain(PendingSelection const& pending);
    /// @brief Raise the bubble from the copy already stored for @p pending, and count the pop.
    /// @return True when there was one; a word that has never been explained is not it, and neither
    ///         is a sentence, which is not something the cache holds.
    bool showStored(PendingSelection const& pending, core::CacheContext const& context);
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
    /// @brief Put a peek answer away for the session, and onto the card when it still stands.
    void onPeekAnswered(QString const& lemma, QString const& line, llm::Usage usage);
    /// @brief Put a next-sense answer away for the session, and onto the card when it still stands.
    ///
    /// An answer without senses is the exhaustion answer when the call reached the model and came
    /// back whole, and a quiet failure when it carries no usage at all -- only the exhaustion one
    /// retires the entry; a failure leaves it up to ask again.
    void onNextSenseAnswered(QString const& lemma, llm::Explanation const& answer, llm::Usage usage);
    /// @return The mother tongue the peek answers in, as the document holds it.
    QString motherTongue() const;

    /// @brief Tell the card what its cycle now shows, and leave the payload agreeing with it: a
    ///        later refresh of the same card must not snap the sense back while it stands.
    /// @param language The cycle's explanation language, which the row's fields are chosen by.
    void emitSenseSwapped(CardSession::CycleView const& view, QString const& language);
    /// @brief Draw an answer that is still arriving, on the card of the request in flight.
    /// @param partial Whatever fields have completed so far; the rest are absent.
    void onPartial(llm::Explanation partial);
    /// The countdown and verdict flags live on @p selection: they are part of how this card behaves.
    /// @param notifyPet Whether the pet hears about this card. A stream repaints the same card as
    ///        it grows and stays quiet; only the finished answer is an arrival to react to.
    /// @param reveal Whether the card writes its answer out a character at a time. False for the
    ///        cards raised whole out of the cache: nothing is arriving there, and the word list puts
    ///        one up for every row the pointer crosses.
    /// @param kind   Which identity the card is settled under: a selection, the word list's review
    ///        replay, or the answer beside the pet's sign.
    void    showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool notifyPet = true, bool reveal = true, CardKind kind = CardKind::Selection);
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
    void        clearBubble();
    void        clearNotice();

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
    /// The card standing: its identity, its review life, its sense cycle and the requests it has
    /// out. It is the one owner of what the duty's entry points used to spell for themselves.
    CardSession session_;
    QVariantMap bubble_;
    QVariantMap notice_;
    QString     busyLabel_;
    /// Where the row the reader is pointing at sits, so the explanation a notice's button asks for
    /// lands in the same place the review bubble did.
    QPoint reviewAnchor_;
    /// Whether the request in flight has previewed anything yet. One line is what says whether the
    /// reader's provider streams at all; the rest of the chunks would be a repaint per line.
    bool previewSeen_ = false;
};

}
