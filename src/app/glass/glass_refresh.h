#pragma once

/**
 * @file glass_refresh.h
 * @brief The live refresh's decisions as values: which sheet to read, through which route, at what pace, and what a
 *        landed read means.
 *
 * The backdrop gathers the facts -- each sheet's state and, for a live one, whether its screen's chain can follow
 * frames -- and asks here. Nothing in this file touches a QScreen, a timer or a thread pool, so the same plan runs in
 * a unit test as on the desktop: the GDI budget, the smooth chain and the refusal note are asserted without a screen
 * (docs/adr/0121, extending the seam of docs/adr/0086).
 */

#include "app/glass_settings.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace lens::app {

/// @brief One glass sheet as a refresh sees it.
struct RefreshSheet {
    bool live       = false; ///< The card wants the sheet refreshed.
    bool reading    = false; ///< A deferred read is in flight for the picture in force.
    int  generation = 0;     ///< Bumped when the sheet is retaken or dropped, so a read from an older one is stale.
    /// Whether the sheet lies on a screen, and whether that screen's chain has a source that follows frames. Both
    /// false when there is no screen; the rate is that screen's, zero when unknown.
    bool   onScreen    = false;
    bool   canFollow   = false;
    double refreshRate = 0.0;
};

/// @brief Everything one planning step reads, gathered on the GUI thread.
struct RefreshFacts {
    GlassQuality              quality = GlassQuality::Auto;
    bool                      desktop = false; ///< The platform has a desktop to read.
    bool                      busy    = false; ///< A read is in flight.
    bool                      gdiDue  = true;  ///< The shared GDI budget has elapsed since the last read that spent it.
    std::size_t               cursor  = 0;     ///< The sheet the last refresh served.
    std::vector<RefreshSheet> sheets;
};

/// @brief One read the plan asks for.
struct RefreshRead {
    std::size_t sheet      = 0;     ///< Which sheet to read.
    bool        chain      = false; ///< Read through the screen's chain, not GDI alone.
    int         generation = 0;     ///< The sheet's generation when the read starts; a later one makes its result stale.
};

/// @brief What the refresh does on this tick, and how fast its timer runs.
struct RefreshPlan {
    std::optional<RefreshRead> read;               ///< The read to start now, or nothing.
    int                        intervalMs = 0;     ///< The timer's period while the refresh runs; 0 stops it.
    bool                       fast       = false; ///< A live sheet's screen can follow frames, so the pace is the frame pace.
    double                     fastestHz  = 0.0;   ///< The fastest such screen's rate, for the log; zero when unknown.
};

/// @brief What one read returned, as the plan reads it.
struct RefreshOutcome {
    bool refused = false; ///< A live source refused; the answer came from the slow path.
    bool noFrame = false; ///< A live source had nothing new behind the card.
    bool picture = false; ///< The read produced a picture.
};

/// @brief What a landed read means for its sheet.
struct RefreshLanding {
    /// @brief What the adapter does with the sheet.
    enum class Apply {
        Nothing, ///< Nothing to show: no new frame, or the read belongs to a sheet that has moved on.
        Clear,   ///< The read produced no picture; drop the sheet.
        Publish, ///< Put the picture in force.
    };

    Apply apply = Apply::Nothing;
    bool  note  = false; ///< Tell the settings page whether the screen's live source refused.
    bool  chain = false; ///< Continue on the worker with the next frame.
};

/**
 * @brief The next live sheet after @p after, walking round; nothing when none is live.
 *
 * Walking round from the last one served means no sheet is always first in line, so with several live at once each
 * gets its turn in the same order every time.
 *
 * @param live Whether each sheet is live, in the order the cursor walks them.
 * @param after The index the last refresh served. The walk starts one past it and comes back to it last.
 * @return The index of the next live sheet, or nothing when none is live.
 */
std::optional<std::size_t> nextLive(std::vector<bool> const& live, std::size_t after);

/**
 * @brief Decide this tick's read and the pace the timer should keep.
 *
 * The pace comes first: it is what the timer keeps whether or not this tick has a sheet to read, and a screen that
 * can follow frames sets it while a screen that cannot keeps the shared GDI budget. A sheet on a GDI screen is only
 * due once that budget has elapsed, however fast the timer runs for another screen's frames. One read at a time:
 * a tick that finds the last one still running asks for none.
 *
 * @param facts The sheets, the tier, and the clocks; see RefreshFacts.
 * @return The read to start, if any, and the timer's period; zero stops the timer.
 */
RefreshPlan planRefresh(RefreshFacts const& facts);

/**
 * @brief Read a landed read: what to do with its sheet, whether to note a refusal, and whether the chain carries on.
 *
 * The chain feeds itself from the worker while the Smooth tier has one live sheet on a screen that can follow frames.
 * It ends at the first read that finds otherwise: a second live sheet, another tier, a retake, a deferred read or a
 * screen that stopped following (docs/adr/0074). A read through the chain has something to say to the settings page
 * either way; a GDI-alone read never tried a live source, so it must not clear a refusal already noted.
 *
 * @param facts The facts gathered when the read landed.
 * @param read The read that landed.
 * @param outcome What it returned.
 * @return What to apply, whether to note the refusal, and whether to start the next chained read.
 */
RefreshLanding landRefresh(RefreshFacts const& facts, RefreshRead const& read, RefreshOutcome const& outcome);

} // namespace lens::app
