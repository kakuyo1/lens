/**
 * @file review_duty.cpp
 * @brief When the pet may ask, which word, and what the answer does, for review_duty.h.
 */

#include "review_duty.h"

#include <optional>
#include <unordered_set>
#include <utility>

#include <QDateTime>

namespace lens::app {
namespace {

/// @return Epoch ms of a history minute, "YYYY-MM-DD HH:MM" in local time; std::nullopt when it does not parse.
std::optional<long long> epochOf(std::string const& minute)
{
    auto const when = QDateTime::fromString(QString::fromStdString(minute), QStringLiteral("yyyy-MM-dd HH:mm"));
    if (not when.isValid())
        return std::nullopt;
    return when.toMSecsSinceEpoch();
}

}

ReviewDuty::ReviewDuty(StorageDuty& storage, std::function<bool()> petEnabled, std::function<bool()> petIdle, QObject* parent)
    : QObject(parent), storage_(storage), petEnabled_(std::move(petEnabled)), petIdle_(std::move(petIdle))
{
    timeout_.setSingleShot(true);
    timeout_.setInterval(kAnswerTimeoutMs);
    connect(&timeout_, &QTimer::timeout, this, [this] { ignore(QDateTime::currentMSecsSinceEpoch()); });
    clock_.setInterval(kClockTickMs);
    connect(&clock_, &QTimer::timeout, this, [this] { tick(QDateTime::currentMSecsSinceEpoch()); });
    clock_.start();
}

core::ReviewFrequency ReviewDuty::frequency() const
{
    return storage_.reviewStore().frequency();
}

void ReviewDuty::setFrequency(core::ReviewFrequency const frequency, long long const now)
{
    storage_.reviewStore().setFrequency(frequency);
    storage_.save();
    nextLook_ = 0;
    if (auto const window = core::checkWindow(frequency))
        nextLook_ = now + drawGap(*window);
}

void ReviewDuty::tick(long long const now)
{
    // The pet was switched off under a question: the sign goes, and the word is not counted against the reader.
    if (not word_.isEmpty() and not petEnabled_()) {
        dismiss(now);
        return;
    }
    auto const window = core::checkWindow(frequency());
    if (not window)
        return;
    // The first look after a start waits its turn like any other, so a session never opens on a question.
    if (nextLook_ == 0) {
        nextLook_ = now + drawGap(*window);
        return;
    }
    if (now < nextLook_)
        return;
    nextLook_ = now + drawGap(*window);
    if (not word_.isEmpty() or not petEnabled_() or not petIdle_())
        return;
    if (auto const lemma = core::pickDue(askable(), now)) {
        word_ = QString::fromStdString(*lemma);
        timeout_.start();
        emit wordChanged();
        emit petEvent(core::pet::PetEvent::ReviewAsked);
    }
}

QString ReviewDuty::answer(bool const remembered, long long const now)
{
    if (word_.isEmpty())
        return {};
    QString const answered = word_;
    auto const    lemma    = answered.toStdString();
    auto const    state    = core::afterAnswer(stateOf(lemma), remembered, now);
    close();
    storage_.reviewStore().put(lemma, state);
    storage_.save();
    emit petEvent(remembered ? core::pet::PetEvent::ReviewRemembered : core::pet::PetEvent::ReviewForgotten);
    return answered;
}

void ReviewDuty::ignore(long long const now)
{
    if (passOver(now))
        emit petEvent(core::pet::PetEvent::ReviewIgnored);
}

void ReviewDuty::dismiss(long long const now)
{
    passOver(now);
}

std::vector<core::ReviewCandidate> ReviewDuty::askable()
{
    auto const&                        knownStore = storage_.knownStore();
    core::CacheContext const           context{knownStore.explanationLang(), storage_.section().value<bool>("multiSense", false)};
    std::vector<core::ReviewCandidate> candidates;
    std::unordered_set<std::string>    seen;
    // The history is newest first, so the first entry for a word is its last lookup.
    for (auto const& entry : storage_.statsStore().history()) {
        if (not seen.insert(entry.lemma).second)
            continue;
        auto const lastLookup = epochOf(entry.minute);
        // Only a word with a stored explanation in the current language is asked about: a question must not spend money.
        if (not lastLookup or not knownStore.cacheGet(entry.lemma, context).has_value())
            continue;
        candidates.push_back(core::ReviewCandidate{entry.lemma, storage_.reviewStore().get(entry.lemma).value_or(core::freshState(*lastLookup))});
    }
    return candidates;
}

core::ReviewState ReviewDuty::stateOf(std::string const& lemma) const
{
    if (auto const stored = storage_.reviewStore().get(lemma))
        return *stored;
    return core::freshState(lastLookupOf(lemma));
}

long long ReviewDuty::lastLookupOf(std::string const& lemma) const
{
    for (auto const& entry : storage_.statsStore().history()) {
        if (entry.lemma != lemma)
            continue;
        if (auto const lastLookup = epochOf(entry.minute))
            return *lastLookup;
    }
    return 0;
}

bool ReviewDuty::passOver(long long const now)
{
    if (word_.isEmpty())
        return false;
    auto const lemma = word_.toStdString();
    auto const state = core::afterIgnore(stateOf(lemma), now);
    close();
    storage_.reviewStore().put(lemma, state);
    storage_.save();
    return true;
}

void ReviewDuty::close()
{
    word_.clear();
    timeout_.stop();
    emit wordChanged();
}

long long ReviewDuty::drawGap(core::CheckWindow const window)
{
    return std::uniform_int_distribution<long long>{window.minMs, window.maxMs}(random_);
}

}
