/**
 * @file card_session.cpp
 * @brief The current explanation card: identity, review life, sense cycle and peek requests.
 */

#include "card_session.h"

#include <algorithm>

namespace lens::app {

QString cardKindName(CardKind const kind)
{
    switch (kind) {
        case CardKind::Selection: return QStringLiteral("selection");
        case CardKind::Review: return QStringLiteral("review");
        case CardKind::ReviewAnswer: return QStringLiteral("reviewAnswer");
        case CardKind::None: break;
    }
    return {};
}

bool CardSession::raise(PendingSelection selection, CardKind const kind, QString title)
{
    const bool same = sameCard(selection, kind);
    subject_        = std::move(selection);
    kind_           = kind;
    title_          = std::move(title);
    // Whatever raised this bubble, the review card the pointer's row put up is no longer the
    // reading: the held exit goes with it.
    reviewHeld_           = false;
    reviewDismissPending_ = false;
    return same;
}

void CardSession::clear()
{
    subject_.reset();
    kind_ = CardKind::None;
    title_.clear();
    reviewHeld_           = false;
    reviewDismissPending_ = false;
}

bool CardSession::sameCard(PendingSelection const& selection, CardKind const kind) const
{
    if (!standing() || kind_ != kind)
        return false;
    auto const& standing = *subject_;
    return standing.text == selection.text and standing.surface == selection.surface and
           standing.lemma == selection.lemma and standing.anchor == selection.anchor and
           standing.anchorSize == selection.anchorSize and standing.preset == selection.preset and
           standing.kind == selection.kind and standing.chain == selection.chain and
           standing.parent == selection.parent;
}

CardSession::Presentation CardSession::presentation() const
{
    Presentation out;
    out.kind           = kind_;
    out.countsDown     = standing() and kind_ != CardKind::Review;
    out.offersVerdicts = standing() and kind_ == CardKind::Selection and isWordKind(subject_->kind);
    out.wordCard       = wordCard();
    return out;
}

bool CardSession::wordCard() const
{
    return standing() and not subject_->chain and isWordKind(subject_->kind);
}

CardSession::ReviewDismissal CardSession::dismissReview()
{
    if (kind_ != CardKind::Review)
        return ReviewDismissal::NotReview;
    // A pointer on the card is reading it: the dismissal waits for it to leave, which the card
    // itself reports.
    if (reviewHeld_) {
        reviewDismissPending_ = true;
        return ReviewDismissal::Wait;
    }
    return ReviewDismissal::Dismiss;
}

bool CardSession::dismissReviewNow()
{
    reviewDismissPending_ = false;
    return kind_ == CardKind::Review;
}

bool CardSession::holdReviewCard(bool const on)
{
    if (reviewHeld_ == on)
        return false;
    reviewHeld_ = on;
    if (on) {
        // The pointer arrived on the card: a dismissal that waited for it to leave the word is
        // obsolete -- the reader moved onto the card, not away from the reading.
        reviewDismissPending_ = false;
        return false;
    }
    if (reviewDismissPending_ and kind_ == CardKind::Review) {
        reviewDismissPending_ = false;
        return true;
    }
    return false;
}

std::optional<PendingSelection> CardSession::chainStep(QString const& lemma) const
{
    if (!standing() or subject_->chain)
        return std::nullopt;
    return PendingSelection{.anchor  = subject_->anchor,
                            .kind    = core::SelectionKind::Word,
                            .surface = lemma,
                            .lemma   = lemma,
                            .chain   = true,
                            .parent  = title_};
}

bool CardSession::chainStanding(QString const& parent) const
{
    return standing() and title_ == parent;
}

CardSession::Cycle* CardSession::findCycle(QString const& language, QString const& lemma)
{
    auto languageIt = cycles_.find(language);
    if (languageIt == cycles_.end())
        return nullptr;
    auto lemmaIt = languageIt->find(lemma);
    return lemmaIt == languageIt->end() ? nullptr : &lemmaIt.value();
}

CardSession::Cycle const* CardSession::findCycle(QString const& language, QString const& lemma) const
{
    auto languageIt = cycles_.constFind(language);
    if (languageIt == cycles_.cend())
        return nullptr;
    auto lemmaIt = languageIt->constFind(lemma);
    return lemmaIt == languageIt->cend() ? nullptr : &lemmaIt.value();
}

std::optional<CardSession::CycleView> CardSession::holdAnswer(core::CacheContext const& now, llm::Explanation const& answer)
{
    if (!wordCard() or now.multipleSenses)
        return std::nullopt;
    Cycle& cycle = cycles_[QString::fromStdString(now.language)][subject_->lemma];
    // The answer's own sense is the head of the cycle, and a fresh draw restarts it: what is
    // arriving, on the wire or out of the cache, is the most common sense's to write.
    llm::Sense own{answer.en, answer.zh, answer.translation, QString{}, answer.imageable, answer.imageQuery};
    if (cycle.senses.isEmpty()) {
        cycle.senses.push_back(own);
        // The first sense's label is the signpost the answer already carried.
        cycle.hints.push_back(answer.signpost);
    } else {
        cycle.senses[0] = own;
        cycle.hints[0]  = answer.signpost;
    }
    cycle.index = 0;
    return viewOf(cycle);
}

std::optional<CardSession::CycleView> CardSession::standingCycle(core::CacheContext const& now) const
{
    if (!wordCard() or now.multipleSenses)
        return std::nullopt;
    auto const* cycle = findCycle(QString::fromStdString(now.language), subject_->lemma);
    return cycle ? std::optional<CycleView>{viewOf(*cycle)} : std::nullopt;
}

CardSession::SenseStep CardSession::cycleSense(core::CacheContext const& now, bool const mayAsk)
{
    if (!wordCard())
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::NoWordCard};
    if (now.multipleSenses)
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::MultipleSenses};
    const QString language = QString::fromStdString(now.language);
    Cycle*        cycle    = findCycle(language, subject_->lemma);
    if (cycle == nullptr)
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::NoCycle};
    if (cycle->fetching or senseInFlight_.contains(subject_->lemma))
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::Waiting};

    int next = cycle->index + 1;
    if (next >= cycle->senses.size()) {
        if (cycle->exhausted) {
            if (cycle->senses.size() <= 1)
                return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::Exhausted};
            // Past the last sense the cycle turns back to the most common one.
            next = 0;
        } else {
            // The sense the click asks for has not been bought yet.
            return ask(*cycle, language, subject_->lemma, /*advance=*/true, mayAsk);
        }
    }
    cycle->index = next;
    return {.kind = SenseStep::Kind::Swap, .language = language, .view = viewOf(*cycle)};
}

