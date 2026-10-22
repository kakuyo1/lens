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
#include "cost_duty.h"
#include "app/notice.h"
#include "util/log.h"
#include "util/text.h"

namespace lens::app {
namespace {

QString nowMinute()
{
    return QString::fromStdString(util::localMinuteNow());
}

llm::Explanation fromCache(core::WordCache const& cached, QString const& lemma)
{
    llm::Explanation result{lemma,
                            QString::fromStdString(cached.ipa),
                            QString::fromStdString(cached.en),
                            QString::fromStdString(cached.zh),
                            QString::fromStdString(cached.translation)};
    result.etymology  = QString::fromStdString(cached.etymology);
    result.signpost   = QString::fromStdString(cached.signpost);
    result.imageable  = cached.imageable;
    result.imageQuery = QString::fromStdString(cached.imageQuery);
    for (auto const& collocation : cached.collocations)
        result.collocations.push_back(QString::fromStdString(collocation));
    if (cached.example) result.example = QString::fromStdString(*cached.example);
    for (auto const& sense : cached.senses) {
        llm::Sense parsed{QString::fromStdString(sense.en), QString::fromStdString(sense.zh), QString::fromStdString(sense.translation)};
        parsed.imageable  = sense.imageable;
        parsed.imageQuery = QString::fromStdString(sense.imageQuery);
        result.senses.push_back(std::move(parsed));
    }
    return result;
}

}

ExplanationDuty::ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, QObject* parent)
    : QObject(parent), storage_(storage), llm_(llm), cost_(cost), cache_(storage.knownStore())
{
    connect(&llm_, &llm::LlmClient::batchFinished, this, &ExplanationDuty::onBatchFinished);
    connect(&llm_, &llm::LlmClient::failed, this, &ExplanationDuty::onFailed);
    connect(&llm_, &llm::LlmClient::partial, this, &ExplanationDuty::onPartial);
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
        // Named rather than positional: five of these six fields are strings, and the one that
        // must not be dropped is the one that arrived last. The example is judged from the batch,
        // not the setting: a sentence paid for with the setting on is kept even if it was turned off
        // while the request was away.
        core::WordCache cached{.ipa         = result.ipa.toStdString(),
                               .en          = result.en.toStdString(),
                               .zh          = result.zh.toStdString(),
                               .translation = result.translation.toStdString(),
                               .etymology   = result.etymology.toStdString(),
                               .example     = settlement.batch.example ? std::optional{result.example.toStdString()} : std::nullopt,
                               .signpost    = result.signpost.toStdString(),
                               .imageable   = result.imageable,
                               .imageQuery  = result.imageQuery.toStdString()};
        for (auto const& collocation : result.collocations)
            cached.collocations.push_back(collocation.toStdString());
        for (auto const& sense : result.senses) {
            core::CachedSense parsed{sense.en.toStdString(), sense.zh.toStdString(), sense.translation.toStdString()};
            parsed.imageable  = sense.imageable;
            parsed.imageQuery = sense.imageQuery.toStdString();
            cached.senses.push_back(std::move(parsed));
        }
        cache_.store(selected.lemma.toStdString(), std::move(cached), settlement.batch.context);
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
    if (QString::compare(bubble_.value("title").toString(), value, Qt::CaseInsensitive) == 0)
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
                showBubble(fromCache(found.entry, candidate.lemma), candidate, context, /*notifyPet=*/true, /*reveal=*/false);
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
    if (action != QLatin1String("translate") && action != QLatin1String("explain")) return;
    if (text != pending_.text)
        LENS_WARN("selection action text differed from the captured selection; using the captured text");
    if (pending_.kind == core::SelectionKind::Sentence) pending_.preset = action;
    explain(pending_);
}

void ExplanationDuty::reviewWord(QString lemma, QPoint const anchor)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    reviewAnchor_      = anchor;
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    const auto found   = cache_.look(value.toStdString(), context);
    if (found.state != core::ExplanationCache::State::Missing) {
        PendingSelection pending{.anchor = anchor, .kind = core::SelectionKind::Word, .surface = value, .lemma = value, .countsDown = false};
        showBubble(fromCache(found.entry, value), pending, context, /*notifyPet=*/true, /*reveal=*/false);
        reviewRaised_ = true;
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
    PendingSelection pending{.anchor = anchor, .kind = core::SelectionKind::Word, .surface = lemma, .lemma = lemma, .verdictButtons = false};
    showBubble(fromCache(found.entry, lemma), pending, context, /*notifyPet=*/false, /*reveal=*/false);
}

