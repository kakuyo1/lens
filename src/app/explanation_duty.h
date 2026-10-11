#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "capture_duty.h"
#include "core/pet/pet_state.h"
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
     * @param lemma  The word, as the row draws it.
     * @param anchor Where the row is, in the physical pixels the other surfaces anchor with.
     */
    void reviewWord(QString lemma, QPoint anchor);
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
     * when it can be: the batch that has just finished may be what stored that very word, and a
     * word already paid for is not bought twice.
     */
    void sendNext();
    void onBatchFinished(QVector<llm::Explanation> results, llm::Usage usage);
    void onFailed(QString const& message);
    /// @param countsDown Whether the bubble takes itself down after its countdown. A bubble the
    ///        word list raises belongs to the row under the pointer instead, and is taken down when
    ///        the pointer leaves it.
    void    showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool countsDown = true);
    QString noticeTitle(PendingSelection const& pending) const;
    ///
    /// One reader of this, two uses: the request asks the model for it, and the card decides
    /// whether to draw it. Turned off, the definition alone is what is bought and shown; turned
    /// back on, the entries already stored are the ones served -- see WordCache for why the origin
    /// sits outside the cache key.
    bool etymologyEnabled() const;
    void clearBubble();
    void clearNotice();

    StorageDuty&    storage_;
    llm::LlmClient& llm_;
    CostDuty&       cost_;
    /// What the reader has asked for and not yet been answered. It holds nothing the duty's own
    /// decisions do not read: the selection standing is the selection bar's, and it is told here
    /// only that the reader has moved on.
    ExplanationQueue queue_;
    PendingSelection pending_;
    QVariantMap      bubble_;
    QVariantMap      notice_;
    QString          busyLabel_;
    /// Where the row the reader is pointing at sits, so the explanation a notice's button asks for
    /// lands in the same place the review bubble did.
    QPoint reviewAnchor_;
    /// Whether the bubble standing is the one reviewWord() raised. Only that one is the pointer's
    /// to take down: a scan or a selection bubble outlives the pointer leaving a word-list row.
    bool reviewRaised_ = false;
};

}
