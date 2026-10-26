/**
 * @file pet_throw_test.cpp
 * @brief One throw from release to idle, offline: the arc, the landed-throw run, the angry repeat and every way a
 *        throw is interrupted (ADR 0090, 0091, 0092).
 */

#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include "app/pet/pet_throw.h"

namespace {

using lens::app::pet::PetThrow;
using lens::app::pet::ThrowStep;
using lens::app::pet::ThrowStop;
using lens::core::pet::Role;
using namespace std::chrono_literals;

QRect const   kArea{0, 0, 1920, 1040};
QSize const   kPet{264, 264};
QPointF const kFling{1200.0, -900.0};

/// @brief A scripted die: each landing takes the next value, and the last value repeats once the script runs out.
class Dice {
public:
    explicit Dice(std::vector<double> rolls)
        : rolls_{std::move(rolls)}
    {}

    double operator()()
    {
        auto const value = rolls_.at(at_);
        if (at_ + 1 < rolls_.size()) ++at_;
        return value;
    }

private:
    std::vector<double> rolls_;
    std::size_t         at_{0};
};

/// @brief Releases the pet with the standard fling.
void start(PetThrow& thrown)
{
    ASSERT_TRUE(thrown.begin(QPoint{500, 300}, kFling, kPet, kArea));
}

/// @brief Ticks until the arc lands, then answers the landing's role request as the controller does.
/// @param dizzy Whether the pet's own dizzy action is taken to have played.
ThrowStep land(PetThrow& thrown, bool dizzy = true)
{
    auto step = ThrowStep{};
    for (auto i = 0; i < 400 and not step.landed; ++i)
        step = thrown.tick(20ms, true);
    EXPECT_TRUE(step.landed) << "the throw never landed";
    thrown.landingReacted(dizzy);
    return step;
}

TEST(PetThrow, ASlowReleaseIsADropAndLeavesTheRunAlone)
{
    PetThrow thrown{Dice{{0.9}}};
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.streak(), 1);

    EXPECT_FALSE(thrown.begin(QPoint{500, 300}, QPointF{20.0, 0.0}, kPet, kArea));
    EXPECT_FALSE(thrown.flying());
    EXPECT_EQ(thrown.streak(), 1) << "a refused release started the run over";
}

TEST(PetThrow, TheArcMovesTheWindowAndLandsOnTheFloor)
{
    PetThrow thrown{Dice{{0.9}}};
    start(thrown);

    auto step = thrown.tick(20ms, true);
    ASSERT_TRUE(step.position);
    EXPECT_NE(*step.position, (QPoint{500, 300}));
    EXPECT_FALSE(step.landed);
    EXPECT_FALSE(step.role);

    step = land(thrown);
    EXPECT_FALSE(thrown.flying());
    ASSERT_TRUE(step.position);
    EXPECT_EQ(step.position->y(), kArea.bottom() - kPet.height() + 1);
    EXPECT_EQ(step.role, Role::Dizzy);
    EXPECT_GT(step.elapsed.count(), 0);

    // The window stays on the landing spot for good: nothing later asks it to move again.
    EXPECT_FALSE(thrown.tick(20ms, true).position);
}

TEST(PetThrow, TheSecondLandingAngersAndRepeatsThreeTimes)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    EXPECT_EQ(thrown.streak(), 1);
    EXPECT_FALSE(thrown.repeatsOwed());

    start(thrown);
    land(thrown);
    EXPECT_TRUE(thrown.repeatsOwed()) << "the second completed landing did not anger";
    EXPECT_EQ(thrown.streak(), 0) << "the angry landing did not spend the run";

    for (auto left = PetThrow::kAngryRepeats; left > 0; --left) {
        auto const step = thrown.tick(20ms, true);
        EXPECT_EQ(step.role, Role::Angry);
        EXPECT_FALSE(step.position);
    }
    EXPECT_FALSE(thrown.repeatsOwed());
    EXPECT_FALSE(thrown.tick(20ms, true).role) << "the angry action played more than three times";
}

TEST(PetThrow, AFailedRollKeepsTheRunForTheThirdLanding)
{
    PetThrow thrown{Dice{{0.9, 0.9, 0.5}}};
    start(thrown);
    land(thrown);
    start(thrown);
    land(thrown);
    EXPECT_EQ(thrown.streak(), 2);
    EXPECT_FALSE(thrown.repeatsOwed());

    start(thrown);
    land(thrown);
    EXPECT_TRUE(thrown.repeatsOwed()) << "the third landing, with the run at two, did not anger";
}

TEST(PetThrow, OnlyADizzyLandingCanAnger)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    start(thrown);
    land(thrown, false);

    EXPECT_FALSE(thrown.repeatsOwed()) << "a landing without the dizzy art still angered";
    EXPECT_EQ(thrown.streak(), 0) << "the failed landing did not spend the roll";
}

