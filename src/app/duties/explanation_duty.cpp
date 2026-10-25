/**
 * @file explanation_duty.cpp
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

#include "explanation_duty.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>

#include <algorithm>
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

/// @brief The key a word's sense cycle lives under: the explanation language names it, because a
///        sense is an answer in a language, and the lemma because the cycles outlive the cards.
///        Neither part carries the separator on its own.
QString cycleKey(QString const& language, QString const& lemma)
{
    return language + QLatin1Char('|') + lemma;
}

llm::Explanation fromCache(core::WordCache const& cached, QString const& lemma)
{
    llm::Explanation result{lemma,
                            QString::fromStdString(cached.ipa),
                            QString::fromStdString(cached.en),
                            QString::fromStdString(cached.zh),
                            QString::fromStdString(cached.translation)};
    result.pos        = QString::fromStdString(cached.pos);
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
    connect(&llm_, &llm::LlmClient::peekAnswered, this, &ExplanationDuty::onPeekAnswered);
    connect(&llm_, &llm::LlmClient::senseAnswered, this, &ExplanationDuty::onNextSenseAnswered);
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
        // not the setting: one asked for with the setting on is kept even if it was turned off
        // while the request was away.
        core::WordCache cached{.ipa         = result.ipa.toStdString(),
                               .en          = result.en.toStdString(),
                               .zh          = result.zh.toStdString(),
                               .translation = result.translation.toStdString(),
                               .pos         = result.pos.toStdString(),
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
    // The senses the cycle bought for the word are answers about a word that no longer exists:
    // they go with the cache entry, and a request out for one is left without a home. The peek
    // lines the session bought are the same answers' other half, and go with them.
    for (auto it = cycles_.begin(); it != cycles_.end();) {
        if (it.key().endsWith(QLatin1Char('|') + value))
            it = cycles_.erase(it);
        else
            ++it;
    }
    for (auto it = peekLines_.begin(); it != peekLines_.end();) {
        if (it.key().section(QLatin1Char('|'), 1).section(QLatin1Char('#'), 0, 0) == value)
            it = peekLines_.erase(it);
        else
            ++it;
    }
    if (senseRequestKey_.endsWith(QLatin1Char('|') + value)) {
        senseRequestKey_.clear();
        senseRequestAdvances_ = false;
    }
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

void ExplanationDuty::reviewWord(QString lemma, QPoint const anchor, QSize const anchorSize)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    reviewAnchor_      = anchor;
    const auto context = cache_.contextFor(core::SelectionKind::Word);
    const auto found   = cache_.look(value.toStdString(), context);
    if (found.state != core::ExplanationCache::State::Missing) {
        PendingSelection pending{.anchor = anchor, .anchorSize = anchorSize, .kind = core::SelectionKind::Word, .surface = value, .lemma = value, .countsDown = false, .review = true};
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
    // A pointer on the card is reading it -- and the entry at its foot is there to be pressed --
    // so the dismissal waits for the pointer to leave the card, which the card itself reports.
    if (reviewCardHeld_) {
        reviewDismissPending_ = true;
        return;
    }
    clearBubble();
}

void ExplanationDuty::dismissReviewNow()
{
    reviewDismissPending_ = false;
    if (not reviewRaised_) return;
    clearBubble();
}

void ExplanationDuty::holdReviewCard(bool const on)
{
    if (reviewCardHeld_ == on)
        return;
    reviewCardHeld_ = on;
    if (on) {
        // The pointer arrived on the card: whatever dismissal was waiting for it to leave the
        // word is obsolete -- the reader moved onto the card, not away from the reading.
        reviewDismissPending_ = false;
        return;
    }
    // The pointer has now left the card as well: a dismissal that waited for it lands.
    if (reviewDismissPending_ and reviewRaised_) {
        reviewDismissPending_ = false;
        clearBubble();
    }
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
    askChainStep(PendingSelection{.anchor  = bubbleSubject_.anchor,
                                  .kind    = core::SelectionKind::Word,
                                  .surface = value,
                                  .lemma   = value,
                                  .chain   = true,
                                  .parent  = bubble_.value("title").toString()});
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

void ExplanationDuty::requestPeek()
{
    if (bubble_.isEmpty() or bubbleSubject_.kind != core::SelectionKind::Word or bubbleSubject_.chain)
        return;
    if (storage_.knownStore().explanationLang() != "en")
        return;
    const QString mother = motherTongue();
    if (mother.isEmpty() or mother == QLatin1String("en"))
        return;
    const QString lemma = bubbleSubject_.lemma;

    // The line answers the sense the card is showing, not the word in the abstract: a swapped
    // sense is as far from the answer's own first one as the card has moved, and the peek has to
    // move with it (UI-SPEC.md 4.3, the sense cycle).
    QString     cycleKey;
    SenseCycle* cycle      = standingCycle(cycleKey);
    const int   senseIndex = cycle ? cycle->index : 0;

    // The word channel always buys the Chinese pair along with the explanation, so a Chinese
    // mother tongue is answered by the entry the standing card came from: the reader held a globe
    // the answer had already paid for. The cache is keyed by the explanation language, which is
    // exactly the English the globe exists under. A swapped sense carries its own pair -- the
    // sense channel's answer bought it with the definition -- and that is the one drawn.
    if (mother == QLatin1String("zh")) {
        if (cycle and senseIndex > 0) {
            const QString swapped = cycle->senses.value(senseIndex).zh;
            if (not swapped.trimmed().isEmpty()) {
                emit peekArrived(swapped);
                return;
            }
        }
        const auto found = cache_.look(lemma.toStdString(), cache_.contextFor(core::SelectionKind::Word));
        if (found.state != core::ExplanationCache::State::Missing and not found.entry.zh.empty()) {
            emit peekArrived(QString::fromStdString(found.entry.zh));
            return;
        }
    }
    // A line this session has paid for is the second hold's answer, not the model's to sell twice.
    // The key carries the sense it was bought for: a line bought under the first sense does not
    // answer a hold on the third one, and coming back to a sense finds its line waiting.
    const QString key  = mother + QLatin1Char('|') + lemma + (senseIndex > 0 ? QLatin1Char('#') + QString::number(senseIndex) : QString{});
    const auto    held = peekLines_.constFind(key);
    if (held != peekLines_.cend()) {
        emit peekArrived(*held);
        return;
    }
    // Out of budget the hold buys nothing and says nothing: the definition on the card is the
    // reader's answer either way, and a notice is a whole surface, not a line under it.
    if (not cost_.canRequest()) {
        LENS_DEBUG("the peek for '{}' waits: today's budget is spent", lemma.toStdString());
        return;
    }
    peekKey_        = key;
    peekSenseIndex_ = senseIndex;
    llm_.peekWord(lemma, mother, cycle and senseIndex > 0 ? cycle->senses.value(senseIndex).en : QString{});
}

void ExplanationDuty::onPeekAnswered(QString const& lemma, QString const& line, llm::Usage const usage)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    if (line.trimmed().isEmpty())
        return;
    // Stored under the key the request went out with, which names the sense the line answers for;
    // the reader may have swapped onto another sense while it was on its way, and a line drawn
    // then would answer the sense next to the one on the card.
    const QString key      = not peekKey_.isEmpty() ? peekKey_ : motherTongue() + QLatin1Char('|') + lemma;
    const int     forSense = peekSenseIndex_;
    peekKey_.clear();
    peekSenseIndex_ = 0;
    peekLines_.insert(key, line);
    if (not bubble_.isEmpty() and bubbleSubject_.kind == core::SelectionKind::Word and not bubbleSubject_.chain and bubbleSubject_.lemma == lemma) {
        QString     standingKey;
        SenseCycle* standing = standingCycle(standingKey);
        if ((standing != nullptr and standing->index == forSense) or (standing == nullptr and forSense == 0))
            emit peekArrived(line);
    }
}

QString ExplanationDuty::motherTongue() const
{
    return QString::fromStdString(storage_.section().value<std::string>("motherTongue", "zh"));
}

QString ExplanationDuty::nextSenseHint(SenseCycle const& cycle) const
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
    return next < count ? cycle.hints.at(next) : QString{};
}

ExplanationDuty::SenseCycle* ExplanationDuty::standingCycle(QString& keyOut)
{
    if (bubble_.isEmpty() or bubbleSubject_.kind != core::SelectionKind::Word or bubbleSubject_.chain)
        return nullptr;
    // With multiple senses on the rows are the selector themselves: the entry is the
    // single-sense card's, whatever the request that raised this card was captured under.
    if (storage_.section().value<bool>("multiSense", false))
        return nullptr;
    const QString key   = cycleKey(QString::fromStdString(cache_.readerContext().language), bubbleSubject_.lemma);
    const auto    found = cycles_.find(key);
    if (found == cycles_.end())
        return nullptr;
    keyOut = key;
    return &found.value();
}

void ExplanationDuty::requestNextSense(SenseCycle& cycle, QString const& key, bool const advanceOnArrival)
{
    if (cycle.fetching)
        return;
    // Out of budget the click buys nothing and says nothing: the definition on the card is the
    // reader's answer either way, the same quiet the held peek keeps (docs/adr/0112).
    if (not cost_.canRequest()) {
        LENS_DEBUG("the next sense of '{}' waits: today's budget is spent", bubbleSubject_.lemma.toStdString());
        return;
    }
    // The wire payload: the word, then the senses already shown, one per line, most frequent
    // first -- the list the model is asked to add to, in English whatever the card reads.
    QStringList known;
    for (auto const& sense : cycle.senses)
        known << QLatin1String("- ") + sense.en;
    cycle.fetching        = true;
    senseRequestKey_      = key;
    senseRequestAdvances_ = advanceOnArrival;
    LENS_INFO("asking for the next sense of '{}' ({} known, advance={})", bubbleSubject_.lemma.toStdString(), cycle.senses.size(), advanceOnArrival);
    llm_.nextSenseWord(bubbleSubject_.lemma,
                       bubbleSubject_.lemma + QLatin1Char('\n') + known.join(QLatin1Char('\n')),
                       QString::fromStdString(cache_.readerContext().language));
}

void ExplanationDuty::cycleSense()
{
    QString     key;
    SenseCycle* cycle = standingCycle(key);
    if (cycle == nullptr)
        return;
    int next = cycle->index + 1;
    if (next >= cycle->senses.size()) {
        if (cycle->exhausted) {
            if (cycle->senses.size() <= 1)
                return; // the entry that has nowhere to go is not drawn, and a stray press does nothing
            next = 0;
        } else {
            // The sense the click asks for has not been bought yet: the request goes out and
            // the answer switches the card onto it when it lands.
            requestNextSense(*cycle, key, /*advanceOnArrival=*/true);
            return;
        }
    }
    cycle->index = next;
    emitSenseSwapped(*cycle, key);
}