CardSession::SenseStep CardSession::ensureNextSense(core::CacheContext const& now, bool const mayAsk)
{
    if (!wordCard())
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::NoWordCard};
    if (now.multipleSenses)
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::MultipleSenses};
    const QString language = QString::fromStdString(now.language);
    Cycle*        cycle    = findCycle(language, subject_->lemma);
    if (cycle == nullptr)
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::NoCycle};
    if (cycle->fetching or senseInFlight_.contains(subject_->lemma))
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::Waiting};
    if (cycle->exhausted)
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::Exhausted};
    if (cycle->index + 1 < cycle->senses.size())
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::None};
    return ask(*cycle, language, subject_->lemma, /*advance=*/false, mayAsk);
}

CardSession::SenseStep CardSession::ask(Cycle& cycle, QString const& language, QString const& lemma, bool const advance, bool const mayAsk)
{
    if (!mayAsk)
        return {.kind = SenseStep::Kind::None, .why = SenseStep::Why::Budget};
    QStringList known;
    for (auto const& sense : cycle.senses)
        known << QLatin1String("- ") + sense.en;
    cycle.fetching = true;
    senseInFlight_.insert(lemma, PendingSense{language, advance});
    return {.kind = SenseStep::Kind::Ask, .language = language, .lemma = lemma, .known = known, .advance = advance};
}

