/**
 * @file explanation_duty.cpp
 * @brief Explanation requests, cache lookup, verdicts and explanation surfaces.
 */

#include "explanation_duty.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>

#include <utility>

#include "cost_duty.h"
#include "notice.h"
#include "util/log.h"

namespace lens::app {
namespace {

QString nowMinute()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

llm::Explanation fromCache(core::WordCache const& cached, QString const& lemma)
{
    llm::Explanation result{lemma,
                            QString::fromStdString(cached.ipa),
                            QString::fromStdString(cached.en),
                            QString::fromStdString(cached.zh),
                            QString::fromStdString(cached.translation)};
    result.etymology = QString::fromStdString(cached.etymology);
    if (cached.example) result.example = QString::fromStdString(*cached.example);
    for (auto const& sense : cached.senses)
        result.senses.push_back({QString::fromStdString(sense.en), QString::fromStdString(sense.zh), QString::fromStdString(sense.translation)});
    return result;
}

}

ExplanationDuty::ExplanationDuty(StorageDuty& storage, llm::LlmClient& llm, CostDuty& cost, QObject* parent)
    : QObject(parent), storage_(storage), llm_(llm), cost_(cost)
{
    connect(&llm_, &llm::LlmClient::batchFinished, this, &ExplanationDuty::onBatchFinished);
    connect(&llm_, &llm::LlmClient::failed, this, &ExplanationDuty::onFailed);
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
    const auto settlement = queue_.settled(titles, {storage_.knownStore().explanationLang(), storage_.section().value<bool>("multiSense", false)});

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
                               .example     = settlement.batch.example ? std::optional{result.example.toStdString()} : std::nullopt};
        for (auto const& sense : result.senses)
            cached.senses.push_back({sense.en.toStdString(), sense.zh.toStdString(), sense.translation.toStdString()});
        storage_.knownStore().cachePut(selected.lemma.toStdString(), std::move(cached), settlement.batch.context);
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
    if (subject)
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
    for (const auto& value : candidates) {
        const auto map = value.toMap();
        if (not isWordKind(map.value("kind").toString()))
            continue;
        PendingSelection candidate{.anchor  = anchor,
                                   .kind    = QStringLiteral("word"),
                                   .surface = map.value("surface").toString(),
                                   .lemma   = map.value("lemma").toString()};
        if (candidate.lemma.isEmpty())
            continue;
        const core::CacheContext context{storage_.knownStore().explanationLang(), storage_.section().value<bool>("multiSense", false)};
        if (const auto cached = storage_.knownStore().cacheGet(candidate.lemma.toStdString(), context); cached and servable(*cached)) {
            storage_.statsStore().recordPop(candidate.lemma.toStdString(), nowMinute().toStdString());
            if (bubble_.isEmpty() and not queue_.inFlight())
                showBubble(fromCache(*cached, candidate.lemma), candidate, context);
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
    if (pending_.kind == QLatin1String("sentence")) pending_.preset = action;
    explain(pending_);
}

void ExplanationDuty::reviewWord(QString lemma, QPoint const anchor)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    reviewAnchor_ = anchor;
    const core::CacheContext context{storage_.knownStore().explanationLang(), storage_.section().value<bool>("multiSense", false)};
    if (const auto cached = storage_.knownStore().cacheGet(value.toStdString(), context)) {
        PendingSelection pending{.anchor = anchor, .kind = QStringLiteral("word"), .surface = value, .lemma = value};
        showBubble(fromCache(*cached, value), pending, context, false);
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

void ExplanationDuty::dismissReview()
{
    if (not reviewRaised_) return;
    clearBubble();
}

void ExplanationDuty::explainLemma(QString lemma)
{
    const QString value = lemma.trimmed();
    if (value.isEmpty()) return;
    PendingSelection pending{.anchor = reviewAnchor_, .kind = QStringLiteral("word"), .surface = value, .lemma = value};
    explain(pending);
}

void ExplanationDuty::explain(PendingSelection const& pending)
{
    // A request that arrives while another is in flight is held, and judged when its turn comes:
    // the answer the queue is waiting for may be the one that pays for this word.
    if (queue_.inFlight()) {
        queue_.request(pending);
        return;
    }
    const core::CacheContext context{storage_.knownStore().explanationLang(),
                                     isWordKind(pending.kind) && storage_.section().value<bool>("multiSense", false)};
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
    const auto cached = storage_.knownStore().cacheGet(pending.lemma.toStdString(), context);
    if (not cached or not servable(*cached))
        return false;
    storage_.statsStore().recordPop(pending.lemma.toStdString(), nowMinute().toStdString());
    storage_.save();
    showBubble(fromCache(*cached, pending.lemma), pending, context);
    emit statsChanged();
    return true;
}

void ExplanationDuty::send(ExplanationBatch batch)
{
    const PendingSelection& first   = batch.items.front();
    const auto              channel = first.kind == QLatin1String("entity")     ? llm::Channel::Entity
                                      : first.kind == QLatin1String("sentence") ? llm::Channel::Sentence
                                                                                : llm::Channel::Word;
    llm_.setChannel(channel);
    llm_.setExplanationLang(QString::fromStdString(batch.context.language));
    llm_.setPreset(batch.context.multipleSenses ? QStringLiteral("multiple") : first.preset);
    llm_.setEtymology(isWordKind(first.kind) && etymologyEnabled());
    llm_.setExample(isWordKind(first.kind) && batch.example);
    QStringList inputs;
    for (const auto& item : batch.items) {
        inputs << (isWordKind(item.kind) ? item.lemma : item.text);
        if (isWordKind(item.kind))
            storage_.statsStore().recordPop(item.lemma.toStdString(), nowMinute().toStdString());
    }
    busyLabel_ = QCoreApplication::translate("lens::app::AppController", "Explaining…");
    emit busyChanged();
    emit petEvent(core::pet::PetEvent::ExplanationRequested);
    storage_.save();
    emit statsChanged();
    llm_.explainWords(std::move(inputs));
}

void ExplanationDuty::sendNext()
{
    const core::CacheContext now{storage_.knownStore().explanationLang(), storage_.section().value<bool>("multiSense", false)};
    auto                     batch = queue_.take(cost_.canRequest(), now, exampleEnabled());
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

void ExplanationDuty::showBubble(llm::Explanation const& explanation, PendingSelection const& selection, core::CacheContext const& context, bool const countsDown)
{
    auto const selected = [&](QString const& en, QString const& zh, QString const& translation) {
        return context.language == "en" ? en : context.language == "zh" ? zh
                                                                        : translation;
    };
    QVariantMap payload{{"title", explanation.title},
                        {"type", selection.kind},
                        {"en", context.language == "en" ? explanation.en : QString{}},
                        {"zh", context.language == "zh" ? explanation.zh : QString{}},
                        {"translation", context.language != "en" && context.language != "zh" ? explanation.translation : QString{}},
                        {"countsDown", countsDown},
                        {"x", selection.anchor.x()},
                        {"y", selection.anchor.y()}};
    if (isWordKind(selection.kind)) {
        QVariantList senses;
        auto         entries = explanation.senses;
        if (entries.isEmpty()) entries.push_back({explanation.en, explanation.zh, explanation.translation});
        for (const auto& sense : entries) {
            if (senses.size() >= (context.multipleSenses ? llm::kMaxSenses : 1)) break;
            senses.append(QVariantMap{{"en", context.multipleSenses || context.language == "en" ? sense.en : QString{}},
                                      {"text", context.multipleSenses ? (context.language == "en" ? sense.zh : selected(sense.en, sense.zh, sense.translation)) : (context.language == "en" ? QString{} : selected(sense.en, sense.zh, sense.translation))}});
        }
        payload.insert(QStringLiteral("senses"), senses);
        payload.insert(QStringLiteral("ipa"), explanation.ipa);
        // Read off the setting as the card is drawn rather than as the request was made: turning
        // the setting off puts the origin away on the next bubble, and the entry it came from is
        // left whole for whoever asks for it later.
        if (etymologyEnabled())
            payload.insert(QStringLiteral("etymology"), explanation.etymology);
        if (exampleEnabled())
            payload.insert(QStringLiteral("example"), explanation.example);
        payload.insert(QStringLiteral("status"), storage_.knownStore().isKnown(explanation.title.toStdString()) ? QStringLiteral("known") : QStringLiteral("new"));
    }
    bubble_ = payload;
    // Whatever raised this bubble, the one the pointer's row put up is no longer standing: a scan
    // bubble must not be taken down by the pointer leaving a word the reader has moved on from.
    // The held exit goes with it -- this bubble is the reading now.
    reviewRaised_ = false;
    clearNotice();
    emit bubbleChanged();
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
    return pending.kind == QLatin1String("sentence") ? QCoreApplication::translate("lens::app::AppController", "Request failed")
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
    reviewRaised_ = false;
    if (bubble_.isEmpty()) return;
    bubble_.clear();
    emit bubbleChanged();
    emit petEvent(core::pet::PetEvent::ExplanationHidden);
}

void ExplanationDuty::mark(QString lemma, bool learned)
{
    if (!bubble_.isEmpty() && bubble_.value("type").toString() != QLatin1String("word") && bubble_.value("title").toString() == lemma)
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

bool ExplanationDuty::servable(core::WordCache const& cached) const
{
    return not exampleEnabled() or cached.example.has_value();
}

}
