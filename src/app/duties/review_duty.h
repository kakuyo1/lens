#pragma once

#include <functional>
#include <random>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QTimer>

#include "core/domain/pet/pet_state.h"
#include "core/domain/review_schedule.h"
#include "core/document/explanation_cache.h"
#include "storage_duty.h"

/**
 * @file review_duty.h
 * @brief Asks about a looked-up word when the pet is free, and takes the reader's answer (docs/adr/0050).
 *
 * One question at a time. The word reaches QML through word(), never through a PetEvent: that is a bare
 * enum, so the pet hears that a question went up and not what it is about. A question ends by an answer,
 * by going unanswered for kAnswerTimeoutMs, or by dismiss() when the pet is turned off.
 *
 * Clock readings come in as arguments, so the schedule can be driven from a test. The timers only pass
 * the current time along.
 */

namespace lens::app {

class ReviewDuty final : public QObject {
    Q_OBJECT
public:
    /// @brief How long the sign waits for an answer before the question counts as ignored.
    static constexpr int kAnswerTimeoutMs{90'000};
    /// @brief How often the duty looks at the clock. The frequency sets the random gap between two looks.
    static constexpr int kClockTickMs{60'000};

    /**
     * @param storage      The storage boundary. History, cache, and review state all come through it.
     * @param petEnabled   Whether the pet is switched on.
     * @param petAvailable Whether the pet is free to take a question: idle, or hiding at a screen edge (ADR 0076).
     */
    ReviewDuty(StorageDuty& storage, std::function<bool()> petEnabled, std::function<bool()> petAvailable, QObject* parent = nullptr);

    /// @return The lemma of the question up now; empty when none.
    QString const& word() const
    {
        return word_;
    }

    /// @return The reader's check frequency.
    core::ReviewFrequency frequency() const;

    /// @brief Store a new check frequency. The next look is drawn again from its window, so the change takes effect now.
    /// @param now Epoch ms.
    void setFrequency(core::ReviewFrequency frequency, long long now);

    /// @brief Look at the clock. When a look is due, asks about a due word, if the pet is free and no question is up.
    /// @param now Epoch ms.
    void tick(long long now);

    /// @brief Record the answer to the question up now, and move the word along its curve.
    /// @param now Epoch ms of the answer.
    /// @return The lemma that was answered; empty when no question was up.
    QString answer(bool remembered, long long now);

    /// @brief Take the question up now down unanswered. The word waits out the cooldown and the pet reacts.
    /// @param now Epoch ms.
    void ignore(long long now);

    /// @brief Take the question up now down with no reaction, because the pet was turned off. The cooldown still applies.
    /// @param now Epoch ms.
    void dismiss(long long now);

signals:
    /// @brief The question went up or came down; word() holds the current one.
    void wordChanged();
    /// @brief What the pet should react to. Carries no text.
    void petEvent(lens::core::pet::PetEvent event);

private:
    /// @return Every cached word, each with the state it is at, in the current explanation language.
    std::vector<core::ReviewCandidate> askable();
    /// @return The stored state of a word, or a fresh one from its last lookup when it has none.
    core::ReviewState stateOf(std::string const& lemma) const;
    /// @return Epoch ms of the newest lookup of a word; zero when the history has none.
    long long lastLookupOf(std::string const& lemma) const;
    /// @brief Record the pass-over of the question up now and take it down. @return False when no question was up.
    bool passOver(long long now);
    /// @brief Takes the question down and tells QML.
    void close();
    /// @return A random gap in ms within @p window.
    long long drawGap(core::CheckWindow window);

    StorageDuty& storage_;
    /// The explanation cache's rules, for asking whether a word has a stored explanation at all.
    core::ExplanationCache cache_;
    std::function<bool()>  petEnabled_;
    std::function<bool()>  petAvailable_;
    std::mt19937           random_{std::random_device{}()};
    QString                word_;
    /// Epoch ms of the next look at the clock. Zero until a look is drawn after a start or a change.
    long long nextLook_{0};
    QTimer    clock_;
    QTimer    timeout_;
};

}
