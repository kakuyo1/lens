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

/// @return The sentence as one unmarked run, the shape a card shows while it waits or when its answer failed.
QVariantList plainSegments(QString const& sentence)
{
    return QVariantList{QVariantMap{{"text", sentence}, {"role", QString{}}}};
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
    : QObject(parent), storage_(storage), llm_(llm), cost_(cost), exchange_(storage.knownStore())
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
    if (not exchange_.inFlight())
        return;
    const auto now     = exchange_.readerContext();
    const auto subject = exchange_.liveSubject(partial.title, now);
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
    showBubble(exchange_.raise(*subject, partial.title, partial, display, CardKind::Selection, /*reveal=*/true, /*notifyPet=*/false));
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
    if (not exchange_.inFlight())
        return;

    busyLabel_.clear();
    emit busyChanged();
    cost_.recordUsage(nowMinute().toStdString(), usage);

    auto const outcome = exchange_.settle(results, facts());
    if (results.isEmpty())
        LENS_WARN("the model returned no explanation");
    storage_.save();
    emit statsChanged();
    if (not results.isEmpty())
        apply(outcome);
    sendNext();
}

void ExplanationDuty::onFailed(QString const& message)
{
    if (not exchange_.inFlight())
        return;
    const auto subject = exchange_.refuse();
    busyLabel_.clear();
    emit busyChanged();
    if (subject and subject->chain)
        emit chainFailed(subject->lemma);
    else if (subject)
        showNotice(noticeTitle(*subject), message, kNoticeError);
    sendNext();
}

void ExplanationDuty::setSelection(PendingSelection selection)
{
    exchange_.setSelection(std::move(selection));
}

void ExplanationDuty::invalidateWord(QString const& lemma)
{
    const auto value = lemma.trimmed();
    if (value.isEmpty())
        return;
    // The senses the cycle bought for the word, the peek lines and the requests out for it are
    // answers about a word that no longer exists: they go with the cache entry.
    if (exchange_.invalidateWord(value))
        clearBubble();
}

void ExplanationDuty::enqueueScanCandidates(QVariantList candidates, QPoint anchor)
{
    // The scan's candidates are words by construction -- the word list is the one thing that fills
    // this in -- so the one context they are all judged under is the word channel's.
    for (auto const& value : candidates) {
        const auto       map = value.toMap();
        PendingSelection candidate{.anchor  = anchor,
                                   .kind    = core::SelectionKind::Word,
                                   .surface = map.value("surface").toString(),
                                   .lemma   = map.value("lemma").toString()};
        if (candidate.lemma.isEmpty())
            continue;
        apply(exchange_.ask(std::move(candidate), AskOrigin::Scan, facts()), /*save=*/false);
    }
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
    auto const& pending = exchange_.selection();
    if (pending.text.isEmpty()) {
        LENS_WARN("action '{}' arrived with no selection pending", action.toStdString());
        return;
    }
    if (action == QLatin1String("copy")) {
        QGuiApplication::clipboard()->setText(pending.text);
        exchange_.clearSelection();
        return;
    }
    if (action == QLatin1String("breakdown")) {
        // Only a sentence has a breakdown preset: a word carrying it would ask the word channel for a prompt it does not have.
        if (pending.kind == core::SelectionKind::Sentence) {
            auto selection   = pending;
            selection.preset = kBreakdownPreset;
            breakdown(selection);
        } else {
            breakdown(pending);
        }
        return;
    }
    if (action == QLatin1String("spell")) {
        correct(pending, kSpellPreset);
        return;
    }
    if (action == QLatin1String("grammar")) {
        correct(pending, kGrammarPreset);
        return;
    }
    if (action != QLatin1String("translate") && action != QLatin1String("explain")) return;
    if (text != pending.text)
        LENS_WARN("selection action text differed from the captured selection; using the captured text");
    auto selection = pending;
    if (selection.kind == core::SelectionKind::Sentence) selection.preset = action;
    explain(selection);
}

void ExplanationDuty::reviewWord(QString lemma, QPoint const anchor, QSize const anchorSize)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    reviewAnchor_     = anchor;
    reviewAnchorSize_ = anchorSize;
    clearWordHint();
    PendingSelection pending{.anchor = anchor, .anchorSize = anchorSize, .kind = core::SelectionKind::Word, .surface = value, .lemma = value};
    apply(exchange_.ask(std::move(pending), AskOrigin::Review, facts()));
}

void ExplanationDuty::showReviewAnswer(QString const& lemma, QPoint const anchor)
{
    PendingSelection pending{.anchor = anchor, .kind = core::SelectionKind::Word, .surface = lemma, .lemma = lemma};
    apply(exchange_.ask(std::move(pending), AskOrigin::ReviewAnswer, facts()));
}

