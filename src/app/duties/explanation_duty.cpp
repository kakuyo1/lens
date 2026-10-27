/**
 * @file explanation_duty.cpp
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

#include "explanation_duty.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>

#include <utility>

#include "core/domain/filter_core.h"
#include "core/domain/settings_table.h"
#include "cost_duty.h"
#include "app/notice.h"
#include "app/settings_access.h"
#include "llm/llm_pure.h"
#include "util/log.h"
#include "util/text.h"
#include "word_answer.h"

namespace lens::app {
namespace {

QString nowMinute()
{
    return QString::fromStdString(util::localMinuteNow());
}

/// @return The sentence's runs as the bubble draws them: a {text, role} map per run, in order.
QVariantList breakdownSegments(QString const& sentence, QVector<llm::RoleSpan> const& spans)
{
    QVariantList segments;
    for (auto const& run : llm::breakdownRuns(sentence, spans))
        segments.append(QVariantMap{{"text", run.text}, {"role", run.role}});
    return segments;
}

/// @return The corrected text as runs: a changed word carries the role "changed", which the bubble underlines.
QVariantList correctionSegments(QString const& original, QString const& corrected)
{
    QVariantList segments;
    for (auto const& run : llm::correctionRuns(original, corrected))
        segments.append(QVariantMap{{"text", run.text}, {"role", run.changed ? QStringLiteral("changed") : QString{}}});
    return segments;
}

}

ExplanationDuty::ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, QObject* parent)
    : QObject(parent), storage_(storage), llm_(llm), cost_(cost), cache_(storage.knownStore())
{
    connect(&llm_, &llm::LlmClient::batchFinished, this, &ExplanationDuty::onBatchFinished);
    connect(&llm_, &llm::LlmClient::failed, this, &ExplanationDuty::onFailed);
    connect(&llm_, &llm::LlmClient::partial, this, &ExplanationDuty::onPartial);
    connect(&llm_, &llm::LlmClient::peekAnswered, this, &ExplanationDuty::onPeekAnswered);
    connect(&llm_, &llm::LlmClient::senseAnswered, this, &ExplanationDuty::onNextSenseAnswered);
    connect(&llm_, &llm::LlmClient::breakdownAnswered, this, &ExplanationDuty::onBreakdownAnswered);
    connect(&llm_, &llm::LlmClient::correctionAnswered, this, &ExplanationDuty::onCorrectionAnswered);
}

void ExplanationDuty::onPartial(llm::Explanation partial)
{
    // A preview is only ever the answer to the batch in flight: the model-list fetch, and a reply
    // for a request already disposed of, reach nothing here.
    if (not queue_.inFlight())
        return;
    const auto now     = cache_.readerContext();
    const auto subject = queue_.liveSubject(partial.title, now);
    if (not subject)
        return;
    // A step of the chain is drawn whole or not at all: a preview would put half a definition on the chain.
    if (subject->chain)
        return;
    // The title is the reader's own text rather than the model's echo, the same rule the finished
    // answer follows, so the card does not rename itself part-way through the stream.
    partial.title = isWordKind(subject->kind)
                        ? subject->lemma
                        : (subject->surface.isEmpty() ? subject->text : subject->surface);
    // The sense mode reaches the card masked by the kind, exactly as it does for a finished answer.
    const auto display = core::ExplanationCache::masked(now, subject->kind);
    showBubble(partial, *subject, display, /*notifyPet=*/false);
    // Once per request, and after the staleness rule above has spoken: what this answers is whether
    // the reader's provider streams at all -- the reader's own report was a card that filled in one
    // jump, and a provider that answers in one body looks exactly like that at this level.
    if (not previewSeen_) {
        previewSeen_ = true;
        LENS_TRACE("the first preview of the request in flight arrived: '{}'", partial.title.toStdString());
    }
}

