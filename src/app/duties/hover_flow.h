#pragma once

/**
 * @file hover_flow.h
 * @brief The hover job: while the mode is on, a pointer that rests on a word reads it and asks for
 *        its meaning -- the rhythm the capture duty's hover block carried, in one module
 *        (docs/adr/0165).
 *
 * Value in, value out: no QObject, no clock. The duty samples the pointer and runs the read
 * off-thread when a step asks for one; the mode is not a setting, so a restart puts the reader
 * back on selection.
 */

#include <QPoint>
#include <QString>

#include <optional>
#include <string>
#include <vector>

#include "app/platform/hover_word_lookup.h"
#include "core/domain/hover_lookup.h"

namespace lens::app {

class HoverFlow {
public:
    /// @brief What one tick reads: the pointer, the pause it answers to, and the whitelist.
    struct Facts {
        bool                     budgetPaused = false;
        QPoint                   pointer;   ///< Physical pixels, the coordinates the hook reports.
        std::vector<std::string> whitelist; ///< Executable names allowed, lowercase, as the settings store them.
    };

    /// @brief What one tick or answered read asks the adapter to do.
    struct Step {
        bool                     read = false; ///< Run the read under @p at off-thread.
        QPoint                   at;
        std::vector<std::string> whitelist;
        /// A read came back and the word still rests: ask for @p text at @p anchor, translate now.
        bool    asked = false;
        QString text;
        QPoint  anchor;
    };

    /// @brief Switch between selection lookup and hover lookup.
    /// @return The mode now in force; the adapter starts or stops its timer and announces it.
    bool toggle();
    /// @brief One hover tick: sample the pointer, and when it has rested, ask for the read.
    Step tick(Facts const& facts, qint64 nowMs);
    /// @brief The word read under a rested pointer came back; ask for it when the pointer still rests on it.
    Step wordRead(std::optional<HoverWord> word, QPoint pointerNow);

    [[nodiscard]] bool mode() const
    {
        return mode_;
    }
    [[nodiscard]] bool busy() const
    {
        return busy_;
    }

private:
    core::HoverDwell dwell_; ///< One second of rest within four pixels, as the reader rests.
    bool             mode_ = false;
    bool             busy_ = false; ///< A word read is in flight.
};

}
