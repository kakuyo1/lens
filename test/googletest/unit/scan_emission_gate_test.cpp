/** @file scan_emission_gate_test.cpp
 * @brief The scan payload's gate at the emission boundary: the duplicates it drops, the cadence it
 *        holds the rest to, and the trailing flush that keeps a suppressed payload from being lost.
 */

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QString>

#include <chrono>

#include "app/duties/scan_emission_gate.h"

namespace {

using lens::app::ScanEmissionGate;
using lens::app::ScanStep;
using Clock = std::chrono::steady_clock;

/// A placement of @p count words, all at distinct boxes, on a fixed monitor.
ScanStep::Placed placementOf(int const count, int const yOffset = 0)
{
    ScanStep::Placed placed;
    placed.screen = QRect{0, 0, 1920, 1080};
    placed.origin = QPoint{0, 0};
    placed.scale  = 1.5;
    for (int index = 0; index < count; ++index)
        placed.words.push_back({QStringLiteral("lemma%1").arg(index),
                                QStringLiteral("surface%1").arg(index),
                                lens::core::CandidateState::New,
                                10 * index,
                                20 + yOffset,
                                60,
                                16});
    return placed;
}

/// @brief Whether @p answer says the payload goes out on this call.
bool emitted(ScanEmissionGate::Answer const& answer)
{
    return answer.emitNow;
}

/// @brief Whether @p answer says the payload is held for a later flush.
bool held(ScanEmissionGate::Answer const& answer)
{
    return answer.hold;
}

TEST(ScanEmissionGate, TheFirstPlacementIsEmittedAtOnce)
{
    ScanEmissionGate gate;
    auto const       answer = gate.admit(placementOf(2), Clock::now());
    EXPECT_TRUE(emitted(answer));
    EXPECT_FALSE(held(answer));
    EXPECT_FALSE(gate.holding());
}

TEST(ScanEmissionGate, AnIdenticalPlacementIsDroppedSilently)
{
    ScanEmissionGate gate;
    auto const       now    = Clock::now();
    auto const       placed = placementOf(3);
    EXPECT_TRUE(emitted(gate.admit(placed, now)));
    // The same page read again, positions and all: the layer already draws exactly that.
    EXPECT_FALSE(emitted(gate.admit(placed, now + std::chrono::seconds{10})));
    EXPECT_FALSE(held(gate.admit(placed, now + std::chrono::seconds{10})));
    EXPECT_FALSE(gate.holding());
}

TEST(ScanEmissionGate, AChangedPlacementInsideTheCadenceIsHeldForAFlush)
{
    ScanEmissionGate gate;
    auto const       now = Clock::now();
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now)));
    // A scroll moved the words 16 ms later: the layer is not rebuilt for it yet, but the newest
    // observation is kept, and the flush is due when the cadence opens.
    auto const answer = gate.admit(placementOf(2, 40), now + std::chrono::milliseconds{16});
    EXPECT_TRUE(held(answer));
    EXPECT_FALSE(emitted(answer));
    EXPECT_TRUE(gate.holding());
    EXPECT_EQ(answer.dueAt, now + std::chrono::milliseconds{66});

    auto const flushed = gate.takeHeld(now + std::chrono::milliseconds{66});
    ASSERT_TRUE(flushed);
    EXPECT_EQ(flushed->words.size(), 2u);
    EXPECT_EQ(flushed->words.front().y, 60);
    EXPECT_FALSE(gate.holding());
}

TEST(ScanEmissionGate, TheCadenceOpensForAChangedPlacement)
{
    ScanEmissionGate gate;
    auto const       now = Clock::now();
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now)));
    // Past the cadence, a changed placement goes out at once.
    EXPECT_TRUE(emitted(gate.admit(placementOf(2, 40), now + std::chrono::milliseconds{100})));
    EXPECT_FALSE(gate.holding());
}