void ExplanationDuty::dismissReview()
{
    if (not reviewRaised_) return;
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
    if (value.isEmpty() or bubble_.isEmpty())
        return;
    PendingSelection pending{.anchor  = bubbleSubject_.anchor,
                             .kind    = core::SelectionKind::Word,
                             .surface = value,
                             .lemma   = value,
                             .chain   = true,
                             .parent  = bubble_.value("title").toString()};
    // Out of budget with nothing stored: explain() would drop the step without a word, and the card would
    // wait on it for ever. Say so, and the card shows the failure where the answer would have gone.
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    if (not cost_.canRequest()) {
        if (not showStored(pending, context))
            emit chainFailed(value);
        return;
    }
    explain(pending);
}

bool ExplanationDuty::chainStanding(PendingSelection const& selection) const
{
    return not bubble_.isEmpty() and bubble_.value("title").toString() == selection.parent;
}

QVariantMap ExplanationDuty::chainEntry(llm::Explanation const& explanation, core::CacheContext const& context) const
{
    QString const text = context.language == "en"   ? explanation.en
                         : context.language == "zh" ? explanation.zh
                                                    : explanation.translation;
    QVariantList  collocations;
    if (showsCardBlock("collocations"))
        for (auto const& collocation : explanation.collocations)
            collocations.append(collocation);
    return QVariantMap{{"title", explanation.title},
                       {"ipa", showsCardBlock("ipa") ? explanation.ipa : QString{}},
                       {"text", text},
                       {"signpost", showsCardBlock("signpost") ? explanation.signpost : QString{}},
                       {"collocations", collocations},
                       {"spans", spansOf(context.language == "en" ? text : QString{})}};
}

QVariantList ExplanationDuty::spansOf(QString const& text) const
{
    // The core counts bytes; ChainText draws with UTF-16 indices, so each offset is converted on its own prefix.
    QByteArray const bytes  = text.toUtf8();
    const auto       cutoff = core::rankForLevel(storage_.knownStore().level());
    QVariantList     spans;
    for (auto const& link : core::definitionLinks(std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())}, cutoff)) {
        const auto start = QString::fromUtf8(bytes.constData(), static_cast<int>(link.start)).size();
        const auto end   = QString::fromUtf8(bytes.constData(), static_cast<int>(link.end)).size();
        spans.append(QVariantMap{{"start", start}, {"end", end}, {"lemma", QString::fromStdString(link.lemma)}});
    }
    return spans;
}

