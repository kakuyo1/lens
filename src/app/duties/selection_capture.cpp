/**
 * @file selection_capture.cpp
 * @brief The selection job, moved verbatim from the capture duty's selection block (docs/adr/0165).
 */

#include "selection_capture.h"

#include <QVariantMap>

#include <algorithm>
#include <utility>

#include "app/settings_access.h"
#include "capture/capture_policy.h"
#include "core/domain/settings_table.h"
#include "llm/llm_pure.h"
#include "util/log.h"

namespace lens::app {
namespace {

constexpr int kMaxSelectionChars = 1000;

}

SelectionCapture::SelectionCapture(core::KnownStore& store, StorageDuty& storage, std::unique_ptr<SelectionTextGrabber> grabber)
    : store_(store), storage_(storage), grabber_(std::move(grabber))
{}

std::optional<SelectionCapture::Answer> SelectionCapture::released(QPoint const anchor, ReleaseFacts const& facts)
{
    // In hover mode the release is not a lookup: the reader is resting on words, not selecting them.
    if (facts.hoverMode or facts.budgetPaused) return std::nullopt;
    if (not settings::read(storage_.section(), *core::settings::find("selectionCapture")).toBool()) {
        LENS_TRACE("selection at ({}, {}) ignored: selection capture is off", anchor.x(), anchor.y());
        return std::nullopt;
    }
    return begin(anchor, std::nullopt);
}

std::optional<SelectionCapture::Answer> SelectionCapture::begin(QPoint const anchor, std::optional<QString> const& text)
{
    if (text) return classify(*text, anchor, false);
    const auto policy = clipboardPolicyFromStored(
        settings::read(storage_.section(), *core::settings::find("clipboardPolicy")).toString().toStdString());
    const auto grabbed = grabber_->grab(policy);
    if (const auto* status = std::get_if<GrabStatus>(&grabbed)) {
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return std::nullopt;
    }
    return classify(std::get<QString>(grabbed), anchor, false);
}

std::optional<SelectionCapture::Answer> SelectionCapture::classify(QString text, QPoint const anchor, bool const translateNow)
{
    if (text.size() > kMaxSelectionChars) text = text.left(kMaxSelectionChars) + QStringLiteral("…");
    const core::Selection selection = core::classifySelection(text.toStdString(), store_.known(), store_.mastered(), core::difficultyForLevel(store_.level()));
    PendingSelection      pending{.anchor = anchor, .text = text, .kind = selection.kind};
    if (selection.kind == core::SelectionKind::Word) {
        // Only a word narrows to a candidate: the other kinds have no lemma to pick, and the whole
        // selection is what gets answered.
        const auto& candidates = selection.candidates;
        const auto  fresh      = std::find_if(candidates.begin(), candidates.end(), [](const core::Candidate& candidate) {
            return candidate.state == core::CandidateState::New;
        });
        if (candidates.empty()) return std::nullopt;
        const core::Candidate& word = fresh != candidates.end() ? *fresh : candidates.front();
        pending.surface             = QString::fromStdString(word.surface);
        pending.lemma               = QString::fromStdString(word.lemma);
    }
    Answer answer;
    answer.selection    = pending;
    answer.translateNow = translateNow;
    if (not translateNow)
        answer.bar = QVariantMap{{"x", anchor.x()},
                                 {"y", anchor.y()},
                                 {"kind", kindName(pending.kind)},
                                 {"text", pending.text},
                                 {"letterCount", capture::selectionLetterCount(pending.text)},
                                 {"breakdownable", pending.kind == core::SelectionKind::Sentence and llm::breakdownApplies(pending.text)},
                                 {"spellable", llm::spellingApplies(pending.text) and pending.kind != core::SelectionKind::Translation},
                                 {"grammarable", pending.kind == core::SelectionKind::Sentence and llm::grammarApplies(pending.text)}};
    return answer;
}

}
