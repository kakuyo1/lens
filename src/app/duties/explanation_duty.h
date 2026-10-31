#pragma once

#include <QObject>
#include <QHash>
#include <QPoint>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "card_session.h"
#include "core/domain/pet/pet_state.h"
#include "core/document/explanation_cache.h"
#include "explanation_exchange.h"
#include "llm/llm_client.h"
#include "pending_selection.h"
#include "storage_duty.h"

/**
 * @file explanation_duty.h
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 *
 * The transaction -- the cache's answer, the budget's gate, the request's shape, the batch's
 * settlement and the card standing -- is ExplanationExchange's. What is left here is the doing:
 * the reader's settings, the wire, the store, and the surfaces.
 */

namespace lens::app {

class CostDuty;
class ReviewDuty;

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
     * the current explanation language gets a hint beside the word (wordHint), which carries the one
     * button that does ask the model. The hover itself asks nothing.
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
    /// @brief Report whether the pointer is on the word hint.
    ///
    /// Pushed from the hint's own window. A held hint survives the row's dismissal, which then waits for
    /// the hold to end, so the hint's button stays reachable from the word.
    Q_INVOKABLE void holdWordHint(bool on);
    /// @brief Ask the model about a lemma the reader asked for by name, anchored where it was read.
    ///
    /// This is the one path from a hover to a request: the word hint's button calls it.
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
     * @param intent What the action does when it is not an explanation (kNoticeIntentApiKey), empty for an explanation.
     */
    void        showNotice(QString const& title, QString const& body, QString const& kind, QString const& action = {}, QString const& lemma = {}, QString const& intent = {});
    QVariantMap bubble() const;
    QVariantMap notice() const;
    /// @return The word's hint as the view places it: the lemma, the body and the anchor in physical pixels.
    QVariantMap wordHint() const;
    QString     busyLabel() const;

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QVariantMap wordHint READ wordHint NOTIFY wordHintChanged)

    /// @brief Wire the review duty in, after construction.
    /// @param review The duty whose curves the review and answer cards read. The duties are built
    ///               in an order that cannot hand this one over at construction time, so the
    ///               controller binds it as soon as both exist; until then the cards carry no
    ///               next-ask line.
    void setReviewSource(ReviewDuty* review);

signals:
    void bubbleChanged();
    void noticeChanged();
    void wordHintChanged();
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
    /// @brief The settings one exchange decision reads: the language and sense mode, the budget's
    ///        verdict, and whether an example is asked for.
    ExchangeFacts facts() const;
    /// @brief Execute what one transaction answered: count a pop, draw the card, hang the chain step,
    ///        send the request, or raise the review's notice.
    /// @param save Whether a pop writes the store now; the scan's batch saves once after its loop.
    void apply(ExplanationExchange::Outcome const& outcome, bool save = true);
    /**
     * @brief Send whatever the exchange gives out next, until it gives out nothing.
     *
     * A request the reader made that waited for a batch to land is answered out of the cache first
     * when it can be: the batch that has just finished may be what stored that very word, and the
     * cache answer avoids paying for the word twice.
     */
    void sendNext();
    /// @brief Put one batch on the wire and count the words the request asks about.
    void send(llm::ExplainRequest request, ExplanationBatch const& batch);
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
    /// @brief Ask which part of the sentence each run of words plays, onto a breakdown card raised now.
    void breakdown(PendingSelection const& pending);
    /// @brief Draw the answer for the breakdown card standing on @p sentence; an answer for a card the
    ///        reader has moved on from is dropped, though its usage is still paid.
    void onBreakdownAnswered(QString const& sentence, QVector<llm::RoleSpan> const& spans, llm::Usage usage, QString const& failure);
    /// @brief Ask for the spelling or grammar correction of the selection, onto a card raised now.
    /// @param preset kSpellPreset or kGrammarPreset.
    void correct(PendingSelection const& pending, QString const& preset);
    /// @brief Draw the corrected text for the card standing on @p text, or the failure line when none came back.
    void onCorrectionAnswered(QString const& text, llm::Channel channel, QString const& corrected, llm::Usage usage, QString const& failure);
    /// @brief Raise the correction card in @p state, the same shape as the breakdown card; @p failure is the
    ///        reader's reason on a failed card and empty otherwise.
    void showCorrection(PendingSelection const& selection, QString const& state, QVariantList const& segments, QString const& failure);
    /// @brief Raise the breakdown card on @p selection in @p state: "pending", "ready" or "failed"; @p failure is
    ///        the reader's reason on a failed card and empty otherwise.
    void showBreakdown(PendingSelection const& selection, QString const& state, QVariantList const& segments, QStringList const& roles, QString const& failure);
    /// @return The mother tongue the peek answers in, as the document holds it.
    QString motherTongue() const;

    /// @brief Tell the card what its cycle now shows, and leave the payload agreeing with it: a
    ///        later refresh of the same card must not snap the sense back while it stands.
    /// @param language The cycle's explanation language, which the row's fields are chosen by.
    void emitSenseSwapped(CardSession::CycleView const& view, QString const& language);
    /// @brief Draw an answer that is still arriving, on the card of the request in flight.
    /// @param partial Whatever fields have completed so far; the rest are absent.
    void onPartial(llm::Explanation partial);
    /// @brief Draw the card the exchange raised. The countdown, the verdict buttons and the card's
    ///        identity are settled there; the payload built here is their projection.
    /// @param holding Whether the answer is still on its way: the card the reader's click raised keeps
    ///        the height it reserved until the whole answer is in hand (docs/adr/0146).
    void showBubble(ExplanationExchange::CardView const& card, bool holding = false);
    /// @brief Draw the card the reader's ask raised before there was an answer: the ask's own shape,
    ///        with the fields the answer will fill left out until they arrive (docs/adr/0146).
    void showWaiting(ExplanationExchange::CardView const& card);
    /// @return How many lines the answer this card is waiting for will need, from the kind, the
    ///         explanation mode and the display switches -- what the card reserves its height by, so
    ///         nothing moves while the answer streams in (docs/adr/0146).
    int     waitingLines(const PendingSelection& selection, const core::CacheContext& context) const;
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
    /// @return The chain entry for one answered step, in the explanation language, with its links.
    QVariantMap chainEntry(llm::Explanation const& explanation, core::CacheContext const& context) const;
    void        clearBubble();
    void        clearNotice();
    /// @brief Put the hint up for a word with nothing stored, against the word the row last pointed at.
    void showWordHint(QString const& lemma);
    void clearWordHint();

    StorageDuty&    storage_;
    llm::LlmClient& llm_;
    CostDuty&       cost_;
    /// The transaction: the cache's answer, the budget's gate, the request's shape, the batch's
    /// settlement, the card standing and the reader's selection. The one owner of all five.
    ExplanationExchange exchange_;
    QVariantMap         bubble_;
    QVariantMap         notice_;
    QVariantMap         wordHint_;
    /// The pointer's hold on the word hint, and the row's dismissal that waits for it to end.
    bool    wordHintHeld_           = false;
    bool    wordHintDismissPending_ = false;
    QString busyLabel_;
    /// Where the row the reader is pointing at sits, and how big the word is: the review card and the
    /// word's hint are both placed against it.
    QPoint reviewAnchor_;
    QSize  reviewAnchorSize_;
    /// Whether the request in flight has previewed anything yet. One line is what says whether the
    /// reader's provider streams at all; the rest of the chunks would be a repaint per line.
    bool previewSeen_ = false;
    /// The review duty the review and answer cards read their curve from. Null until the
    /// controller wires it, which is why every read of it is guarded.
    ReviewDuty* review_ = nullptr;
};

}
