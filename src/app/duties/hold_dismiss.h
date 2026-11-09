#pragma once

/**
 * @file hold_dismiss.h
 * @brief The pointer's hold on a surface, and the dismissal that waits for it to end.
 *
 * A row's dismissal must not take down a card the pointer is reading. Two surfaces carry this rule --
 * the review card and the word hint -- and this is the one implementation of it.
 */

namespace lens::app {

/// @brief The pointer's hold on a surface, and the dismissal waiting for it to end.
struct HoldDismiss {
    /// @brief The pointer arrived: a dismissal that waited for it to leave is obsolete.
    void hold()
    {
        held    = true;
        pending = false;
    }
    /// @return Whether a dismissal lands now, which is when one was waiting; the hold ends either way.
    [[nodiscard]] bool release()
    {
        held = false;
        if (not pending)
            return false;
        pending = false;
        return true;
    }
    /// @return Whether the dismissal has to wait for the pointer to leave; sets the wait when it does.
    [[nodiscard]] bool requestDismiss()
    {
        if (not held)
            return false;
        pending = true;
        return true;
    }
    /// @brief The surface went away: nothing is held and nothing waits.
    void clear()
    {
        held    = false;
        pending = false;
    }

    bool held    = false;
    bool pending = false;
};

}