void ExplanationDuty::reexplain()
{
    if (bubble_.isEmpty() or bubbleSubject_.kind == core::SelectionKind::Translation)
        return;
    // The cache is local, so it answers even while another request is in flight: the target
    // language may have been paid for already, and the shortcut must not make the reader wait for
    // a request it does not need. A miss goes through explain(), which holds it behind that request.
    const auto context = cache_.contextFor(bubbleSubject_.kind);
    if (showStored(bubbleSubject_, context, /*reveal=*/true))
        return;
    explain(bubbleSubject_);
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

bool ExplanationDuty::showStored(PendingSelection const& pending, core::CacheContext const& context, bool const reveal)
{
    if (not isWordKind(pending.kind))
        return false;
    const auto found = cache_.look(pending.lemma.toStdString(), context);
    if (found.state != core::ExplanationCache::State::Hit)
        return false;
    storage_.statsStore().recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
    storage_.save();
    showBubble(fromCache(found.entry, pending.lemma), pending, context, /*notifyPet=*/true, reveal);
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
    // A held request comes back out through the cache before it is paid for: the batch that has
    // just landed may be the one that stored this very word. Nothing else is ever taken here --
    // the word list's own candidates were answered out of the cache before they were queued.
    if (not batch->fromScan and showStored(batch->items.front(), batch->context)) {
        queue_.complete();
        return;
    }
    send(std::move(*batch));
}

void ExplanationDuty::showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool const notifyPet, bool const reveal)
{
    // A step of the chain goes onto the chain under the card and leaves the card alone: its text, its
    // verdict and its countdown are the card's own, and a step asked from a card that has moved on is dropped.
    if (selection.chain) {
        if (chainStanding(selection))
            emit chainAnswered(chainEntry(explanation, context));
        return;
    }
    auto const selected = [&](QString const& en, QString const& zh, QString const& translation) {
        return context.language == "en" ? en : context.language == "zh" ? zh
                                                                        : translation;
    };
    // A translation is one line of English whatever the explanation language says: the reader
    // selected Chinese, so Chinese is the one answer that cannot be the one they want, and the
    // model was asked for English alone. `en` carries it while the other two languages are the
    // ones the reader did not ask for -- the bubble then draws that line and nothing else: no
    // title, no badge, none of the word-only blocks.
    const bool    translated  = selection.kind == core::SelectionKind::Translation;
    const QString english     = translated or context.language == "en" ? explanation.en : QString{};
    const QString chinese     = context.language == "zh" and not translated ? explanation.zh : QString{};
    const QString translation = context.language != "en" and context.language != "zh" and not translated ? explanation.translation
                                                                                                         : QString{};
    // The same card refreshed -- a stream chunk, or a language switch replaying the answer -- is
    // not a new surface that has to be placed: the reader is already reading it where it stands.
    const bool  sameCard = not bubble_.isEmpty() and bubbleSubject_.kind == selection.kind and bubbleSubject_.text == selection.text and bubbleSubject_.surface == selection.surface and bubbleSubject_.lemma == selection.lemma and bubbleSubject_.anchor == selection.anchor and bubbleSubject_.preset == selection.preset and bubbleSubject_.countsDown == selection.countsDown and bubbleSubject_.verdictButtons == selection.verdictButtons;
    QVariantMap payload{{"title", explanation.title},
                        {"type", kindName(selection.kind)},
                        {"sameCard", sameCard},
                        {"en", english},
                        {"zh", chinese},
                        {"translation", translation},
                        {"countsDown", selection.countsDown},
                        {"reveal", reveal},
                        {"verdictButtons", selection.verdictButtons},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    if (isWordKind(selection.kind)) {
        // Display switches, read as the card is drawn: the answer keeps every field, and these
        // decide only which of them reach the payload (see settings_table.h).
        const bool   showIpa          = showsCardBlock("ipa");
        const bool   showSignpost     = showsCardBlock("signpost");
        const bool   showCollocations = showsCardBlock("collocations");
        const bool   showPictures     = showsCardBlock("pictures");
        QVariantList senses;
        auto         entries = explanation.senses;
        if (entries.isEmpty()) entries.push_back({explanation.en, explanation.zh, explanation.translation, explanation.imageable, explanation.imageQuery});
        for (const auto& sense : entries) {
            if (senses.size() >= (context.multipleSenses ? llm::kMaxSenses : 1)) break;
            // One language a row, the reader's own and no other: the explanation language is the
            // answer to "which language do I read this in", and a sense drawn in two of them is that
            // question answered twice (UI.md 4.3). So a row carries the English definition when
            // English was asked for and the reader's language otherwise -- the same pair the singular
            // definition above the sense list is drawn from, and `multipleSenses` decides only how
            // many rows there are.
            QString const senseEnglish = context.language == "en" ? sense.en : QString{};
            senses.append(QVariantMap{{"en", senseEnglish},
                                      {"spans", spansOf(senseEnglish)},
                                      {"text", context.language == "en" ? QString{} : selected(sense.en, sense.zh, sense.translation)},
                                      // The picture advice for this row: the card draws a Pictures entry only
                                      // for a row whose `imageable` is true, and the duty runs the search
                                      // with `imageQuery` (IMAGES.md 2.1, 4.2). Neither follows the
                                      // explanation language; the Pictures switch is what clears them here.
                                      {"imageable", showPictures && sense.imageable},
                                      {"imageQuery", showPictures ? sense.imageQuery : QString{}}});
        }
        payload.insert(QStringLiteral("senses"), senses);
        payload.insert(QStringLiteral("ipa"), showIpa ? explanation.ipa : QString{});
        payload.insert(QStringLiteral("spans"), spansOf(english));
        payload.insert(QStringLiteral("signpost"), showSignpost ? explanation.signpost : QString{});
        QVariantList collocations;
        if (showCollocations)
            for (auto const& collocation : explanation.collocations)
                collocations.append(collocation);
        payload.insert(QStringLiteral("collocations"), collocations);
        // Read off the setting as the card is drawn rather than as the request was made: turning
        // the setting off puts the origin away on the next bubble, and the entry it came from is
        // left whole for whoever asks for it later.
        if (etymologyEnabled())
            payload.insert(QStringLiteral("etymology"), explanation.etymology);
        if (exampleEnabled())
            payload.insert(QStringLiteral("example"), explanation.example);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(explanation.title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    bubble_        = payload;
    bubbleSubject_ = selection;
    // Whatever raised this bubble, the one the pointer's row put up is no longer standing: a scan
    // bubble must not be taken down by the pointer leaving a word the reader has moved on from.
    // The held exit goes with it -- this bubble is the reading now.
    reviewRaised_ = false;
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
    reviewRaised_  = false;
    bubbleSubject_ = PendingSelection{};
    if (bubble_.isEmpty()) return;
    bubble_.clear();
    emit bubbleChanged();
    emit petEvent(core::pet::PetEvent::ExplanationHidden);
}

void ExplanationDuty::mark(QString lemma, bool learned)
{
    if (not bubble_.isEmpty() and bubbleSubject_.kind != core::SelectionKind::Word and bubble_.value("title").toString() == lemma)
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
    return storage_.section().value<bool>("etymology", false);
}

bool ExplanationDuty::exampleEnabled() const
{
    return storage_.section().value<bool>("example", false);
}

bool ExplanationDuty::showsCardBlock(char const* key) const
{
    return storage_.section().value<bool>(key, true);
}

}