void ExplanationDuty::ensureNextSense()
{
    QString     key;
    SenseCycle* cycle = standingCycle(key);
    if (cycle == nullptr or cycle->fetching or cycle->exhausted)
        return;
    if (cycle->index + 1 < cycle->senses.size())
        return; // the word the hover would fill the entry with is already known
    requestNextSense(*cycle, key, /*advanceOnArrival=*/false);
}

void ExplanationDuty::onNextSenseAnswered(QString const& lemma, llm::Explanation const& answer, llm::Usage const usage)
{
    cost_.recordUsage(nowMinute().toStdString(), usage);
    const QString key     = senseRequestKey_;
    const bool    advance = senseRequestAdvances_;
    senseRequestKey_.clear();
    senseRequestAdvances_ = false;
    if (key.isEmpty())
        return;
    const auto found = cycles_.find(key);
    if (found == cycles_.end())
        return;
    SenseCycle& cycle = found.value();
    cycle.fetching    = false;
    if (answer.senses.isEmpty()) {
        // An answer that reached the model and came back whole without a sense has said there is
        // nothing further; one that carries no usage at all never got that far, and the entry
        // stays up to ask again.
        if (usage.promptTokens == 0 and usage.completionTokens == 0) {
            LENS_DEBUG("the next sense of '{}' was refused; the entry stays", lemma.toStdString());
            return;
        }
        cycle.exhausted = true;
        LENS_INFO("the model has no further sense for '{}'", lemma.toStdString());
    } else {
        const llm::Sense& sense = answer.senses.front();
        // A model that repeats a listed sense has said "nothing further" in the shape of a
        // definition; the repetition is the exhaustion answer, not a sixth sense.
        const bool known = std::any_of(cycle.senses.cbegin(), cycle.senses.cend(), [&sense](llm::Sense const& existing) { return existing.en == sense.en; });
        if (known) {
            cycle.exhausted = true;
            LENS_INFO("the model repeated a sense of '{}'; the cycle is exhausted", lemma.toStdString());
        } else {
            cycle.senses.push_back(sense);
            cycle.hints.push_back(sense.hint);
        }
    }
    // A click's answer switches the card onto the sense it brought; a hover's only fills the
    // entry. Neither moves a cycle that was exhausted on the way.
    if (advance and not cycle.exhausted and cycle.index + 1 < cycle.senses.size())
        cycle.index += 1;
    // The swap belongs to a single-sense card: the reader who turned multiple senses on while
    // the request was away has rows to read, and a one-row payload would take them away.
    if (not bubble_.isEmpty() and bubbleSubject_.kind == core::SelectionKind::Word and not bubbleSubject_.chain and bubbleSubject_.lemma == lemma and
        not storage_.section().value<bool>("multiSense", false))
        emitSenseSwapped(cycle, key);
}