void ExplanationDuty::onBatchFinished(QVector<llm::Explanation> results, llm::Usage const usage)
{
    // The client answers the model-list fetch through the same signal, and that is not an
    // explanation anybody asked for.
    if (not queue_.inFlight())
        return;

    QStringList titles;
    for (const auto& result : results)
        titles.append(result.title);
    const auto settlement = queue_.settled(titles, cache_.readerContext());

    busyLabel_.clear();
    emit busyChanged();
    cost_.recordUsage(nowMinute().toStdString(), usage);

    if (results.isEmpty()) {
        LENS_WARN("the model returned no explanation");
        storage_.save();
        emit statsChanged();
        queue_.complete();
        sendNext();
        return;
    }

    for (int index = 0; index < settlement.resultFor.size(); ++index) {
        const int               answer   = settlement.resultFor.at(index);
        const PendingSelection& selected = settlement.batch.items.at(index);
        if (answer < 0 or not isWordKind(selected.kind))
            continue;
        const llm::Explanation& result = results.at(answer);
        // The example is stored from the batch's own flag, not the setting as it stands now: an
        // answer paid for survives the switch turning off mid-request (docs/adr/0033).
        cache_.store(selected.lemma.toStdString(), word_answer::toCache(result, settlement.batch.example), settlement.batch.context);
    }

    if (settlement.subject)
        showBubble(results.front(), *settlement.subject, settlement.batch.context);

    storage_.save();
    emit statsChanged();
    queue_.complete();
    sendNext();
}

void ExplanationDuty::onFailed(QString const& message)
{
    if (not queue_.inFlight())
        return;
    const auto subject = queue_.refusalSubject();
    busyLabel_.clear();
    emit busyChanged();
    if (subject and subject->chain)
        emit chainFailed(subject->lemma);
    else if (subject)
        showNotice(noticeTitle(*subject), message, kNoticeError);
    queue_.complete();
    sendNext();
}

void ExplanationDuty::setSelection(PendingSelection selection)
{
    pending_ = std::move(selection);
    queue_.selectionChanged();
}

void ExplanationDuty::invalidateWord(QString const& lemma)
{
    const auto value = lemma.trimmed();
    if (value.isEmpty())
        return;
    queue_.invalidate(value);
    // The senses the cycle bought for the word, the peek lines and the requests out for it are
    // answers about a word that no longer exists: they go with the cache entry.
    if (session_.invalidateWord(value))
        clearBubble();
}

void ExplanationDuty::enqueueScanCandidates(QVariantList candidates, QPoint anchor)
{
    QVector<PendingSelection> misses;
    // The scan's candidates are words by construction -- the word list is the one thing that fills
    // this in -- so the one context they are all judged under is the word channel's.
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    for (const auto& value : candidates) {
        const auto       map = value.toMap();
        PendingSelection candidate{.anchor  = anchor,
                                   .kind    = core::SelectionKind::Word,
                                   .surface = map.value("surface").toString(),
                                   .lemma   = map.value("lemma").toString()};
        if (candidate.lemma.isEmpty())
            continue;
        const auto found = cache_.look(candidate.lemma.toStdString(), context);
        if (found.state == core::ExplanationCache::State::Hit) {
            storage_.statsStore().recordPop(candidate.lemma.toStdString(), nowMinute().toStdString());
            if (bubble_.isEmpty() and not queue_.inFlight())
                showBubble(word_answer::fromCache(found.entry, candidate.lemma), candidate, context, /*notifyPet=*/true, /*reveal=*/false);
            continue;
        }
        misses.push_back(std::move(candidate));
    }
    queue_.addScan(std::move(misses));
    storage_.save();
    emit statsChanged();
    sendNext();
}

void ExplanationDuty::resumeQueued()
{
    // The queue answers nothing while an answer is still being disposed of, which is what keeps a
    // budget rollover arriving mid-handler from starting a second request.
    sendNext();
}

void ExplanationDuty::runSelectionAction(QString action, QString text)
{
    if (pending_.text.isEmpty()) {
        LENS_WARN("action '{}' arrived with no selection pending", action.toStdString());
        return;
    }
    if (action == QLatin1String("copy")) {
        QGuiApplication::clipboard()->setText(pending_.text);
        pending_ = PendingSelection{};
        queue_.selectionChanged();
        return;
    }
    if (action == QLatin1String("breakdown")) {
        // Only a sentence has a breakdown preset: a word carrying it would ask the word channel for a prompt it does not have.
        if (pending_.kind == core::SelectionKind::Sentence) pending_.preset = kBreakdownPreset;
        breakdown(pending_);
        return;
    }
    if (action == QLatin1String("spell")) {
        correct(pending_, kSpellPreset);
        return;
    }
    if (action == QLatin1String("grammar")) {
        correct(pending_, kGrammarPreset);
        return;
    }
    if (action != QLatin1String("translate") && action != QLatin1String("explain")) return;
    if (text != pending_.text)
        LENS_WARN("selection action text differed from the captured selection; using the captured text");
    if (pending_.kind == core::SelectionKind::Sentence) pending_.preset = action;
    explain(pending_);
}

