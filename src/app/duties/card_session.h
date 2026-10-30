#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/document/known_store.h"
#include "llm/llm_client.h"
#include "pending_selection.h"

/**
 * @file card_session.h
 * @brief The current explanation card: which identity it has, what it offers, and which request
 *        each answer belongs to.
 *
 * ExplanationDuty used to keep this in twelve mutable members and spell the same rule at every
 * entry point: four booleans described four card identities, the standing-word guard was written
 * four times, and the two answers that arrive without a request id were matched through one key
 * each -- so an answer for word A could land under word B's key when the reader moved on. The
 * session owns the card, and the duty only sends, stores and draws. No QObject and no event loop:
 * every decision is a value in and a value out, the way ExplanationQueue is.
 *
 * The preview card is the view's own: SettingsPopup raises it from PreviewSample rather than from
 * the duty, so it keeps the Bubble's `preview` flag instead of a CardKind.
 */

namespace lens::app {

/// @brief Which card stands. Settled when it is raised, never re-derived from flags.
enum class CardKind {
    None,
    Selection,    ///< A selection, a scan answer, or an answer replayed out of the cache.
    Review,       ///< The word list's hover replay: the pointer's to read, no clock.
    ReviewAnswer, ///< The answer beside the pet's review sign: a clock, no verdict.
};

/// @return @p kind as the bubble payload spells it: "selection", "review" or "reviewAnswer".
QString cardKindName(CardKind kind);

class CardSession {
public:
    /// @brief What the card is allowed to draw, settled by its kind rather than by four flags.
    struct Presentation {
        CardKind kind           = CardKind::None;
        bool     countsDown     = false;
        bool     offersVerdicts = false;
        bool     wordCard       = false;
    };

    /**
     * @brief Put a card up, or refresh the one standing.
     * @param selection The card's subject.
     * @param kind      The identity to settle it under.
     * @param title     The title the card draws, which a chain step names its parent by.
     * @return Whether this is the same card already standing, so the view need not replace it.
     */
    bool raise(PendingSelection selection, CardKind kind, QString title);
    /// @brief Take the card down. The session's bought senses and peek lines stay for the next card.
    void clear();

    [[nodiscard]] bool standing() const
    {
        return subject_.has_value() && kind_ != CardKind::None;
    }
    [[nodiscard]] Presentation            presentation() const;
    [[nodiscard]] PendingSelection const* subject() const
    {
        return subject_ ? &*subject_ : nullptr;
    }
    /// @return Whether the card is a word at its root: the guard the peek, the sense cycle and the
    ///         chain all used to spell for themselves.
    [[nodiscard]] bool wordCard() const;
    /// @return Whether the card is one the held globe answers: a word, sentence or entity at its root.
    [[nodiscard]] bool peekableCard() const;
    /// @return What the peek asks about and keys its answers by: the lemma for a word, the selected
    ///         text for a sentence or entity, which carry no lemma. Meaningful while peekableCard() holds.
    [[nodiscard]] QString peekKey() const;

    /// @brief The word-list review card's dismissal, as the pointer allows it to land.
    enum class ReviewDismissal { NotReview,
                                 Wait,
                                 Dismiss };
    ReviewDismissal dismissReview();
    /// @return Whether a review card was standing and is now to be taken down, held or not.
    bool dismissReviewNow();
    /// @return Whether the hold ending lands a dismissal that waited for the pointer to leave.
    bool holdReviewCard(bool on);

    /// @brief The chain step the reader asked for, when a card still stands to hang it under.
    /// @return The request, with the card's anchor and title as its own, or nothing.
    std::optional<PendingSelection> chainStep(QString const& lemma) const;
    /// @return Whether the card a chain step was asked from is still the one standing.
    [[nodiscard]] bool chainStanding(QString const& parent) const;

    /// @brief What the card draws of its cycle now: one sense, the entry's state, its next word.
    struct CycleView {
        int        index     = 0;
        bool       offered   = false; ///< The Not-the-Sense? entry is drawn.
        bool       exhausted = false;
        bool       fetching  = false;
        QString    hint;  ///< The word the next press would bring, empty while it is unbought.
        QString    label; ///< The signpost the standing sense is drawn under.
        llm::Sense sense;
    };

    /// @brief Put @p answer's own sense at the head of the card's cycle, replacing what was there.
    /// @return What the card draws, or nothing when the card is not a single-sense word card.
    std::optional<CycleView> holdAnswer(core::CacheContext const& now, llm::Explanation const& answer);
    /// @return The card's cycle as it stands, or nothing when it has none to draw.
    std::optional<CycleView> standingCycle(core::CacheContext const& now) const;

