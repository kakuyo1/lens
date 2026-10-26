/**
 * @file glass_refresh_test.cpp
 * @brief The live refresh's decisions without a screen (src/app/glass/glass_refresh.h, docs/adr/0121).
 *
 * The backdrop used to decide every tick in one strip of code that also queried screens, ran a timer and owned a
 * thread pool, so the GDI budget, the smooth chain and the refusal note were only observable on a real DXGI machine.
 * These cases feed the plan plain facts and pin each rule: a GDI-only read leaves the refusal note alone, a second
 * live sheet ends the chain, and a screen that follows frames never raises a GDI screen's pace.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

#include "app/glass/glass_refresh.h"

namespace {

using lens::app::GlassQuality;
using lens::app::RefreshFacts;
using lens::app::RefreshLanding;
using lens::app::RefreshOutcome;
using lens::app::RefreshPlan;
using lens::app::RefreshRead;
using lens::app::RefreshSheet;
using lens::app::kGlassAutoFrameMs;
using lens::app::kGlassGdiFrameMs;

/// @return A live sheet on a screen whose chain follows frames at @p hz.
RefreshSheet following(double const hz = 144.0)
{
    auto sheet        = RefreshSheet{};
    sheet.live        = true;
    sheet.onScreen    = true;
    sheet.canFollow   = true;
    sheet.refreshRate = hz;
    return sheet;
}

/// @return A live sheet on a screen no source can follow: read through GDI.
RefreshSheet gdi()
{
    auto sheet     = RefreshSheet{};
    sheet.live     = true;
    sheet.onScreen = true;
    return sheet;
}

/// @return Facts for a desktop machine at @p quality, with the GDI budget elapsed and nothing in flight.
RefreshFacts factsFor(GlassQuality const quality, std::vector<RefreshSheet> sheets, std::size_t const cursor = 0)
{
    auto facts    = RefreshFacts{};
    facts.quality = quality;
    facts.desktop = true;
    facts.gdiDue  = true;
    facts.cursor  = cursor;
    facts.sheets  = std::move(sheets);
    return facts;
}

/// @return A read of sheet @p index through its chain, as the plan would have handed it over.
RefreshRead readOf(RefreshFacts const& facts, std::size_t const index)
{
    return RefreshRead{index, true, facts.sheets[index].generation};
}

/// @return What a read that produced @p picture answered.
RefreshOutcome outcome(bool const picture, bool const refused = false, bool const noFrame = false)
{
    return RefreshOutcome{refused, noFrame, picture};
}

} // namespace

TEST(GlassRefresh, NextLiveWalksRoundFromTheLastServed)
{
    // Sheets 0, 2 and 3 are live, and 2 was served last: the walk goes on to 3, then wraps to 0, and only then 2.
    auto const live = std::vector<bool>{true, false, true, true};
    EXPECT_EQ(lens::app::nextLive(live, 2), 3u);
    EXPECT_EQ(lens::app::nextLive(live, 3), 0u);
    EXPECT_EQ(lens::app::nextLive(live, 0), 2u);
}

TEST(GlassRefresh, NextLiveGivesEveryLiveSheetItsTurn)
{
    // Each tick serves the one it returned, so three live sheets come round in the same order, each once.
    auto const live   = std::vector<bool>{false, true, true, false, true};
    auto       served = std::vector<std::size_t>{};
    auto       last   = std::size_t{4};
    for (int tick = 0; tick < 6; ++tick) {
        last = *lens::app::nextLive(live, last);
        served.push_back(last);
    }
    EXPECT_EQ(served, (std::vector<std::size_t>{1, 2, 4, 1, 2, 4}));
}

TEST(GlassRefresh, NextLiveIsNothingWhenNoSheetIsLive)
{
    EXPECT_FALSE(lens::app::nextLive(std::vector<bool>{false, false}, 0).has_value());
    EXPECT_FALSE(lens::app::nextLive(std::vector<bool>{}, 0).has_value());
}

TEST(GlassRefresh, TheTickWalksRoundTheLiveSheets)
{
    auto facts = factsFor(GlassQuality::Auto, {gdi(), gdi(), gdi()});

    auto plan = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 1u);
    // A sheet on a screen is read through that screen's chain even when no source can follow, so a refusal can be
    // reported; the chain itself then falls to GDI.
    EXPECT_TRUE(plan.read->chain);
    EXPECT_EQ(plan.intervalMs, kGlassGdiFrameMs);
    EXPECT_FALSE(plan.fast);

    facts.cursor = 1;
    plan         = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 2u);

    facts.cursor = 2;
    plan         = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 0u);
}

TEST(GlassRefresh, AnEcoTierStopsTheTimerAndReadsNothing)
{
    auto const plan = lens::app::planRefresh(factsFor(GlassQuality::Eco, {following()}));
    EXPECT_FALSE(plan.read.has_value());
    EXPECT_EQ(plan.intervalMs, 0);
    EXPECT_FALSE(plan.fast);
}

TEST(GlassRefresh, AnIdleBackdropStopsTheTimer)
{
    auto letSheetBe = following();
    letSheetBe.live = false;
    auto const plan = lens::app::planRefresh(factsFor(GlassQuality::Auto, {letSheetBe}));
    EXPECT_FALSE(plan.read.has_value());
    EXPECT_EQ(plan.intervalMs, 0);
}

TEST(GlassRefresh, AReadInFlightSkipsTheTick)
{
    auto facts      = factsFor(GlassQuality::Auto, {gdi()});
    facts.busy      = true;
    auto const plan = lens::app::planRefresh(facts);
    EXPECT_FALSE(plan.read.has_value());
    // The pace still holds: the tick is skipped, not the refresh stopped.
    EXPECT_EQ(plan.intervalMs, kGlassGdiFrameMs);
}

TEST(GlassRefresh, ADeferredReadLeavesItsSheetAlone)
{
    auto beingRetaken    = gdi();
    beingRetaken.reading = true;
    auto const plan      = lens::app::planRefresh(factsFor(GlassQuality::Auto, {beingRetaken}));
    // Its deferred read is what the next picture will be; reading the old region again would only land after it.
    EXPECT_FALSE(plan.read.has_value());
    EXPECT_EQ(plan.intervalMs, kGlassGdiFrameMs);
}

TEST(GlassRefresh, AGdiSheetWaitsForTheSharedBudget)
{
    auto facts   = factsFor(GlassQuality::Auto, {gdi()});
    facts.gdiDue = false;
    auto plan    = lens::app::planRefresh(facts);
    EXPECT_FALSE(plan.read.has_value());

    facts.gdiDue = true;
    plan         = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 0u);
}

TEST(GlassRefresh, AFastScreenNeverRaisesAGdiSheetsPace)
{
    // Sheet 0 follows at 144 Hz, sheet 1 is GDI, and the shared budget has not elapsed. The fast sheet is due on every
    // tick; the GDI sheet still waits for the budget, however short the timer's period has become for the other screen.
    auto facts   = factsFor(GlassQuality::Auto, {following(), gdi()});
    facts.gdiDue = false;
    facts.cursor = 1;
    auto plan    = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 0u);
    EXPECT_EQ(plan.intervalMs, kGlassAutoFrameMs);

    // With the fast sheet served last and the budget still unspent, the next tick passes the GDI sheet over and
    // serves the fast sheet again: the fast pace does not bring the GDI sheet's turn forward.
    facts.cursor = 0;
    plan         = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 0u);
    EXPECT_EQ(plan.intervalMs, kGlassAutoFrameMs);

    facts.gdiDue = true;
    plan         = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_EQ(plan.read->sheet, 1u);
}

TEST(GlassRefresh, AStandardTierNeverAsksAScreenToFollow)
{
    auto facts   = factsFor(GlassQuality::Standard, {following()});
    facts.gdiDue = false;
    auto plan    = lens::app::planRefresh(facts);
    EXPECT_FALSE(plan.read.has_value());
    EXPECT_FALSE(plan.fast);
    EXPECT_EQ(plan.intervalMs, kGlassGdiFrameMs);

    facts.gdiDue = true;
    plan         = lens::app::planRefresh(facts);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_FALSE(plan.read->chain);
}

TEST(GlassRefresh, ASmoothTierPacesByTheFastestScreen)
{
    auto facts = factsFor(GlassQuality::Smooth, {following(60.0), following(144.0)});
    auto plan  = lens::app::planRefresh(facts);
    EXPECT_TRUE(plan.fast);
    EXPECT_DOUBLE_EQ(plan.fastestHz, 144.0);
    EXPECT_EQ(plan.intervalMs, lens::app::glassQualityFrameMs(GlassQuality::Smooth, 144.0));
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_TRUE(plan.read->chain);
}

TEST(GlassRefresh, OffTheDesktopTheRefreshStaysOnGdi)
{
    auto facts      = factsFor(GlassQuality::Smooth, {following()});
    facts.desktop   = false;
    auto const plan = lens::app::planRefresh(facts);
    EXPECT_FALSE(plan.fast);
    EXPECT_EQ(plan.intervalMs, kGlassGdiFrameMs);
    ASSERT_TRUE(plan.read.has_value());
    EXPECT_FALSE(plan.read->chain);
}

TEST(GlassRefresh, AGdiOnlyReadLeavesTheRefusalNoteAlone)
{
    // The note has been set by an earlier chain read. A read that went to GDI alone never tried a live source, so its
    // silence must not clear what the settings page is telling the reader.
    auto facts   = factsFor(GlassQuality::Standard, {gdi()});
    auto read    = readOf(facts, 0);
    read.chain   = false;
    auto landing = lens::app::landRefresh(facts, read, outcome(true));
    EXPECT_FALSE(landing.note);
    EXPECT_EQ(landing.apply, RefreshLanding::Apply::Publish);

    // A chain read that finds a live source is what clears the note.
    read.chain = true;
    landing    = lens::app::landRefresh(facts, read, outcome(true));
    EXPECT_TRUE(landing.note);
}

TEST(GlassRefresh, ARefusedReadIsNotedEvenOffTheChain)
{
    auto facts   = factsFor(GlassQuality::Auto, {gdi()});
    auto read    = readOf(facts, 0);
    read.chain   = false;
    auto landing = lens::app::landRefresh(facts, read, outcome(false, true));
    EXPECT_TRUE(landing.note);
    EXPECT_EQ(landing.apply, RefreshLanding::Apply::Clear);
}

TEST(GlassRefresh, ALandedReadPublishesClearsOrDoesNothing)
{
    auto facts = factsFor(GlassQuality::Smooth, {following()});
    auto read  = readOf(facts, 0);

    EXPECT_EQ(lens::app::landRefresh(facts, read, outcome(true)).apply, RefreshLanding::Apply::Publish);
    EXPECT_EQ(lens::app::landRefresh(facts, read, outcome(false)).apply, RefreshLanding::Apply::Clear);
    EXPECT_EQ(lens::app::landRefresh(facts, read, outcome(false, false, true)).apply, RefreshLanding::Apply::Nothing);
}

TEST(GlassRefresh, AReadFromAnOlderGenerationIsDropped)
{
    auto facts                 = factsFor(GlassQuality::Smooth, {following()});
    facts.sheets[0].generation = 2;
    auto read                  = readOf(facts, 0);
    read.generation            = 1;

    auto const landing = lens::app::landRefresh(facts, read, outcome(true));
    EXPECT_EQ(landing.apply, RefreshLanding::Apply::Nothing);
    EXPECT_FALSE(landing.note);
    EXPECT_FALSE(landing.chain);
}

TEST(GlassRefresh, ASecondLiveSheetStopsTheChain)
{
    // The smooth chain reads the next frame from the worker; with two surfaces live the timer has to interleave them.
    auto facts = factsFor(GlassQuality::Smooth, {following()});
    auto read  = readOf(facts, 0);
    EXPECT_TRUE(lens::app::landRefresh(facts, read, outcome(true)).chain);

    facts.sheets.push_back(following());
    EXPECT_FALSE(lens::app::landRefresh(facts, read, outcome(true)).chain);
}

TEST(GlassRefresh, TheChainNeedsSmoothLiveAndNotReading)
{
    auto facts = factsFor(GlassQuality::Auto, {following()});
    auto read  = readOf(facts, 0);
    EXPECT_FALSE(lens::app::landRefresh(facts, read, outcome(true)).chain);

    facts.quality           = GlassQuality::Smooth;
    facts.sheets[0].reading = true;
    EXPECT_FALSE(lens::app::landRefresh(facts, read, outcome(true)).chain);

    facts.sheets[0].reading = false;
    facts.sheets[0].live    = false;
    EXPECT_FALSE(lens::app::landRefresh(facts, read, outcome(true)).chain);

    facts.sheets[0].live      = true;
    facts.sheets[0].canFollow = false;
    EXPECT_FALSE(lens::app::landRefresh(facts, read, outcome(true)).chain);
}

TEST(GlassRefresh, AChainKeepsFeedingAfterANoFrame)
{
    // A still desktop is the chain's normal case: the wait timed out, nothing changed, and it waits again.
    auto       facts   = factsFor(GlassQuality::Smooth, {following()});
    auto       read    = readOf(facts, 0);
    auto const landing = lens::app::landRefresh(facts, read, outcome(false, false, true));
    EXPECT_EQ(landing.apply, RefreshLanding::Apply::Nothing);
    EXPECT_TRUE(landing.chain);
    EXPECT_TRUE(landing.note);
}