void ExplanationDuty::dismissReview()
{
    // A pointer on the hint is reading it, and its button is there to be pressed, so the row's dismissal waits
    // for the pointer to leave the hint (holdWordHint). Otherwise the hint goes with the word.
    if (wordHintHeld_)
        wordHintDismissPending_ = true;
    else
        clearWordHint();
    // A pointer on the card is reading it -- and the entry at its foot is there to be pressed --
    // so the dismissal waits for the pointer to leave the card, which the card itself reports.
    if (exchange_.card().dismissReview() == CardSession::ReviewDismissal::Dismiss)
        clearBubble();
}

void ExplanationDuty::dismissReviewNow()
{
    clearWordHint();
    if (exchange_.card().dismissReviewNow())
        clearBubble();
}

void ExplanationDuty::holdWordHint(bool const on)
{
    wordHintHeld_ = on;
    // The pointer has left the hint, and the row's dismissal was waiting for it: it lands now.
    if (!on && wordHintDismissPending_)
        clearWordHint();
}

void ExplanationDuty::holdReviewCard(bool const on)
{
    // The pointer has left the card as well: a dismissal that waited for it lands.
    if (exchange_.card().holdReviewCard(on))
        clearBubble();
}

void ExplanationDuty::explainLemma(QString lemma)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    // The button is the hint's answer, so the hint goes as the request does.
    clearWordHint();
    PendingSelection pending{.anchor = reviewAnchor_, .kind = core::SelectionKind::Word, .surface = value, .lemma = value};
    explain(pending);
}

void ExplanationDuty::explainChainWord(QString lemma)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty())
        return;
    if (auto const step = exchange_.card().chainStep(value))
        askChainStep(*step);
}

void ExplanationDuty::askChainStep(PendingSelection pending)
{
    // Out of budget with nothing stored: explain() would drop the step without a word, and the card would
    // wait on it for ever. Say so, and the card shows the failure where the answer would have gone.
    if (not cost_.canRequest()) {
        auto const outcome = exchange_.stored(pending);
        apply(outcome);
        // A stored step was hung on the chain (or its pop counted); only a word with nothing stored
        // has to say the step cannot be answered.
        if (outcome.kind != ExplanationExchange::Outcome::Kind::Chain and outcome.popLemma.isEmpty())
            emit chainFailed(pending.lemma);
        return;
    }
    explain(pending);
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
    const auto step = exchange_.requestPeek(motherTongue(), cost_.canRequest(), exchange_.readerContext());
    switch (step.kind) {
        case CardSession::PeekStep::Kind::Show: emit peekArrived(step.line); break;
        case CardSession::PeekStep::Kind::Ask: llm_.peekWord(step.lemma, step.language, step.senseEn); break;
        case CardSession::PeekStep::Kind::Waiting: break;
        case CardSession::PeekStep::Kind::None:
            // Out of budget the hold buys nothing and says nothing: the definition on the card is the
            // reader's answer either way, and a notice is a whole surface, not a line under it.
            if (step.why == CardSession::PeekStep::Why::Budget and exchange_.card().subject())
                LENS_DEBUG("the peek for '{}' waits: today's budget is spent", exchange_.card().subject()->lemma.toStdString());
            break;
    }
}

void ExplanationDuty::onPeekAnswered(QString const& lemma, QString const& line, llm::Usage const usage)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    if (auto const shown = exchange_.card().peekAnswered(lemma, line, exchange_.readerContext()))
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
    const auto step = exchange_.card().cycleSense(exchange_.readerContext(), cost_.canRequest());
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
            if (step.why == CardSession::SenseStep::Why::Budget and exchange_.card().subject())
                LENS_DEBUG("the next sense of '{}' waits: today's budget is spent", exchange_.card().subject()->lemma.toStdString());
            break;
    }
}

void ExplanationDuty::ensureNextSense()
{
    const auto step = exchange_.card().ensureNextSense(exchange_.readerContext(), cost_.canRequest());
    if (step.kind == CardSession::SenseStep::Kind::Ask)
        llm_.nextSenseWord(step.lemma, step.lemma + QLatin1Char('\n') + step.known.join(QLatin1Char('\n')), step.language);
}

void ExplanationDuty::onNextSenseAnswered(QString const& lemma, llm::Explanation const& answer, llm::Usage const usage)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    const auto outcome = exchange_.card().senseAnswered(lemma, answer, usage, exchange_.readerContext());
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
    auto const* card = exchange_.card().subject();
    if (card != nullptr and card->preset == kBreakdownPreset and card->text == pending.text and
        bubble_.value(QStringLiteral("breakdown")).toString() != QLatin1String("failed"))
        return;
    showBreakdown(pending, QStringLiteral("pending"), plainSegments(pending.text), {}, {});
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
    auto const* card = exchange_.card().subject();
    if (card != nullptr and card->preset == preset and card->text == pending.text and
        bubble_.value(QStringLiteral("breakdown")).toString() != QLatin1String("failed"))
        return;
    PendingSelection selection = pending;
    selection.preset           = preset;
    showCorrection(selection, QStringLiteral("pending"), plainSegments(pending.text), {});
    if (preset == kSpellPreset)
        llm_.correctSpelling(pending.text);
    else
        llm_.correctGrammar(pending.text);
}