    /// @brief What the Not-the-Sense? entry asks the duty to do.
    struct SenseStep {
        enum class Kind { None,
                          Swap,
                          Ask } kind  = Kind::None;
        enum class Why { None,
                         NoWordCard,
                         MultipleSenses,
                         NoCycle,
                         Waiting,
                         Exhausted,
                         Budget } why = Why::None;
        QString     language;        ///< The cycle's explanation language.
        CycleView   view;            ///< Swap: what the card now draws.
        QString     lemma;           ///< Ask: the word the sense request is about.
        QStringList known;           ///< Ask: the senses already shown, already "- sense" lines.
        bool        advance = false; ///< Ask: whether the answer switches the card onto it.
    };

    /// @brief The entry's click: the next sense, or the request that buys it.
    SenseStep cycleSense(core::CacheContext const& now, bool mayAsk);
    /// @brief The entry's hover: buy the next sense without switching to it.
    SenseStep ensureNextSense(core::CacheContext const& now, bool mayAsk);

    /// @brief What an answer on the sense channel did to its cycle.
    struct SenseOutcome {
        enum class Result { None,
                            Refused,
                            Exhausted,
                            Grown } result = Result::None;
        bool                     repeated  = false; ///< Exhausted because the model repeated a listed sense.
        QString                  language;          ///< The language the request went out under.
        std::optional<CycleView> view;              ///< What to draw, when the card still stands on it.
    };

    /// @brief File an answer under the request it belongs to, never under the latest one.
    /// @param lemma  The word the client echoed back, which is what names the request here.
    /// @param answer The answer; empty senses mean exhaustion or refusal, told apart by @p usage.
    SenseOutcome senseAnswered(QString const& lemma, llm::Explanation const& answer, llm::Usage const& usage, core::CacheContext const& now);

    /// @brief What the duty knows before the peek is decided: the budget gate and the free pair.
    struct PeekFacts {
        core::CacheContext now;
        bool               mayAsk = true;
        QString            mother; ///< The mother tongue the peek answers in.
        QString            ownZh;  ///< The stored Chinese pair for the word, when there is one.
    };

    /// @brief What the held globe asks the duty to do.
    struct PeekStep {
        enum class Kind { None,
                          Show,
                          Ask,
                          Waiting } kind = Kind::None;
        enum class Why { None,
                         NotPeekable,
                         NotEnglish,
                         NoMotherTongue,
                         InFlight,
                         Budget } why    = Why::None;
        QString line;
        QString lemma;
        QString language; ///< Ask: the mother tongue.
        QString senseEn;  ///< Ask: the standing sense, when it is not the answer's own first one.
        int     senseIndex = 0;
    };

    /// @brief The held globe: a line already paid for, a request, or nothing.
    PeekStep requestPeek(PeekFacts const& facts);
    /// @return The line to draw, when the answer belongs to the word and sense the card shows.
    std::optional<QString> peekAnswered(QString const& lemma, QString const& line, core::CacheContext const& now);

    /// @brief The reader deleted a word: its cycles, lines and requests go with it.
    /// @return Whether the standing card was that word and is now down.
    bool invalidateWord(QString const& lemma);

private:
    /// One word's sense cycle, this session: the answer's own sense first, then the ones the
    /// Not-the-Sense? entry has bought, each with the label the entry shows.
    struct Cycle {
        QVector<llm::Sense> senses;
        QStringList         hints;
        int                 index     = 0;
        bool                exhausted = false;
        bool                fetching  = false;
    };
    struct PendingPeek {
        QString mother;
        int     senseIndex = 0;
    };
    struct PendingSense {
        QString language;
        bool    advance = false;
    };

    [[nodiscard]] bool     sameCard(PendingSelection const& selection, CardKind kind) const;
    Cycle*                 findCycle(QString const& language, QString const& lemma);
    Cycle const*           findCycle(QString const& language, QString const& lemma) const;
    std::optional<QString> peekLine(QString const& mother, QString const& lemma, int senseIndex) const;
    static QString         nextHint(Cycle const& cycle);
    static CycleView       viewOf(Cycle const& cycle);
    SenseStep              ask(Cycle& cycle, QString const& language, QString const& lemma, bool advance, bool mayAsk);

    std::optional<PendingSelection> subject_;
    CardKind                        kind_ = CardKind::None;
    QString                         title_;
    /// The pointer's hold on a review card, and the dismissal that waits for it to end.
    bool reviewHeld_           = false;
    bool reviewDismissPending_ = false;

    /// The sense cycles bought this session, keyed explanation language then lemma.
    QHash<QString, QHash<QString, Cycle>> cycles_;
    /// The peek lines bought this session, keyed mother tongue, lemma and sense index.
    QHash<QString, QHash<QString, QHash<int, QString>>> peekLines_;
    /// The one peek request out per subject, under the peekKey() it was asked with. A signal carries
    /// only that key, so a second request for the same subject cannot be told from the first one's answer.
    QHash<QString, PendingPeek> peekInFlight_;
    /// The one sense request out per word, under the cycle it was asked from.
    QHash<QString, PendingSense> senseInFlight_;
};

}
