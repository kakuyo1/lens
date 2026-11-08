#pragma once

/**
 * @file selection_capture.h
 * @brief The selection job: the text a reader selected -- by gesture, by clipboard or by dev
 *        command -- is classified and turned into the payloads the bar, the action and the
 *        explanation read, the policy the capture duty's selection block carried, in one module
 *        (docs/adr/0165).
 *
 * Value in, value out: no QObject, no clock. The duty routes what a classification answers to its
 * signals; the gate's other inputs -- the job slot, the engine status, the online configuration --
 * stay with the duty, which is where they live.
 */

#include <QPoint>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <optional>

#include "app/platform/selection_text_grabber.h"
#include "core/domain/filter_core.h"
#include "core/document/known_store.h"
#include "pending_selection.h"
#include "storage_duty.h"

namespace lens::app {

class SelectionCapture {
public:
    /// @brief What the adapter reads to run one release.
    struct ReleaseFacts {
        bool budgetPaused = false;
        bool hoverMode    = false; ///< The reader is resting on words, not selecting them.
    };

    /// @brief What one classification asks the adapter to announce.
    struct Answer {
        PendingSelection selection;            ///< selectionReady
        bool             translateNow = false; ///< The action is pre-chosen: selectionActionRequested, no bar.
        QVariantMap      bar;                  ///< selectionBarRequested, when the bar is the road.
    };

    /// @param store The verdict store the classification reads; outlives *this.
    /// @param storage The document the clipboard policy is read from; outlives *this.
    /// @param grabber The clipboard the mouse path reads; the dev surface never does.
    SelectionCapture(core::KnownStore& store, StorageDuty& storage, std::unique_ptr<SelectionTextGrabber> grabber);

    /// @brief The reader released a selection at @p anchor.
    /// @return What to announce, or nothing when the gate refused or the grab came up empty.
    std::optional<Answer> released(QPoint anchor, ReleaseFacts const& facts);
    /// @brief Start the pipeline at @p anchor.
    /// @param anchor Release position, in the physical pixels the mouse hook reports.
    /// @param text The selected text, when the caller already has it. Given, the clipboard is not read: the developer
    ///        surface (docs/adr/0068) drives the pipeline this way, with no foreground application to copy from. The
    ///        mouse path never passes it.
    std::optional<Answer> begin(QPoint anchor, std::optional<QString> const& text);
    /// @brief Classify @p text and name what the adapter announces.
    /// @param translateNow True when the reader has already said "translate this" with a gesture of
    ///        their own: a framed region, or a word they rested on. The bar would ask a question they
    ///        have already answered, so the text goes straight to the translation. A selection keeps
    ///        the bar -- there the gesture only says "show me what this says", and the bar is where
    ///        they say what to do with it.
    std::optional<Answer> classify(QString text, QPoint anchor, bool translateNow);

private:
    core::KnownStore&                     store_;
    StorageDuty&                          storage_;
    std::unique_ptr<SelectionTextGrabber> grabber_;
};

}