TEST(PetThrow, AnAngryPlayWaitsForAnIdleBody)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    start(thrown);
    land(thrown);
    ASSERT_TRUE(thrown.repeatsOwed());

    EXPECT_FALSE(thrown.tick(20ms, false).role) << "the angry action started while the body was busy";
    EXPECT_TRUE(thrown.repeatsOwed());
    EXPECT_EQ(thrown.tick(20ms, true).role, Role::Angry);
}

TEST(PetThrow, AnOwedRepeatDiesWhenThePetHasNoAngryArt)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.tick(20ms, true).role, Role::Angry);

    thrown.dropRepeats();
    EXPECT_FALSE(thrown.repeatsOwed());
    EXPECT_FALSE(thrown.tick(20ms, true).role);
}

TEST(PetThrow, ALearningEventKeepsTheCompletedLandings)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.streak(), 1);

    // A learning event arrives mid-air: the throw is put down without a landing, but the run it was extending
    // survives (ADR 0092).
    start(thrown);
    thrown.tick(20ms, true);
    thrown.stop(ThrowStop::Learning);
    EXPECT_FALSE(thrown.flying());
    EXPECT_EQ(thrown.streak(), 1) << "an outside event erased a completed landing";

    // The next completed landing is therefore the second of the run, and can anger.
    start(thrown);
    land(thrown);
    EXPECT_TRUE(thrown.repeatsOwed());
}

TEST(PetThrow, ACatchStartsTheRunOver)
{
    PetThrow thrown{Dice{{0.9}}};
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.streak(), 1);

    start(thrown);
    thrown.tick(20ms, true);
    thrown.stop(ThrowStop::Caught);
    EXPECT_FALSE(thrown.flying());
    EXPECT_EQ(thrown.streak(), 0);
}

TEST(PetThrow, APickupOnTheGroundAndALearningEventKeepARunWithNothingOwed)
{
    for (auto const why : {ThrowStop::Held, ThrowStop::Learning}) {
        PetThrow thrown{Dice{{0.9}}};
        start(thrown);
        land(thrown);
        ASSERT_EQ(thrown.streak(), 1);

        thrown.stop(why);
        EXPECT_EQ(thrown.streak(), 1) << "stop kind " << static_cast<int>(why) << " dropped a settled run";
    }
}

class PetThrowClearingStop : public ::testing::TestWithParam<ThrowStop> {};

TEST_P(PetThrowClearingStop, EveryOtherInterruptionStartsTheRunOver)
{
    PetThrow thrown{Dice{{0.9}}};
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.streak(), 1);

    thrown.stop(GetParam());
    EXPECT_EQ(thrown.streak(), 0);
    EXPECT_FALSE(thrown.flying());
    EXPECT_FALSE(thrown.repeatsOwed());
}

INSTANTIATE_TEST_SUITE_P(PetThrow,
                         PetThrowClearingStop,
                         ::testing::Values(ThrowStop::Caught,
                                           ThrowStop::Dropped,
                                           ThrowStop::Clicked,
                                           ThrowStop::Resized,
                                           ThrowStop::PetChanged,
                                           ThrowStop::Toggled,
                                           ThrowStop::Staged,
                                           ThrowStop::ScreenChanged,
                                           ThrowStop::WindowGone,
                                           ThrowStop::MotionReduced));

TEST(PetThrow, AnInterruptedArcDoesNotCountAsALanding)
{
    PetThrow thrown{Dice{{0.9}}};
    start(thrown);
    thrown.tick(20ms, true);
    thrown.stop(ThrowStop::WindowGone);

    EXPECT_FALSE(thrown.flying());
    EXPECT_EQ(thrown.streak(), 0);
}

TEST(PetThrow, ANewThrowDropsAnOwedRepeat)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.streak(), 1);

    start(thrown);
    land(thrown);
    ASSERT_TRUE(thrown.repeatsOwed());

    start(thrown); // a throw begun before the repeats played takes them away
    EXPECT_FALSE(thrown.repeatsOwed());
    EXPECT_FALSE(thrown.tick(20ms, true).role);
}

TEST(PetThrow, TheRunIsForgottenWhenItsWindowExpires)
{
    PetThrow thrown{Dice{{0.9, 0.5}}};
    start(thrown);
    land(thrown);
    ASSERT_EQ(thrown.streak(), 1);

    thrown.tick(21s, false);
    EXPECT_EQ(thrown.streak(), 0) << "the run outlived its twenty-second window";

    start(thrown);
    land(thrown);
    EXPECT_FALSE(thrown.repeatsOwed()) << "a landing after the window was taken as the run's second";
}

} // namespace