void ExplanationDuty::reviewWord(QString lemma, QPoint const anchor, QSize const anchorSize)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    reviewAnchor_      = anchor;
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    const auto found   = cache_.look(value.toStdString(), context);
    if (found.state != core::ExplanationCache::State::Missing) {
        PendingSelection pending{.anchor = anchor, .anchorSize = anchorSize, .kind = core::SelectionKind::Word, .surface = value, .lemma = value};
        showBubble(word_answer::fromCache(found.entry, value), pending, context, /*notifyPet=*/true, /*reveal=*/false, CardKind::Review);
        return;
    }
    // The one thing on this path that reaches the model, and it is a button rather than the hover
    // itself: a pointer crossing the list must not spend the reader's money. The two strings are
    // spelled in the controller's context, where this file's other reader-facing ones already are;
    // the context is written out at each call because lupdate reads literal arguments only.
    showNotice(value,
               QCoreApplication::translate("lens::app::AppController", "No explanation is stored for this word in the current language."),
               QString::fromLatin1(kNoticeInfo),
               QCoreApplication::translate("lens::app::AppController", "Explain now"),
               value);
}

void ExplanationDuty::showReviewAnswer(QString const& lemma, QPoint const anchor)
{
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    const auto found   = cache_.look(lemma.toStdString(), context);
    if (found.state != core::ExplanationCache::State::Hit)
        return;
    PendingSelection pending{.anchor = anchor, .kind = core::SelectionKind::Word, .surface = lemma, .lemma = lemma};
    showBubble(word_answer::fromCache(found.entry, lemma), pending, context, /*notifyPet=*/false, /*reveal=*/false, CardKind::ReviewAnswer);
}

void ExplanationDuty::dismissReview()
{
    // A pointer on the card is reading it -- and the entry at its foot is there to be pressed --
    // so the dismissal waits for the pointer to leave the card, which the card itself reports.
    if (session_.dismissReview() == CardSession::ReviewDismissal::Dismiss)
        clearBubble();
}

void ExplanationDuty::dismissReviewNow()
{
    if (session_.dismissReviewNow())
        clearBubble();
}

void ExplanationDuty::holdReviewCard(bool const on)
{
    // The pointer has left the card as well: a dismissal that waited for it lands.
    if (session_.holdReviewCard(on))
        clearBubble();
}

void ExplanationDuty::explainLemma(QString lemma)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    PendingSelection pending{.anchor = reviewAnchor_, .kind = core::SelectionKind::Word, .surface = value, .lemma = value};
    explain(pending);
}

void ExplanationDuty::explainChainWord(QString lemma)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty())
        return;
    if (auto const step = session_.chainStep(value))
        askChainStep(*step);
}

void ExplanationDuty::askChainStep(PendingSelection pending)
{
    // Out of budget with nothing stored: explain() would drop the step without a word, and the card would
    // wait on it for ever. Say so, and the card shows the failure where the answer would have gone.
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    if (not cost_.canRequest()) {
        if (not showStored(pending, context))
            emit chainFailed(pending.lemma);
        return;
    }
    explain(pending);
}

bool ExplanationDuty::chainStanding(PendingSelection const& selection) const
{
    return session_.chainStanding(selection.parent);
}

QVariantMap ExplanationDuty::chainEntry(llm::Explanation const& explanation, core::CacheContext const& context) const
{
    const word_answer::Switches switches{.ipa          = showsCardBlock("ipa"),
                                         .signpost     = showsCardBlock("signpost"),
                                         .collocations = showsCardBlock("collocations")};
    return word_answer::chainEntry(explanation, QString::fromStdString(context.language), switches, storage_.knownStore().level());
}