void ExplanationDuty::onCorrectionAnswered(QString const& text, llm::Channel const channel, QString const& corrected, llm::Usage const usage, QString const& failure)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    // The answer belongs to the card standing on this text and no other, as the breakdown's does.
    auto const*   card   = exchange_.card().subject();
    const QString preset = channel == llm::Channel::Spell ? kSpellPreset : kGrammarPreset;
    if (card == nullptr or card->preset != preset or card->text != text) return;
    const PendingSelection selection = *card;
    if (corrected.isEmpty()) {
        showCorrection(selection, QStringLiteral("failed"), plainSegments(text), failure);
        return;
    }
    showCorrection(selection, QStringLiteral("ready"), correctionSegments(text, corrected), {});
}

void ExplanationDuty::showCorrection(PendingSelection const& selection, QString const& state, QVariantList const& segments, QString const& failure)
{
    // The correction reuses the breakdown's bubble shape: its runs draw as the sentence, with no legend and no
    // meaning. The preset tells the label apart, so the card names the correction it is showing.
    auto const  view = exchange_.raise(selection, selection.text, llm::Explanation{}, core::CacheContext{}, CardKind::Selection, /*reveal=*/false, /*notifyPet=*/false);
    QVariantMap payload{{"title", QString{}},
                        {"type", kindName(selection.kind)},
                        {"cardKind", cardKindName(view.kind)},
                        {"sameCard", view.sameCard},
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
                        {"countsDown", view.countsDown},
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
    auto const* card = exchange_.card().subject();
    if (card == nullptr or card->preset != kBreakdownPreset or card->text != sentence) return;
    const PendingSelection selection = *card;
    if (spans.isEmpty()) {
        showBreakdown(selection, QStringLiteral("failed"), plainSegments(sentence), {}, failure);
        return;
    }
    showBreakdown(selection, QStringLiteral("ready"), breakdownSegments(sentence, spans), llm::breakdownRoles(spans), {});
}

void ExplanationDuty::showBreakdown(PendingSelection const& selection, QString const& state, QVariantList const& segments, QStringList const& roles, QString const& failure)
{
    auto const view = exchange_.raise(selection, selection.text, llm::Explanation{}, core::CacheContext{}, CardKind::Selection, /*reveal=*/false, /*notifyPet=*/false);
    // The sentence is the card's text: no title, no meaning, no verdict. The keys the bubble draws the
    // meaning from stay present and empty, so the card is one shape whatever it is showing.
    QVariantMap payload{{"title", QString{}},
                        {"type", kindName(selection.kind)},
                        {"cardKind", cardKindName(view.kind)},
                        {"sameCard", view.sameCard},
                        {"breakdown", state},
                        {"segments", segments},
                        {"roles", roles},
                        {"failure", failure},
                        {"en", QString{}},
                        {"zh", QString{}},
                        {"translation", QString{}},
                        {"senses", QVariantList{}},
                        {"status", QString{}},
                        {"countsDown", view.countsDown},
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
    auto const outcome = exchange_.ask(pending, AskOrigin::Selection, facts());
    apply(outcome);
    // A request held behind the one in flight, or a miss the budget dropped, leaves the exchange
    // nothing to draw; the next queued batch may still be sendable.
    if (outcome.kind == ExplanationExchange::Outcome::Kind::None and outcome.popLemma.isEmpty())
        sendNext();
}

void ExplanationDuty::send(llm::ExplainRequest request, ExplanationBatch const& batch)
{
    for (auto const& item : batch.items) {
        if (isWordKind(item.kind))
            storage_.statsStore().recordPop(item.lemma.toStdString(), nowMinute().toStdString());
    }
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
    apply(exchange_.take(facts(), etymologyEnabled()));
}

void ExplanationDuty::showBubble(ExplanationExchange::CardView const& card)
{
    auto const& explanation = card.answer;
    auto const& selection   = card.selection;
    auto const& context     = card.context;
    // A translation is one line of English whatever the explanation language says: the reader
    // selected Chinese, the model was asked for English alone, and the other two language fields
    // are answers the reader did not ask for. The bubble then draws that line and nothing else --
    // no title, no badge, none of the word-only blocks.
    const bool    translated  = selection.kind == core::SelectionKind::Translation;
    const QString english     = translated or context.language == "en" ? explanation.en : QString{};
    const QString chinese     = context.language == "zh" and not translated ? explanation.zh : QString{};
    const QString translation = context.language != "en" and context.language != "zh" and not translated ? explanation.translation
                                                                                                         : QString{};
    QVariantMap   payload{{"title", explanation.title},
                          {"type", kindName(selection.kind)},
                          {"cardKind", cardKindName(card.kind)},
                          {"sameCard", card.sameCard},
                          {"en", english},
                          {"zh", chinese},
                          {"translation", translation},
                          {"countsDown", card.countsDown},
                          {"reveal", card.reveal},
                          {"verdictButtons", card.offersVerdicts},
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
        } else if (card.cycle) {
            // The one sense the card draws is the Not-the-Sense? entry's to move (UI-SPEC.md 4.3):
            // the session keeps the senses bought for the word, the answer's own first, and the
            // card draws the one at the cycle's index.
            auto const& cycle = *card.cycle;
            senses.append(word_answer::senseRow(cycle.sense, language, switches.pictures, storage_.knownStore().level()));
            payload.insert(QStringLiteral("senseCycle"), cycle.offered);
            payload.insert(QStringLiteral("senseIndex"), cycle.index);
            payload.insert(QStringLiteral("senseHint"), cycle.hint);
            payload.insert(QStringLiteral("senseExhausted"), cycle.exhausted);
        }
        payload.insert(QStringLiteral("senses"), senses);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(explanation.title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    // The exchange raised this card a step earlier, which is what settled its identity and reset
    // the review life: a scan bubble must not be taken down by the pointer leaving a word the
    // reader has moved on from.
    bubble_ = payload;
    clearNotice();
    emit bubbleChanged();
    if (card.notifyPet)
        emit petEvent(core::pet::PetEvent::ExplanationShown);
}

ExchangeFacts ExplanationDuty::facts() const
{
    return ExchangeFacts{exchange_.readerContext(), cost_.canRequest(), exampleEnabled()};
}

void ExplanationDuty::apply(ExplanationExchange::Outcome const& outcome, bool const save)
{
    if (not outcome.popLemma.isEmpty()) {
        storage_.statsStore().recordPop(outcome.popLemma.toStdString(), nowMinute().toStdString());
        if (save) {
            storage_.save();
            emit statsChanged();
        }
    }
    switch (outcome.kind) {
        case ExplanationExchange::Outcome::Kind::None: break;
        case ExplanationExchange::Outcome::Kind::Card: showBubble(outcome.card); break;
        case ExplanationExchange::Outcome::Kind::Chain:
            // The exchange only hands a step back while the card it hangs under still stands.
            emit chainAnswered(chainEntry(outcome.answer, outcome.context));
            break;
        case ExplanationExchange::Outcome::Kind::Send: send(outcome.request, outcome.batch); break;
        case ExplanationExchange::Outcome::Kind::Notice:
            // A hover that finds nothing says so beside the word. It asks the model for nothing: the
            // reader's money is spent only by a request they make, and there is no button for one here.
            showWordHint(outcome.noticeLemma);
            break;
    }
}

void ExplanationDuty::showNotice(QString const& title, QString const& body, QString const& kind, QString const& action, QString const& lemma, QString const& intent)
{
    clearBubble();
    notice_ = noticePayload(title, body, kind, action, lemma, intent);
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

void ExplanationDuty::showWordHint(QString const& lemma)
{
    // The strings are spelled in the controller's context, where this file's other reader-facing ones
    // already are; the context is written out at each call because lupdate reads literal arguments only.
    wordHint_ = {
        {"lemma", lemma},
        {"body", QCoreApplication::translate("lens::app::AppController", "No explanation is stored for this word in the current language.")},
        {"action", QCoreApplication::translate("lens::app::AppController", "Explain now")},
        {"x", reviewAnchor_.x()},
        {"y", reviewAnchor_.y()},
        {"anchorWidth", reviewAnchorSize_.width()},
        {"anchorHeight", reviewAnchorSize_.height()},
    };
    emit wordHintChanged();
}

void ExplanationDuty::clearWordHint()
{
    // Whatever held the hint is gone with it; a fresh hint starts with nothing held.
    wordHintHeld_           = false;
    wordHintDismissPending_ = false;
    if (wordHint_.isEmpty()) return;
    wordHint_.clear();
    emit wordHintChanged();
}

void ExplanationDuty::clearBubble()
{
    // Every way a bubble goes -- the countdown, the cross, a deletion, a notice replacing it --
    // leaves nothing for the pointer to be holding afterwards.
    exchange_.card().clear();
    if (bubble_.isEmpty()) return;
    bubble_.clear();
    emit bubbleChanged();
    emit petEvent(core::pet::PetEvent::ExplanationHidden);
}

void ExplanationDuty::mark(QString lemma, bool learned)
{
    auto const* card = exchange_.card().subject();
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

QVariantMap ExplanationDuty::wordHint() const
{
    return wordHint_;
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
