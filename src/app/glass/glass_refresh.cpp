/**
 * @file glass_refresh.cpp
 * @brief The live refresh's decisions, made from values only (see glass_refresh.h).
 */

#include "glass_refresh.h"

namespace lens::app {

namespace {

/// @return How many sheets are live.
std::size_t countLive(RefreshFacts const& facts)
{
    auto count = std::size_t{0};
    for (auto const& sheet : facts.sheets)
        if (sheet.live) ++count;
    return count;
}

} // namespace

std::optional<std::size_t> nextLive(std::vector<bool> const& live, std::size_t const after)
{
    auto const count = live.size();
    for (auto step = std::size_t{1}; step <= count; ++step) {
        auto const index = (after + step) % count;
        if (live[index]) return index;
    }
    return std::nullopt;
}

RefreshPlan planRefresh(RefreshFacts const& facts)
{
    RefreshPlan plan;
    // Eco keeps the picture on screen and does no live work at all.
    if (not glassQualityRefreshes(facts.quality)) return plan;

    // The pace is set by the fastest live sheet's screen, and only a tier that follows frames asks a screen to do so.
    auto const duplicates = facts.desktop and glassQualityFollows(facts.quality);
    auto       anyLive    = false;
    for (auto const& sheet : facts.sheets) {
        if (not sheet.live) continue;
        anyLive = true;
        if (not duplicates or not sheet.canFollow) continue;
        plan.fast = true;
        if (sheet.refreshRate > plan.fastestHz) plan.fastestHz = sheet.refreshRate;
    }
    if (not anyLive) return plan;
    plan.intervalMs = plan.fast ? glassQualityFrameMs(facts.quality, plan.fastestHz) : kGlassGdiFrameMs;

    // One read at a time across every surface: a tick that finds the last still running is skipped, not queued behind
    // it. A deferred read holds the picture in force, and reading its old region again would only land after it.
    if (facts.busy) return plan;
    auto live = std::vector<bool>{};
    live.reserve(facts.sheets.size());
    for (auto const& sheet : facts.sheets) {
        auto const candidate = sheet.live and not sheet.reading;
        auto const fast      = candidate and duplicates and sheet.canFollow;
        // A GDI screen is read only once the shared budget has elapsed, however fast the timer runs for a screen that
        // follows frames. A fast sheet is due on every tick.
        live.push_back(candidate and (fast or facts.gdiDue));
    }
    auto const next = nextLive(live, facts.cursor);
    if (not next) return plan;
    // A sheet on a screen is read through that screen's chain even when it cannot follow today: the chain then serves
    // GDI itself and says the live sources refused, which is what the settings page's note is read from.
    auto const& sheet = facts.sheets[*next];
    plan.read         = RefreshRead{*next, duplicates and sheet.onScreen, sheet.generation};
    return plan;
}

RefreshLanding landRefresh(RefreshFacts const& facts, RefreshRead const& read, RefreshOutcome const& outcome)
{
    RefreshLanding landing;
    if (read.sheet >= facts.sheets.size()) return landing;
    auto const& sheet = facts.sheets[read.sheet];
    // A retake or a drop since this read began owns the sheet now: the picture is of a region no longer held, and it
    // has nothing to say about the screen's live sources either.
    if (sheet.generation != read.generation) return landing;
    landing.note = read.chain or outcome.refused;
    if (outcome.noFrame)
        landing.apply = RefreshLanding::Apply::Nothing;
    else if (not outcome.picture)
        landing.apply = RefreshLanding::Apply::Clear;
    else
        landing.apply = RefreshLanding::Apply::Publish;
    // A read that drops its sheet turns the refresh off with it, so the chain ends there.
    if (landing.apply == RefreshLanding::Apply::Clear) return landing;
    landing.chain = facts.quality == GlassQuality::Smooth and sheet.live and not sheet.reading and sheet.canFollow and countLive(facts) == 1;
    return landing;
}

} // namespace lens::app