void ExplanationDuty::requestPeek()
{
    CardSession::PeekFacts facts;
    facts.now    = cache_.readerContext();
    facts.mayAsk = cost_.canRequest();
    facts.mother = motherTongue();
    // The word channel always buys the Chinese pair along with the explanation, so a Chinese
    // mother tongue is answered by the entry the standing card came from: the reader held a globe
    // the answer had already paid for. The cache is keyed by the explanation language, which is
    // exactly the English the globe exists under.
    if (facts.mother == QLatin1String("zh")) {
        if (auto const* card = session_.wordCard() ? session_.subject() : nullptr) {
            const auto found = cache_.look(card->lemma.toStdString(), cache_.contextFor(core::SelectionKind::Word));
            if (found.state != core::ExplanationCache::State::Missing)
                facts.ownZh = QString::fromStdString(found.entry.zh);
        }
    }

    const auto step = session_.requestPeek(facts);
    switch (step.kind) {
        case CardSession::PeekStep::Kind::Show: emit peekArrived(step.line); break;
        case CardSession::PeekStep::Kind::Ask: llm_.peekWord(step.lemma, step.language, step.senseEn); break;
        case CardSession::PeekStep::Kind::Waiting: break;
        case CardSession::PeekStep::Kind::None:
            // Out of budget the hold buys nothing and says nothing: the definition on the card is the
            // reader's answer either way, and a notice is a whole surface, not a line under it.
            if (step.why == CardSession::PeekStep::Why::Budget and session_.subject())
                LENS_DEBUG("the peek for '{}' waits: today's budget is spent", session_.subject()->lemma.toStdString());
            break;
    }
}

void ExplanationDuty::onPeekAnswered(QString const& lemma, QString const& line, llm::Usage const usage)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    if (auto const shown = session_.peekAnswered(lemma, line, cache_.readerContext()))
        emit peekArrived(*shown);
}

QString ExplanationDuty::motherTongue() const
{
    // A Choice row, so a hand-edited value the table does not list reads as the row's default
    // rather than reaching the peek prompt raw.
    return settings::read(storage_.section(), *core::settings::find("motherTongue")).toString();
}

void ExplanationDuty::cycleSense()
{
    const auto step = session_.cycleSense(cache_.readerContext(), cost_.canRequest());
    switch (step.kind) {
        case CardSession::SenseStep::Kind::Swap:
            emitSenseSwapped(step.view, step.language);
            break;
        case CardSession::SenseStep::Kind::Ask:
            LENS_INFO("asking for the next sense of '{}' ({} known, advance={})", step.lemma.toStdString(), step.known.size(), step.advance);
            llm_.nextSenseWord(step.lemma, step.lemma + QLatin1Char('\n') + step.known.join(QLatin1Char('\n')), step.language);
            break;
        case CardSession::SenseStep::Kind::None:
            // Out of budget the click buys nothing and says nothing: the definition on the card is
            // the reader's answer either way, the same quiet the held peek keeps (docs/adr/0112).
            if (step.why == CardSession::SenseStep::Why::Budget and session_.subject())
                LENS_DEBUG("the next sense of '{}' waits: today's budget is spent", session_.subject()->lemma.toStdString());
            break;
    }
}

void ExplanationDuty::ensureNextSense()
{
    const auto step = session_.ensureNextSense(cache_.readerContext(), cost_.canRequest());
    if (step.kind == CardSession::SenseStep::Kind::Ask)
        llm_.nextSenseWord(step.lemma, step.lemma + QLatin1Char('\n') + step.known.join(QLatin1Char('\n')), step.language);
}

void ExplanationDuty::onNextSenseAnswered(QString const& lemma, llm::Explanation const& answer, llm::Usage const usage)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    const auto outcome = session_.senseAnswered(lemma, answer, usage, cache_.readerContext());
    switch (outcome.result) {
        case CardSession::SenseOutcome::Result::Refused:
            // An answer that never reached the model leaves the entry up to ask again.
            LENS_DEBUG("the next sense of '{}' was refused; the entry stays", lemma.toStdString());
            break;
        case CardSession::SenseOutcome::Result::Exhausted:
            if (outcome.repeated)
                LENS_INFO("the model repeated a sense of '{}'; the cycle is exhausted", lemma.toStdString());
            else
                LENS_INFO("the model has no further sense for '{}'", lemma.toStdString());
            break;
        case CardSession::SenseOutcome::Result::Grown:
        case CardSession::SenseOutcome::Result::None:
            break;
    }
    if (outcome.view)
        emitSenseSwapped(*outcome.view, outcome.language);
}