void ExplanationDuty::emitSenseSwapped(SenseCycle const& cycle, QString const& key)
{
    const QString     language     = key.left(key.indexOf(QLatin1Char('|')));
    const bool        showSignpost = showsCardBlock("signpost");
    const bool        showPictures = showsCardBlock("pictures");
    const llm::Sense& sense        = cycle.senses.at(cycle.index);
    const QString     label        = cycle.index == 0 ? cycle.hints.at(0) : sense.hint;
    QString const     senseEnglish = language == "en" ? sense.en : QString{};
    QVariantMap       row{{"en", senseEnglish},
                          {"spans", spansOf(senseEnglish)},
                          {"text", language == "en" ? QString{} : language == "zh" ? sense.zh
                                                                                   : sense.translation},
                          {"imageable", showPictures && sense.imageable},
                          {"imageQuery", showPictures ? sense.imageQuery : QString{}}};
    // The payload keeps agreeing with the card, so a refresh of the same card -- a verdict, a
    // replay -- draws the sense the reader moved to, not the answer's own first one.
    bubble_.insert(QStringLiteral("senses"), QVariantList{row});
    bubble_.insert(QStringLiteral("signpost"), showSignpost ? label : QString{});
    bubble_.insert(QStringLiteral("senseCycle"), not(cycle.exhausted and cycle.senses.size() <= 1));
    bubble_.insert(QStringLiteral("senseIndex"), cycle.index);
    bubble_.insert(QStringLiteral("senseHint"), nextSenseHint(cycle));
    bubble_.insert(QStringLiteral("senseExhausted"), cycle.exhausted);
    emit senseSwapped(QVariantMap{{"cycle", not(cycle.exhausted and cycle.senses.size() <= 1)},
                                  {"index", cycle.index},
                                  {"hint", nextSenseHint(cycle)},
                                  {"exhausted", cycle.exhausted},
                                  {"row", row},
                                  {"signpost", label}});
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
    showBubble(fromCache(found.entry, pending.lemma), pending, context, /*notifyPet=*/true, /*reveal=*/false);
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
    const bool  sameCard = not bubble_.isEmpty() and bubbleSubject_.kind == selection.kind and bubbleSubject_.text == selection.text and bubbleSubject_.surface == selection.surface and bubbleSubject_.lemma == selection.lemma and bubbleSubject_.anchor == selection.anchor and bubbleSubject_.anchorSize == selection.anchorSize and bubbleSubject_.preset == selection.preset and bubbleSubject_.countsDown == selection.countsDown and bubbleSubject_.verdictButtons == selection.verdictButtons and bubbleSubject_.review == selection.review;
    QVariantMap payload{{"title", explanation.title},
                        {"type", kindName(selection.kind)},
                        {"sameCard", sameCard},
                        {"en", english},
                        {"zh", chinese},
                        {"translation", translation},
                        {"countsDown", selection.countsDown},
                        {"reveal", reveal},
                        {"verdictButtons", selection.verdictButtons},
                        {"review", selection.review},
                        {"anchorWidth", selection.anchorSize.width()},
                        {"anchorHeight", selection.anchorSize.height()},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    if (isWordKind(selection.kind)) {
        // Display switches, read as the card is drawn: the answer keeps every field, and these
        // decide only which of them reach the payload (see settings_table.h).
        const bool showIpa          = showsCardBlock("ipa");
        const bool showSignpost     = showsCardBlock("signpost");
        const bool showCollocations = showsCardBlock("collocations");
        const bool showPictures     = showsCardBlock("pictures");
        const bool showPos          = showsCardBlock("pos");
        // One language a row, the reader's own and no other: the explanation language answers
        // "which language do I read this in", and a sense drawn in two of them answers it
        // twice (UI-SPEC.md 4.3). A row carries the English definition when English was asked
        // for and the reader's language otherwise -- the same pair the singular definition
        // above the sense list is drawn from.
        const auto rowOf = [&](llm::Sense const& sense) {
            QString const senseEnglish = context.language == "en" ? sense.en : QString{};
            return QVariantMap{{"en", senseEnglish},
                               {"spans", spansOf(senseEnglish)},
                               {"text", context.language == "en" ? QString{} : selected(sense.en, sense.zh, sense.translation)},
                               // The picture advice for this row: the card draws a Pictures entry only
                               // for a row whose `imageable` is true, and the duty runs the search
                               // with `imageQuery`. Neither follows the
                               // explanation language; the Pictures switch is what clears them here.
                               {"imageable", showPictures && sense.imageable},
                               {"imageQuery", showPictures ? sense.imageQuery : QString{}}};
        };
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
                senses.append(rowOf(sense));
            }
            payload.insert(QStringLiteral("senseCycle"), false);
            payload.insert(QStringLiteral("senseIndex"), 0);
            payload.insert(QStringLiteral("senseHint"), QString{});
            payload.insert(QStringLiteral("senseExhausted"), false);
        } else {
            // The one sense the card draws is the Not-the-Sense? entry's to move (UI-SPEC.md 4.3):
            // the cycle keeps the senses this session has bought for the word, the answer's own
            // first, and the card draws the one at the cycle's index. A fresh draw of the card
            // restarts it -- what is arriving, on the wire or out of the cache, is the most
            // common sense's to write, and the senses already bought wait under the same key.
            SenseCycle&      cycle = cycles_[cycleKey(QString::fromStdString(context.language), selection.lemma)];
            const llm::Sense own{explanation.en, explanation.zh, explanation.translation, QString{}, explanation.imageable, explanation.imageQuery};
            if (cycle.senses.isEmpty()) {
                cycle.senses.push_back(own);
                // The first sense's label is the signpost the answer already carried: past the
                // last sense the entry offers the reader the way back to it.
                cycle.hints.push_back(explanation.signpost);
            } else {
                cycle.senses[0] = own;
                cycle.hints[0]  = explanation.signpost;
            }
            cycle.index = 0;
            senses.append(rowOf(cycle.senses.at(cycle.index)));
            payload.insert(QStringLiteral("senseCycle"), not(cycle.exhausted and cycle.senses.size() <= 1));
            payload.insert(QStringLiteral("senseIndex"), cycle.index);
            payload.insert(QStringLiteral("senseHint"), nextSenseHint(cycle));
            payload.insert(QStringLiteral("senseExhausted"), cycle.exhausted);
        }
        payload.insert(QStringLiteral("senses"), senses);
        payload.insert(QStringLiteral("ipa"), showIpa ? explanation.ipa : QString{});
        payload.insert(QStringLiteral("spans"), spansOf(english));
        payload.insert(QStringLiteral("signpost"), showSignpost ? explanation.signpost : QString{});
        payload.insert(QStringLiteral("pos"), showPos ? explanation.pos : QString{});
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
    reviewRaised_         = false;
    reviewCardHeld_       = false;
    reviewDismissPending_ = false;
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
    reviewRaised_         = false;
    reviewCardHeld_       = false;
    reviewDismissPending_ = false;
    bubbleSubject_        = PendingSelection{};
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
