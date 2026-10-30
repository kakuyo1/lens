/**
 * @file explanation_exchange.cpp
 * @brief The one transaction behind every door into an explanation.
 */

#include "explanation_exchange.h"

#include <utility>

#include "word_answer.h"

namespace lens::app {

using core::ExplanationCache;

ExplanationExchange::ExplanationExchange(core::KnownStore& store)
    : cache_(store)
{}

void ExplanationExchange::setSelection(PendingSelection selection)
{
    selection_ = std::move(selection);
    queue_.selectionChanged();
}

void ExplanationExchange::clearSelection()
{
    selection_ = PendingSelection{};
    queue_.selectionChanged();
}

ExplanationExchange::Outcome ExplanationExchange::ask(PendingSelection selection, AskOrigin const origin, ExchangeFacts const& facts)
{
    // The word list's candidates are words by construction, and a scan that hits is served only when
    // no card stands: the row the pointer already crossed keeps its card.
    if (origin == AskOrigin::Scan) {
        auto const context = cache_.contextFor(core::SelectionKind::Word);
        auto const found   = cache_.look(selection.lemma.toStdString(), context);
        if (found.state == ExplanationCache::State::Hit) {
            if (session_.standing() or queue_.inFlight())
                return Outcome{.popLemma = selection.lemma};
            return fromEntry(std::move(selection), found.entry, context, CardKind::Selection, /*reveal=*/false, /*notifyPet=*/true, /*pop=*/true);
        }
        queue_.addScan({std::move(selection)});
        return {};
    }

    auto const context = cache_.contextFor(selection.kind);

    // The hover shows what is stored and buys nothing; an entry bought without the example still
    // shows, because the reader came to reread and not to repurchase (docs/adr/0033).
    if (origin == AskOrigin::Review) {
        auto const found = cache_.look(selection.lemma.toStdString(), context);
        if (found.state != ExplanationCache::State::Missing)
            return fromEntry(std::move(selection), found.entry, context, CardKind::Review, /*reveal=*/false, /*notifyPet=*/true, /*pop=*/false);
        return Outcome{.kind = Outcome::Kind::Notice, .noticeLemma = selection.lemma};
    }

    // The pet's sign shows a stored answer and nothing else: no request, no pop, no notice.
    if (origin == AskOrigin::ReviewAnswer) {
        auto const found = cache_.look(selection.lemma.toStdString(), context);
        if (found.state != ExplanationCache::State::Hit)
            return {};
        return fromEntry(std::move(selection), found.entry, context, CardKind::ReviewAnswer, /*reveal=*/false, /*notifyPet=*/false, /*pop=*/false);
    }

    // The reader's own ask waits behind the one request in flight: the answer on its way may be the
    // one that pays for this word.
    if (queue_.inFlight()) {
        queue_.request(std::move(selection));
        return {};
    }
    if (isWordKind(selection.kind)) {
        auto const found = cache_.look(selection.lemma.toStdString(), context);
        if (found.state == ExplanationCache::State::Hit)
            return fromEntry(std::move(selection), found.entry, context, CardKind::Selection, /*reveal=*/false, /*notifyPet=*/true, /*pop=*/true);
    }
    // Out of budget the ask is dropped rather than left waiting for a budget that may be a day away;
    // the action bar has already said why.
    if (not facts.mayAsk)
        return {};
    queue_.request(std::move(selection));
    return {};
}

ExplanationExchange::Outcome ExplanationExchange::stored(PendingSelection selection)
{
    if (not isWordKind(selection.kind))
        return {};
    auto const context = cache_.contextFor(selection.kind);
    return storedIn(std::move(selection), context);
}

ExplanationExchange::Outcome ExplanationExchange::take(ExchangeFacts const& facts, bool const etymology)
{
    auto batch = queue_.take(facts.mayAsk, facts.now, facts.example);
    if (not batch)
        return {};

    // A held request comes back out through the cache before it goes to the model: the batch that has
    // just landed may be the one that stored this very word.
    if (not batch->fromScan) {
        auto hit = storedIn(batch->items.front(), batch->context);
        if (hit.kind != Outcome::Kind::None or not hit.popLemma.isEmpty()) {
            queue_.complete();
            return hit;
        }
    }

    Outcome outcome;
    outcome.kind    = Outcome::Kind::Send;
    outcome.batch   = *batch;
    outcome.request = requestFor(*batch, etymology);
    return outcome;
}

ExplanationExchange::Outcome ExplanationExchange::settle(QVector<llm::Explanation> const& results, ExchangeFacts const& facts)
{
    if (not queue_.inFlight())
        return {};

    QStringList titles;
    for (auto const& result : results)
        titles.append(result.title);
    auto const settlement = queue_.settled(titles, facts.now);

    if (not results.isEmpty()) {
        for (int index = 0; index < settlement.resultFor.size(); ++index) {
            const int               answer   = settlement.resultFor.at(index);
            const PendingSelection& selected = settlement.batch.items.at(index);
            if (answer < 0 or not isWordKind(selected.kind))
                continue;
            // The example is stored from the batch's own flag, not the setting as it stands now: an
            // answer paid for survives the switch turning off mid-request (docs/adr/0033).
            cache_.store(selected.lemma.toStdString(),
                         word_answer::toCache(results.at(answer), settlement.batch.example),
                         settlement.batch.context);
        }
    }
    queue_.complete();

    if (results.isEmpty() or not settlement.subject)
        return {};
    auto const& subject = *settlement.subject;
    if (subject.chain) {
        if (not session_.chainStanding(subject.parent))
            return {};
        Outcome outcome;
        outcome.kind      = Outcome::Kind::Chain;
        outcome.selection = subject;
        outcome.answer    = results.front();
        outcome.context   = settlement.batch.context;
        return outcome;
    }

    Outcome outcome;
    outcome.kind = Outcome::Kind::Card;
    outcome.card = raise(subject,
                         results.front().title,
                         results.front(),
                         settlement.batch.context,
                         CardKind::Selection,
                         /*reveal=*/true,
                         /*notifyPet=*/true);
    return outcome;
}

std::optional<PendingSelection> ExplanationExchange::refuse()
{
    auto const subject = queue_.refusalSubject();
    queue_.complete();
    return subject;
}

bool ExplanationExchange::invalidateWord(QString const& lemma)
{
    queue_.invalidate(lemma);
    return session_.invalidateWord(lemma);
}

std::optional<PendingSelection> ExplanationExchange::liveSubject(QString const& title, core::CacheContext const& now) const
{
    return queue_.liveSubject(title, now);
}

ExplanationExchange::CardView ExplanationExchange::raise(PendingSelection          selection,
                                                         QString const&            title,
                                                         llm::Explanation const&   answer,
                                                         core::CacheContext const& context,
                                                         CardKind const            kind,
                                                         bool const                reveal,
                                                         bool const                notifyPet)
{
    auto const same = session_.raise(std::move(selection), kind, title);
    auto const face = session_.presentation();

    CardView view;
    view.selection      = *session_.subject();
    view.answer         = answer;
    view.context        = context;
    view.kind           = face.kind;
    view.sameCard       = same;
    view.countsDown     = face.countsDown;
    view.offersVerdicts = face.offersVerdicts;
    view.reveal         = reveal;
    view.notifyPet      = notifyPet;
    // An answer card keeps the session's sense cycle; a control card (a breakdown, a correction)
    // carries no answer and no cycle.
    if (not answer.title.isEmpty() and face.wordCard and not context.multipleSenses)
        view.cycle = session_.holdAnswer(context, answer);
    return view;
}

CardSession::PeekStep ExplanationExchange::requestPeek(QString const& mother, bool const mayAsk, core::CacheContext const& now)
{
    CardSession::PeekFacts facts{now, mayAsk, mother, {}};
    // The word channel always buys the Chinese pair along with the explanation, so a Chinese mother
    // tongue is answered by the entry the standing card came from instead of a second purchase.
    if (mother == QLatin1String("zh") and session_.wordCard() and session_.subject()) {
        auto const found = cache_.look(session_.subject()->lemma.toStdString(), cache_.contextFor(core::SelectionKind::Word));
        if (found.state != ExplanationCache::State::Missing)
            facts.ownZh = QString::fromStdString(found.entry.zh);
    }
    return session_.requestPeek(facts);
}

ExplanationExchange::Outcome ExplanationExchange::fromEntry(PendingSelection          selection,
                                                            core::WordCache const&    entry,
                                                            core::CacheContext const& context,
                                                            CardKind const            kind,
                                                            bool const                reveal,
                                                            bool const                notifyPet,
                                                            bool const                pop)
{
    auto    answer = word_answer::fromCache(entry, selection.lemma);
    Outcome outcome;
    outcome.popLemma = pop ? selection.lemma : QString{};

    if (selection.chain) {
        // A step goes onto the chain under the card, and only while that card still stands. The pop
        // counts either way: the word was read.
        if (not session_.chainStanding(selection.parent))
            return outcome;
        outcome.kind      = Outcome::Kind::Chain;
        outcome.selection = std::move(selection);
        outcome.answer    = std::move(answer);
        outcome.context   = context;
        return outcome;
    }

    outcome.kind = Outcome::Kind::Card;
    outcome.card = raise(std::move(selection), answer.title, answer, context, kind, reveal, notifyPet);
    return outcome;
}

ExplanationExchange::Outcome ExplanationExchange::storedIn(PendingSelection selection, core::CacheContext const& context)
{
    if (not isWordKind(selection.kind))
        return {};
    auto const found = cache_.look(selection.lemma.toStdString(), context);
    if (found.state != ExplanationCache::State::Hit)
        return {};
    return fromEntry(std::move(selection), found.entry, context, CardKind::Selection, /*reveal=*/false, /*notifyPet=*/true, /*pop=*/true);
}

llm::ExplainRequest ExplanationExchange::requestFor(ExplanationBatch const& batch, bool const etymology)
{
    auto const& first = batch.items.front();
    // A word is what is left when the kind names no other channel, which is what the word list's own
    // candidates carry.
    auto channel = llm::Channel::Word;
    if (first.kind == core::SelectionKind::Entity)
        channel = llm::Channel::Entity;
    else if (first.kind == core::SelectionKind::Sentence)
        channel = llm::Channel::Sentence;
    else if (first.kind == core::SelectionKind::Translation)
        channel = llm::Channel::Translation;

    llm::ExplainRequest request;
    request.channel        = channel;
    request.preset         = first.preset;
    request.language       = QString::fromStdString(batch.context.language);
    request.multipleSenses = batch.context.multipleSenses;
    request.etymology      = isWordKind(first.kind) and etymology;
    request.example        = isWordKind(first.kind) and batch.example;
    QStringList words;
    for (auto const& item : batch.items)
        words << (isWordKind(item.kind) ? item.lemma : item.text);
    request.words = std::move(words);
    return request;
}

}