void ExplanationDuty::emitSenseSwapped(CardSession::CycleView const& view, QString const& language)
{
    const bool  showSignpost = showsCardBlock("signpost");
    const bool  showPictures = showsCardBlock("pictures");
    QVariantMap row          = word_answer::senseRow(view.sense, language, showPictures, storage_.knownStore().level());
    // The payload keeps agreeing with the card, so a refresh of the same card -- a verdict, a
    // replay -- draws the sense the reader moved to, not the answer's own first one.
    bubble_.insert(QStringLiteral("senses"), QVariantList{row});
    bubble_.insert(QStringLiteral("signpost"), showSignpost ? view.label : QString{});
    bubble_.insert(QStringLiteral("senseCycle"), view.offered);
    bubble_.insert(QStringLiteral("senseIndex"), view.index);
    bubble_.insert(QStringLiteral("senseHint"), view.hint);
    bubble_.insert(QStringLiteral("senseExhausted"), view.exhausted);
    emit senseSwapped(QVariantMap{{"cycle", view.offered},
                                  {"index", view.index},
                                  {"hint", view.hint},
                                  {"exhausted", view.exhausted},
                                  {"row", row},
                                  {"signpost", view.label}});
}

void ExplanationDuty::breakdown(PendingSelection const& pending)
{
    // The drawer offers the breakdown only where it applies; the check here keeps a stale action from
    // reaching the model.
    if (pending.kind != core::SelectionKind::Sentence or not llm::breakdownApplies(pending.text)) return;
    // Out of budget the facade has already said why, and a breakdown that cannot be bought draws nothing.
    if (not cost_.canRequest()) return;
    // A card already standing on this sentence has its answer or is waiting for it: asking again would buy
    // it twice. Only a failed one is asked again.
    auto const* card = session_.subject();
    if (card != nullptr and card->preset == kBreakdownPreset and card->text == pending.text and
        bubble_.value(QStringLiteral("breakdown")).toString() != QLatin1String("failed"))
        return;
    showBreakdown(pending, QStringLiteral("pending"), QVariantList{QVariantMap{{"text", pending.text}, {"role", QString{}}}}, {}, {});
    llm_.breakdownSentence(pending.text, llm::breakdownWords(pending.text));
}

void ExplanationDuty::correct(PendingSelection const& pending, QString const& preset)
{
    // The drawer offers each correction only where it applies; the check here keeps a stale action from
    // reaching the model. Spelling takes one token, grammar an English sentence.
    const bool applies = preset == kSpellPreset ? llm::spellingApplies(pending.text) : llm::grammarApplies(pending.text);
    if (not applies) return;
    if (not cost_.canRequest()) return;
    // A card already standing on this text has its answer or is waiting for it: asking again would buy it twice.
    auto const* card = session_.subject();
    if (card != nullptr and card->preset == preset and card->text == pending.text and
        bubble_.value(QStringLiteral("breakdown")).toString() != QLatin1String("failed"))
        return;
    PendingSelection selection = pending;
    selection.preset           = preset;
    showCorrection(selection, QStringLiteral("pending"), {}, {});
    if (preset == kSpellPreset)
        llm_.correctSpelling(pending.text);
    else
        llm_.correctGrammar(pending.text);
}

void ExplanationDuty::onCorrectionAnswered(QString const& text, llm::Channel const channel, QString const& corrected, llm::Usage const usage, QString const& failure)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    // The answer belongs to the card standing on this text and no other, as the breakdown's does.
    auto const*   card   = session_.subject();
    const QString preset = channel == llm::Channel::Spell ? kSpellPreset : kGrammarPreset;
    if (card == nullptr or card->preset != preset or card->text != text) return;
    const PendingSelection selection = *card;
    if (corrected.isEmpty()) {
        showCorrection(selection, QStringLiteral("failed"), {}, failure);
        return;
    }
    showCorrection(selection, QStringLiteral("ready"), correctionSegments(text, corrected), {});
}

