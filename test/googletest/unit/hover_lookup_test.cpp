/**
 * @file hover_lookup_test.cpp
 * @brief The hover dwell rule and the word rule, offline.
 */

#include <gtest/gtest.h>

#include "core/domain/hover_lookup.h"

namespace {

using lens::core::HoverDwell;
using lens::core::hoverWordFrom;

TEST(HoverDwellTest, FiresOnceWhenThePointerHasRestedLongEnough)
{
    HoverDwell dwell{1000, 4};
    EXPECT_FALSE(dwell.update(0, 100, 100));
    EXPECT_FALSE(dwell.update(500, 100, 100));
    EXPECT_TRUE(dwell.update(1000, 100, 100));
    EXPECT_FALSE(dwell.update(1100, 100, 100));
}

TEST(HoverDwellTest, MovingBeyondTheSlopStartsANewRest)
{
    HoverDwell dwell{1000, 4};
    EXPECT_FALSE(dwell.update(0, 100, 100));
    EXPECT_FALSE(dwell.update(900, 103, 100));  // inside the slop: the rest goes on
    EXPECT_FALSE(dwell.update(1000, 150, 100)); // beyond it: the clock restarts here
    EXPECT_FALSE(dwell.update(1500, 150, 100)); // only 500 ms of rest so far
    EXPECT_TRUE(dwell.update(2000, 150, 100));
}

TEST(HoverDwellTest, JitterInsideTheSlopDoesNotRestartTheClock)
{
    HoverDwell dwell{1000, 4};
    EXPECT_FALSE(dwell.update(0, 100, 100));
    EXPECT_FALSE(dwell.update(400, 104, 96));
    EXPECT_TRUE(dwell.update(1000, 102, 98));
}

TEST(HoverDwellTest, NoSecondFireWithoutAMove)
{
    HoverDwell dwell{1000, 4};
    dwell.update(0, 10, 10);
    ASSERT_TRUE(dwell.update(1200, 10, 10));
    EXPECT_FALSE(dwell.update(3000, 10, 10));
    EXPECT_FALSE(dwell.update(9000, 11, 10)); // a jitter inside the slop is still the same rest
}

TEST(HoverWordTest, TrimsTheSpacesUIAPutsAroundAWord)
{
    EXPECT_EQ(hoverWordFrom("earthquake "), "earthquake");
    EXPECT_EQ(hoverWordFrom("  Halloween "), "Halloween");
}

TEST(HoverWordTest, KeepsAnApostropheInsideAWord)
{
    EXPECT_EQ(hoverWordFrom("What's"), "What's");
}

TEST(HoverWordTest, RefusesWhatIsNotAWord)
{
    EXPECT_EQ(hoverWordFrom("7 "), "");          // a digit
    EXPECT_EQ(hoverWordFrom("-"), "");           // punctuation
    EXPECT_EQ(hoverWordFrom("magnitude-7"), ""); // a hyphen inside the token
    EXPECT_EQ(hoverWordFrom("A"), "");           // a lone letter
    EXPECT_EQ(hoverWordFrom("Bartlett\n"), "");  // a line break in the token
    EXPECT_EQ(hoverWordFrom(""), "");
    EXPECT_EQ(hoverWordFrom("   "), "");
    EXPECT_EQ(hoverWordFrom("'tis"), ""); // an apostrophe at the start
}

}
