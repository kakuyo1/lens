#pragma once

/**
 * @file scan_emission_gate.h
 * @brief The scan payload's gate at the emission boundary: a placement identical to the last one
 *        emitted is dropped outright, a changed one is held to a cadence, and the newest held
 *        payload survives for a trailing flush.
 *
 * The track hands a frame in up to every 16 ms, and every frame's placement reaches the pencil
 * layer as a whole new model -- each word's map rebuilt, every delegate recreated. A page at rest
 * changes nothing frame to frame, and a scroll changes only positions, so the duty decides here
 * which placements are worth that rebuild. ScanSession itself stays out of it (docs/adr/0167):
 * its per-frame answer remains whole -- every frame the scan still owns carries its placement --
 * and the gate sits between that answer and the scanPlaced signal.
 *
 * @note Header-only and pure: the caller hands in the time, and nothing here ticks on its own.
 *       The trailing half is the reason takeHeld exists -- a suppressed payload must not be lost
 *       when the frames stop coming, so the newest one waits for the flush the duty schedules.
 */

#include <chrono>
#include <optional>

#include "scan_session.h"

namespace lens::app {

class ScanEmissionGate {
public:
    /// @param cadence The minimum gap between two placements the layer rebuilds for.
    explicit ScanEmissionGate(std::chrono::milliseconds cadence = std::chrono::milliseconds{66})
        : cadence_{cadence}
    {}

    /// @brief What admit() decided for one placement.
    struct Answer {
        bool                                  emitNow = false; ///< The placement goes out now, and is the last one emitted.
        bool                                  hold    = false; ///< The placement is kept for a flush due at @p dueAt.
        std::chrono::steady_clock::time_point dueAt{};
    };

    /// @brief The verdict for @p placed at @p now.
    ///
    /// A placement identical to the last one emitted is refused silently -- the layer already
    /// draws exactly that. A changed one is emitted when the cadence allows, else held: the
    /// newest observation wins, and an older held one it replaces is dropped with it.
    Answer admit(ScanStep::Placed const& placed, std::chrono::steady_clock::time_point now)
    {
        if (emitted_ and samePlacement(*emitted_, placed)) return {};
        if (now < nextDue_) {
            held_ = placed;
            return {.hold = true, .dueAt = nextDue_};
        }
        emitted_ = placed;
        held_.reset();
        nextDue_ = now + cadence_;
        return {.emitNow = true};
    }

    /// @brief The payload a due flush sends, if one is held; the flush counts as an emission, so
    ///       the cadence restarts from it.
    std::optional<ScanStep::Placed> takeHeld(std::chrono::steady_clock::time_point now)
    {
        if (not held_) return std::nullopt;
        auto out = std::move(held_);
        held_.reset(); // an optional's move does not promise the source disengages
        emitted_ = *out;
        nextDue_ = now + cadence_;
        return out;
    }

    /// @brief Whether a flush is still owed.
    [[nodiscard]] bool holding() const
    {
        return held_.has_value();
    }

    /// @brief The lines went down: nothing emitted counts any more, nothing is held, and the next
    ///       changed placement is emitted at once rather than after the cadence.
    void reset()
    {
        emitted_.reset();
        held_.reset();
        nextDue_ = {};
    }

private:
    /// @brief Whether @p b describes the page exactly as @p a does: the same words with the same
    ///       boxes and states, on the same monitor at the same scale.
    static bool samePlacement(ScanStep::Placed const& a, ScanStep::Placed const& b)
    {
        if (a.screen != b.screen or a.origin != b.origin or a.scale != b.scale) return false;
        if (a.words.size() != b.words.size()) return false;
        for (std::size_t index = 0; index < a.words.size(); ++index) {
            auto const& left  = a.words[index];
            auto const& right = b.words[index];
            if (left.lemma != right.lemma or left.surface != right.surface or left.state != right.state or
                left.x != right.x or left.y != right.y or left.width != right.width or left.height != right.height)
                return false;
        }
        return true;
    }

    std::chrono::milliseconds             cadence_;
    std::optional<ScanStep::Placed>       emitted_;   ///< The placement the layer is currently drawing.
    std::optional<ScanStep::Placed>       held_;      ///< The newest placement the cadence held back.
    std::chrono::steady_clock::time_point nextDue_{}; ///< When the cadence opens again; epoch means now.
};

}