void ExplanationDuty::showCorrection(PendingSelection const& selection, QString const& state, QVariantList const& segments, QString const& failure)
{
    // The correction reuses the breakdown's bubble shape: its runs draw as the sentence, with no legend and no
    // meaning. The preset tells the label apart, so the card names the correction it is showing.
    const bool  sameCard = session_.raise(selection, CardKind::Selection, selection.text);
    const auto  face     = session_.presentation();
    QVariantMap payload{{"title", QString{}},
                        {"type", kindName(selection.kind)},
                        {"cardKind", cardKindName(face.kind)},
                        {"sameCard", sameCard},
                        {"breakdown", state},
                        {"correction", selection.preset},
                        {"segments", segments},
                        {"roles", QStringList{}},
                        {"failure", failure},
                        {"en", QString{}},
                        {"zh", QString{}},
                        {"translation", QString{}},
                        {"senses", QVariantList{}},
                        {"status", QString{}},
                        {"countsDown", face.countsDown},
                        {"reveal", false},
                        {"verdictButtons", false},
                        {"anchorWidth", selection.anchorSize.width()},
                        {"anchorHeight", selection.anchorSize.height()},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    bubble_ = payload;
    clearNotice();
    emit bubbleChanged();
    if (state != QLatin1String("pending"))
        emit petEvent(core::pet::PetEvent::ExplanationShown);
}

void ExplanationDuty::onBreakdownAnswered(QString const& sentence, QVector<llm::RoleSpan> const& spans, llm::Usage const usage, QString const& failure)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    // The answer belongs to the card standing on this sentence and to nothing else: the reader may have
    // moved on, and a breakdown is never kept for later.
    auto const* card = session_.subject();
    if (card == nullptr or card->preset != kBreakdownPreset or card->text != sentence) return;
    const PendingSelection selection = *card;
    if (spans.isEmpty()) {
        showBreakdown(selection, QStringLiteral("failed"), {}, {}, failure);
        return;
    }
    showBreakdown(selection, QStringLiteral("ready"), breakdownSegments(sentence, spans), llm::breakdownRoles(spans), {});
}

