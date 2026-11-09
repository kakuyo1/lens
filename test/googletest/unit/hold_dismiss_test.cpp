/**
 * @file hold_dismiss_test.cpp
 * @brief The one hold/dismissal rule both the review card and the word hint carry.
 */

#include <gtest/gtest.h>

#include "app/duties/hold_dismiss.h"

using lens::app::HoldDismiss;

TEST(HoldDismiss, ADismissalWaitsForTheRelease)
{
    HoldDismiss hold;
    EXPECT_FALSE(hold.requestDismiss()) << "nothing is held, so nothing waits";

    hold.hold();
    EXPECT_TRUE(hold.requestDismiss());
    EXPECT_TRUE(hold.release());
    EXPECT_FALSE(hold.held);
    EXPECT_FALSE(hold.pending);
}

TEST(HoldDismiss, ArrivingOnTheSurfaceCancelsAWaitingDismissal)
{
    HoldDismiss hold;
    hold.hold();
    ASSERT_TRUE(hold.requestDismiss());

    hold.hold();
    EXPECT_FALSE(hold.pending);
    EXPECT_FALSE(hold.release()) << "a cancelled dismissal must not land on release";
}

TEST(HoldDismiss, ClearForgetsBoth)
{
    HoldDismiss hold;
    hold.hold();
    ASSERT_TRUE(hold.requestDismiss());

    hold.clear();
    EXPECT_FALSE(hold.held);
    EXPECT_FALSE(hold.pending);
    EXPECT_FALSE(hold.release());
}