TEST(ScanEmissionGate, TheNewestHeldPlacementWinsOverAnOlderOne)
{
    ScanEmissionGate gate;
    auto const       now = Clock::now();
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now)));
    (void)gate.admit(placementOf(2, 10), now + std::chrono::milliseconds{16});
    (void)gate.admit(placementOf(2, 40), now + std::chrono::milliseconds{32});
    auto const flushed = gate.takeHeld(now + std::chrono::milliseconds{66});
    ASSERT_TRUE(flushed);
    EXPECT_EQ(flushed->words.front().y, 60); // the 40-offset frame, not the 10-offset one
}

TEST(ScanEmissionGate, AFlushCountsAsAnEmissionForTheCadence)
{
    ScanEmissionGate gate;
    auto const       now = Clock::now();
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now)));
    (void)gate.admit(placementOf(2, 40), now + std::chrono::milliseconds{16});
    EXPECT_TRUE(gate.takeHeld(now + std::chrono::milliseconds{66}));
    // The flush's own cadence still stands: a change right after it is held, not emitted.
    auto const answer = gate.admit(placementOf(2, 80), now + std::chrono::milliseconds{70});
    EXPECT_TRUE(held(answer));
}

TEST(ScanEmissionGate, AClearForgetsAndTheNextChangedPlacementIsEmittedAtOnce)
{
    ScanEmissionGate gate;
    auto const       now = Clock::now();
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now)));
    (void)gate.admit(placementOf(2, 40), now + std::chrono::milliseconds{16});
    gate.reset();
    EXPECT_FALSE(gate.holding());
    // The lines went down; the page's return is not held to the cadence it interrupted.
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now + std::chrono::milliseconds{20})));
}

TEST(ScanEmissionGate, AGeometryChangeIsNotAnIdenticalPlacement)
{
    auto const now    = Clock::now();
    auto const placed = placementOf(2);

    // Each case gets a fresh gate and its own moment: an emitted placement starts a cadence of
    // its own, so the sub-cases must not share one timeline.
    {
        ScanEmissionGate gate;
        EXPECT_TRUE(emitted(gate.admit(placed, now)));
        auto rescaled  = placed;
        rescaled.scale = 2.0;
        EXPECT_TRUE(emitted(gate.admit(rescaled, now + std::chrono::seconds{10})));
    }
    {
        ScanEmissionGate gate;
        EXPECT_TRUE(emitted(gate.admit(placed, now)));
        auto moved   = placed;
        moved.screen = QRect{1920, 0, 1920, 1080};
        EXPECT_TRUE(emitted(gate.admit(moved, now + std::chrono::seconds{10})));
    }
    {
        ScanEmissionGate gate;
        EXPECT_TRUE(emitted(gate.admit(placed, now)));
        // A word's verdict restyled by a verdict change is a different placement too.
        auto restyled           = placed;
        restyled.words[0].state = lens::core::CandidateState::Known;
        EXPECT_TRUE(emitted(gate.admit(restyled, now + std::chrono::seconds{10})));
    }
}

TEST(ScanEmissionGate, AnEmptyPlacementTakesTheLinesDownOnceTheCadenceOpens)
{
    ScanEmissionGate gate;
    auto const       now = Clock::now();
    EXPECT_TRUE(emitted(gate.admit(placementOf(2), now)));
    // The anchor's first missed read, inside the cadence: the empty placement is held like any
    // other change, and the flush takes the lines down.
    (void)gate.admit(ScanStep::Placed{}, now + std::chrono::milliseconds{16});
    auto const flushed = gate.takeHeld(now + std::chrono::milliseconds{66});
    ASSERT_TRUE(flushed);
    EXPECT_TRUE(flushed->words.empty());
    // The missed reads after it describe the page the layer already shows -- nothing.
    EXPECT_FALSE(emitted(gate.admit(ScanStep::Placed{}, now + std::chrono::milliseconds{100})));
    EXPECT_FALSE(held(gate.admit(ScanStep::Placed{}, now + std::chrono::milliseconds{100})));
}

}