void ExplanationDuty::showBreakdown(PendingSelection const& selection, QString const& state, QVariantList const& segments, QStringList const& roles, QString const& failure)
{
    const bool sameCard = session_.raise(selection, CardKind::Selection, selection.text);
    const auto face     = session_.presentation();
    // The sentence is the card's text: no title, no meaning, no verdict. The keys the bubble draws the
    // meaning from stay present and empty, so the card is one shape whatever it is showing.
    QVariantMap payload{{"title", QString{}},
                        {"type", kindName(selection.kind)},
                        {"cardKind", cardKindName(face.kind)},
                        {"sameCard", sameCard},
                        {"breakdown", state},
                        {"segments", segments},
                        {"roles", roles},
                        {"failure", failure},
                        {"en", QString{}},
                        {"zh", QString{}},
                        {"translation", QString{}},
                        {"senses", QVariantList{}},
                        {"status", QString{}},
                        {"countsDown", face.countsDown},
                        {"reveal", false},
                        {"verdictButtons", false},
                        {"anchorWidth", selection.anchorSize.width()},
                        {"anchorHeight", selection.anchorSize.height()},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    bubble_ = payload;
    clearNotice();
    emit bubbleChanged();
    // The pet reacts once, to an answer that arrived; a card still waiting on one makes no arrival yet.
    if (state != QLatin1String("pending"))
        emit petEvent(core::pet::PetEvent::ExplanationShown);
}

void ExplanationDuty::explain(PendingSelection const& pending)
{
    // A request that arrives while another is in flight is held, and judged when its turn comes:
    // the answer the queue is waiting for may be the one that pays for this word.
    if (queue_.inFlight()) {
        queue_.request(pending);
        return;
    }
    const auto context = cache_.contextFor(pending.kind);
    if (showStored(pending, context))
        return;
    // Nothing is in flight and nothing is stored, so this one goes out now or not at all: a
    // request out of budget is dropped rather than left waiting for a budget that may be a day
    // away, and the facade has already said why.
    if (not cost_.canRequest())
        return;
    queue_.request(pending);
    sendNext();
}

bool ExplanationDuty::showStored(PendingSelection const& pending, core::CacheContext const& context)
{
    if (not isWordKind(pending.kind))
        return false;
    const auto found = cache_.look(pending.lemma.toStdString(), context);
    if (found.state != core::ExplanationCache::State::Hit)
        return false;
    storage_.statsStore().recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
    storage_.save();
    showBubble(word_answer::fromCache(found.entry, pending.lemma), pending, context, /*notifyPet=*/true, /*reveal=*/false);
    emit statsChanged();
    return true;
}

void ExplanationDuty::send(ExplanationBatch batch)
{
    const PendingSelection& first = batch.items.front();
    // A word is what is left when the kind names no other channel, which is what the word list's
    // own candidates carry.
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
    request.etymology      = isWordKind(first.kind) && etymologyEnabled();
    request.example        = isWordKind(first.kind) && batch.example;
    QStringList inputs;
    for (const auto& item : batch.items) {
        inputs << (isWordKind(item.kind) ? item.lemma : item.text);
        if (isWordKind(item.kind))
            storage_.statsStore().recordPop(item.lemma.toStdString(), nowMinute().toStdString());
    }
    request.words = std::move(inputs);
    // The request goes out with nothing previewed yet: the first preview of this one is the line
    // onPartial() writes.
    previewSeen_ = false;
    busyLabel_   = QCoreApplication::translate("lens::app::AppController", "Explaining…");
    emit busyChanged();
    emit petEvent(core::pet::PetEvent::ExplanationRequested);
    storage_.save();
    emit statsChanged();
    llm_.explainWords(std::move(request));
}

void ExplanationDuty::sendNext()
{
    const auto now   = cache_.readerContext();
    auto       batch = queue_.take(cost_.canRequest(), now, exampleEnabled());
    if (not batch)
        return;
    // A held request comes back out through the cache before it goes to the model: the batch that
    // has just landed may be the one that stored this very word. Nothing else is ever taken here --
    // the word list's own candidates were answered out of the cache before they were queued.
    if (not batch->fromScan and showStored(batch->items.front(), batch->context)) {
        queue_.complete();
        return;
    }
    send(std::move(*batch));
}

void ExplanationDuty::showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool const notifyPet, bool const reveal, CardKind const kind)
{
    // A step of the chain goes onto the chain under the card and leaves the card alone: its text, its
    // verdict and its countdown are the card's own, and a step asked from a card that has moved on is dropped.
    if (selection.chain) {
        if (chainStanding(selection))
            emit chainAnswered(chainEntry(explanation, context));
        return;
    }
    // A translation is one line of English whatever the explanation language says: the reader
    // selected Chinese, the model was asked for English alone, and the other two language fields
    // are answers the reader did not ask for. The bubble then draws that line and nothing else --
    // no title, no badge, none of the word-only blocks.
    const bool    translated  = selection.kind == core::SelectionKind::Translation;
    const QString english     = translated or context.language == "en" ? explanation.en : QString{};
    const QString chinese     = context.language == "zh" and not translated ? explanation.zh : QString{};
    const QString translation = context.language != "en" and context.language != "zh" and not translated ? explanation.translation
                                                                                                         : QString{};
    // The same card refreshed -- a stream chunk, or a language switch replaying the answer -- is
    // not a new surface that has to be placed: the reader is already reading it where it stands.
    const bool  sameCard = session_.raise(selection, kind, explanation.title);
    const auto  face     = session_.presentation();
    QVariantMap payload{{"title", explanation.title},
                        {"type", kindName(selection.kind)},
                        {"cardKind", cardKindName(face.kind)},
                        {"sameCard", sameCard},
                        {"en", english},
                        {"zh", chinese},
                        {"translation", translation},
                        {"countsDown", face.countsDown},
                        {"reveal", reveal},
                        {"verdictButtons", face.offersVerdicts},
                        {"anchorWidth", selection.anchorSize.width()},
                        {"anchorHeight", selection.anchorSize.height()},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    if (isWordKind(selection.kind)) {
        // Display switches, read as the card is drawn: the answer keeps every field, and these
        // decide only which of them reach the payload (see settings_table.h).
        const word_answer::Switches switches{.ipa          = showsCardBlock("ipa"),
                                             .pos          = showsCardBlock("pos"),
                                             .signpost     = showsCardBlock("signpost"),
                                             .collocations = showsCardBlock("collocations"),
                                             .pictures     = showsCardBlock("pictures"),
                                             .etymology    = etymologyEnabled(),
                                             .example      = exampleEnabled()};
        const QString               language = QString::fromStdString(context.language);
        const QVariantMap           fields   = word_answer::cardFields(explanation, language, switches, storage_.knownStore().level());
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            payload.insert(it.key(), it.value());
        QVariantList senses;
        if (context.multipleSenses) {
            // The rows are the sense selector themselves (UI-SPEC.md 4.3), and there is no
            // Not-the-Sense? entry beside them to be one.
            auto entries = explanation.senses;
            if (entries.isEmpty())
                entries.push_back({explanation.en, explanation.zh, explanation.translation, QString{}, explanation.imageable, explanation.imageQuery});
            for (const auto& sense : entries) {
                if (senses.size() >= llm::kMaxSenses)
                    break;
                senses.append(word_answer::senseRow(sense, language, switches.pictures, storage_.knownStore().level()));
            }
            payload.insert(QStringLiteral("senseCycle"), false);
            payload.insert(QStringLiteral("senseIndex"), 0);
            payload.insert(QStringLiteral("senseHint"), QString{});
            payload.insert(QStringLiteral("senseExhausted"), false);
        } else {
            // The one sense the card draws is the Not-the-Sense? entry's to move (UI-SPEC.md 4.3):
            // the session keeps the senses bought for the word, the answer's own first, and the
            // card draws the one at the cycle's index.
            if (auto const cycle = session_.holdAnswer(context, explanation)) {
                senses.append(word_answer::senseRow(cycle->sense, language, switches.pictures, storage_.knownStore().level()));
                payload.insert(QStringLiteral("senseCycle"), cycle->offered);
                payload.insert(QStringLiteral("senseIndex"), cycle->index);
                payload.insert(QStringLiteral("senseHint"), cycle->hint);
                payload.insert(QStringLiteral("senseExhausted"), cycle->exhausted);
            }
        }
        payload.insert(QStringLiteral("senses"), senses);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(explanation.title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    // The session raised this card a step earlier, which is what settled its identity and reset
    // the review life: a scan bubble must not be taken down by the pointer leaving a word the
    // reader has moved on from.
    bubble_ = payload;
    clearNotice();
    emit bubbleChanged();
    if (notifyPet)
        emit petEvent(core::pet::PetEvent::ExplanationShown);
}

void ExplanationDuty::showNotice(QString const& title, QString const& body, QString const& kind, QString const& action, QString const& lemma)
{
    clearBubble();
    notice_ = noticePayload(title, body, kind, action, lemma);
    emit noticeChanged();
}

QString ExplanationDuty::noticeTitle(PendingSelection const& pending) const
{
    // A sentence and a translation have no short name to head the card with -- the pending text is
    // the whole of what the reader selected, and a paragraph of it would be the card. A word and an
    // entity are named by the one or two words they are.
    return pending.kind == core::SelectionKind::Sentence or pending.kind == core::SelectionKind::Translation
               ? QCoreApplication::translate("lens::app::AppController", "Request failed")
               : (pending.surface.isEmpty() ? pending.text : pending.surface);
}

void ExplanationDuty::clearNotice()
{
    if (notice_.isEmpty()) return;
    notice_.clear();
    emit noticeChanged();
}

void ExplanationDuty::clearBubble()
{
    // Every way a bubble goes -- the countdown, the cross, a deletion, a notice replacing it --
    // leaves nothing for the pointer to be holding afterwards.
    session_.clear();
    if (bubble_.isEmpty()) return;
    bubble_.clear();
    emit bubbleChanged();
    emit petEvent(core::pet::PetEvent::ExplanationHidden);
}

void ExplanationDuty::mark(QString lemma, bool learned)
{
    auto const* card = session_.subject();
    if (not bubble_.isEmpty() and card != nullptr and card->kind != core::SelectionKind::Word and bubble_.value("title").toString() == lemma)
        return;
    const std::string key = lemma.toStdString();
    storage_.knownStore().mark(key, learned);
    storage_.statsStore().recordVerdict(key, nowMinute().toStdString(), learned ? "known" : "new");
    storage_.save();
    if (bubble_.value("title").toString() == lemma) bubble_["status"] = learned ? QStringLiteral("known") : QStringLiteral("new");
    emit bubbleChanged();
    emit statsChanged();
    emit petEvent(learned ? core::pet::PetEvent::KnownMarked : core::pet::PetEvent::NewWordMarked);
}

void ExplanationDuty::dismissBubble()
{
    clearBubble();
}

void ExplanationDuty::dismissNotice()
{
    clearNotice();
}

QVariantMap ExplanationDuty::bubble() const
{
    return bubble_;
}
QVariantMap ExplanationDuty::notice() const
{
    return notice_;
}
QString ExplanationDuty::busyLabel() const
{
    return busyLabel_;
}

bool ExplanationDuty::etymologyEnabled() const
{
    return settings::read(storage_.section(), *core::settings::find("etymology")).toBool();
}

bool ExplanationDuty::exampleEnabled() const
{
    return settings::read(storage_.section(), *core::settings::find("example")).toBool();
}

bool ExplanationDuty::showsCardBlock(char const* key) const
{
    return settings::read(storage_.section(), *core::settings::find(key)).toBool();
}

}