CardSession::SenseOutcome CardSession::senseAnswered(QString const&            lemma,
                                                     llm::Explanation const&   answer,
                                                     llm::Usage const&         usage,
                                                     core::CacheContext const& now)
{
    SenseOutcome outcome;
    auto         request = senseInFlight_.find(lemma);
    if (request == senseInFlight_.end())
        return outcome;
    const PendingSense pending = request.value();
    senseInFlight_.erase(request);

    Cycle* cycle = findCycle(pending.language, lemma);
    if (cycle == nullptr)
        return outcome;
    cycle->fetching  = false;
    outcome.language = pending.language;

    if (answer.senses.isEmpty()) {
        // One answer reached the model and came back whole without a sense; one that carries no
        // usage never got that far, and the entry stays up to ask again.
        if (usage.promptTokens == 0 and usage.completionTokens == 0) {
            outcome.result = SenseOutcome::Result::Refused;
            return outcome;
        }
        cycle->exhausted = true;
        outcome.result   = SenseOutcome::Result::Exhausted;
    } else {
        const llm::Sense& sense = answer.senses.front();
        // A model that repeats a listed sense has said "nothing further" in the shape of a
        // definition; the repetition is the exhaustion answer, not another sense.
        const bool known = std::any_of(cycle->senses.cbegin(), cycle->senses.cend(), [&sense](llm::Sense const& existing) {
            return existing.en == sense.en;
        });
        if (known) {
            cycle->exhausted = true;
            outcome.result   = SenseOutcome::Result::Exhausted;
            outcome.repeated = true;
        } else {
            cycle->senses.push_back(sense);
            cycle->hints.push_back(sense.hint);
            outcome.result = SenseOutcome::Result::Grown;
        }
    }
    // A click's answer switches the card onto the sense it brought; a hover's only fills the
    // entry. Neither moves a cycle that was exhausted on the way.
    if (pending.advance and not cycle->exhausted and cycle->index + 1 < cycle->senses.size())
        cycle->index += 1;
    // The swap belongs to a single-sense word card: the reader who turned multiple senses on while
    // the request was away has rows to read, and a one-row payload would take them away.
    if (wordCard() and not now.multipleSenses and subject_->lemma == lemma)
        outcome.view = viewOf(*cycle);
    return outcome;
}

CardSession::PeekStep CardSession::requestPeek(PeekFacts const& facts)
{
    if (!wordCard())
        return {.kind = PeekStep::Kind::None, .why = PeekStep::Why::NoWordCard};
    if (QString::fromStdString(facts.now.language) != QLatin1String("en"))
        return {.kind = PeekStep::Kind::None, .why = PeekStep::Why::NotEnglish};
    if (facts.mother.isEmpty() or facts.mother == QLatin1String("en"))
        return {.kind = PeekStep::Kind::None, .why = PeekStep::Why::NoMotherTongue};

    const QString            lemma      = subject_->lemma;
    std::optional<CycleView> cycle      = standingCycle(facts.now);
    const int                senseIndex = cycle ? cycle->index : 0;

    // The word channel always buys the Chinese pair along with the explanation. A swapped sense
    // carries its own pair, bought with the sense; the answer's own pair is the cached entry's.
    if (facts.mother == QLatin1String("zh")) {
        if (cycle and senseIndex > 0) {
            const QString swapped = cycle->sense.zh;
            if (not swapped.trimmed().isEmpty())
                return {.kind = PeekStep::Kind::Show, .line = swapped, .senseIndex = senseIndex};
        }
        if (not facts.ownZh.isEmpty())
            return {.kind = PeekStep::Kind::Show, .line = facts.ownZh, .senseIndex = senseIndex};
    }
    // A line this session has paid for is the second hold's answer, not the model's to sell twice.
    // The key carries the sense it was bought for: a line bought under the first sense does not
    // answer a hold on the third one, and coming back to a sense finds its line waiting.
    if (auto const line = peekLine(facts.mother, lemma, senseIndex))
        return {.kind = PeekStep::Kind::Show, .line = *line, .senseIndex = senseIndex};
    // A signal carries the lemma and nothing else, so a second request for the same word before
    // the first answers could not be told from it. Wait for the one already out.
    if (peekInFlight_.contains(lemma))
        return {.kind = PeekStep::Kind::None, .why = PeekStep::Why::InFlight};
    if (not facts.mayAsk)
        return {.kind = PeekStep::Kind::None, .why = PeekStep::Why::Budget};

    peekInFlight_.insert(lemma, PendingPeek{facts.mother, senseIndex});
    return {.kind       = PeekStep::Kind::Ask,
            .lemma      = lemma,
            .language   = facts.mother,
            .senseEn    = cycle and senseIndex > 0 ? cycle->sense.en : QString{},
            .senseIndex = senseIndex};
}

std::optional<QString> CardSession::peekAnswered(QString const& lemma, QString const& line, core::CacheContext const& now)
{
    auto request = peekInFlight_.find(lemma);
    if (request == peekInFlight_.end())
        return std::nullopt;
    const PendingPeek pending = request.value();
    peekInFlight_.erase(request);

    // A failed peek stores nothing, and the line is spoken for by the request that just ended.
    if (line.trimmed().isEmpty())
        return std::nullopt;
    // Stored under the key the request went out with, which names the sense the line answers for;
    // the reader may have swapped onto another sense while it was on its way.
    peekLines_[pending.mother][lemma][pending.senseIndex] = line;

    if (!wordCard() or subject_->lemma != lemma)
        return std::nullopt;
    auto const cycle     = standingCycle(now);
    const bool sameSense = cycle ? cycle->index == pending.senseIndex : pending.senseIndex == 0;
    return sameSense ? std::optional<QString>{line} : std::nullopt;
}

bool CardSession::invalidateWord(QString const& lemma)
{
    const auto same = [&lemma](QString const& other) {
        return QString::compare(other, lemma, Qt::CaseInsensitive) == 0;
    };
    // The senses the cycle bought for the word are answers about a word that no longer exists;
    // they go with the cache entry. The peek lines are the same answers' other half.
    for (auto languageIt = cycles_.begin(); languageIt != cycles_.end(); ++languageIt) {
        for (auto lemmaIt = languageIt->begin(); lemmaIt != languageIt->end();) {
            if (same(lemmaIt.key()))
                lemmaIt = languageIt->erase(lemmaIt);
            else
                ++lemmaIt;
        }
    }
    for (auto motherIt = peekLines_.begin(); motherIt != peekLines_.end(); ++motherIt) {
        for (auto lemmaIt = motherIt->begin(); lemmaIt != motherIt->end();) {
            if (same(lemmaIt.key()))
                lemmaIt = motherIt->erase(lemmaIt);
            else
                ++lemmaIt;
        }
    }
    for (auto it = peekInFlight_.begin(); it != peekInFlight_.end();) {
        if (same(it.key()))
            it = peekInFlight_.erase(it);
        else
            ++it;
    }
    for (auto it = senseInFlight_.begin(); it != senseInFlight_.end();) {
        if (same(it.key()))
            it = senseInFlight_.erase(it);
        else
            ++it;
    }

    const bool card = standing() and (same(title_) or (subject_ and same(subject_->lemma)));
    if (card)
        clear();
    return card;
}

std::optional<QString> CardSession::peekLine(QString const& mother, QString const& lemma, int const senseIndex) const
{
    auto const motherIt = peekLines_.constFind(mother);
    if (motherIt == peekLines_.cend())
        return std::nullopt;
    auto const lemmaIt = motherIt->constFind(lemma);
    if (lemmaIt == motherIt->cend())
        return std::nullopt;
    auto const lineIt = lemmaIt->constFind(senseIndex);
    if (lineIt == lemmaIt->cend())
        return std::nullopt;
    return *lineIt;
}

QString CardSession::nextHint(Cycle const& cycle)
{
    // Still on the wire: the entry stands with its offer and no word under it yet.
    if (cycle.fetching)
        return {};
    const int count = cycle.senses.size();
    if (count <= 1 and cycle.exhausted)
        return {};
    // Past the last sense the cycle turns back to the most common one, and the entry says so
    // with the first sense's own label.
    const int next = cycle.exhausted ? (cycle.index + 1) % count : cycle.index + 1;
    return next < count ? cycle.hints.value(next) : QString{};
}

CardSession::CycleView CardSession::viewOf(Cycle const& cycle)
{
    CycleView view;
    view.index     = cycle.index;
    view.offered   = not(cycle.exhausted and cycle.senses.size() <= 1);
    view.exhausted = cycle.exhausted;
    view.fetching  = cycle.fetching;
    view.hint      = nextHint(cycle);
    view.label     = cycle.hints.value(cycle.index);
    view.sense     = cycle.senses.at(cycle.index);
    return view;
}

}
