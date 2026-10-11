/**
 * @file pet_state_test.cpp
 * @brief PetStateMachine, offline: the clock is stepped by the test and the random source is injected.
 *
 * Covers PHASE3 3.2, 3.3 and the state-machine rows of 3.9. The playbook below is copied by hand from the shipped
 * data/pet/dog/animations.json; this test does not read that file, so a change there must be copied here.
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "core/pet/pet_state.h"

namespace {

using lens::core::pet::ActionSpec;
using lens::core::pet::Playbook;
using lens::core::pet::PetEvent;
using lens::core::pet::PetStateMachine;
using lens::core::pet::RandomSource;
using lens::core::pet::Role;
using namespace std::chrono_literals;

ActionSpec playback(int frames, int fps, bool loop, std::optional<Role> returnTo = std::nullopt, bool blinks = false)
{
    return ActionSpec{.frames = frames, .fps = fps, .loop = loop, .returnTo = returnTo, .blinks = blinks};
}

/// The dog's playbook: every action by its data name, every role mapped to itself, and the idle pool.
Playbook dogPlaybook()
{
    return Playbook{
        .actions = {
            {"idle", playback(5, 5, true, std::nullopt, true)},
            {"study", playback(6, 4, true, std::nullopt, true)},
            {"thinking", playback(4, 3, true, std::nullopt, true)},
            {"celebrate", playback(8, 10, false)},
            {"encourage", playback(5, 8, false)},
            {"sleep", playback(4, 3, true)},
            {"click_react", playback(4, 12, false)},
            {"pickup", playback(4, 6, true)},
            {"look_around", playback(6, 6, false)},
            {"yawn", playback(5, 6, false, Role::Sleep)},
            {"stretch", playback(6, 6, false)},
        },
        .roles = {
            {Role::Idle, "idle"},
            {Role::Study, "study"},
            {Role::Thinking, "thinking"},
            {Role::Celebrate, "celebrate"},
            {Role::Encourage, "encourage"},
            {Role::Sleep, "sleep"},
            {Role::ClickReact, "click_react"},
            {Role::Pickup, "pickup"},
            {Role::LookAround, "look_around"},
            {Role::Yawn, "yawn"},
            {Role::Stretch, "stretch"},
        },
        .idlePool = {"look_around", "stretch"},
    };
}

PetStateMachine makeMachine(double random = 0.0)
{
    return PetStateMachine{dogPlaybook(), RandomSource{[random] { return random; }}};
}

/// Steps the clock in 100 ms ticks until `wanted` plays. Returns the time spent, capped at 300 s.
std::chrono::milliseconds runUntil(PetStateMachine& machine, std::string const& wanted)
{
    auto elapsed = 0ms;
    while (machine.action() != wanted and elapsed < 300s) {
        machine.advance(100ms);
        elapsed += 100ms;
    }
    return elapsed;
}

TEST(PetStateMachine, StartsIdle)
{
    EXPECT_EQ(makeMachine().action(), "idle");
}

TEST(PetStateMachine, ClickInterruptsReadingAndReturnsToIt)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);
    ASSERT_EQ(machine.action(), "study");

    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "click_react");

    machine.advance(333ms); // 4 frames at 12 fps
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, AnOfferedUpdateMakesTheDogCelebrate)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::UpdateAvailable);
    EXPECT_EQ(machine.action(), "celebrate");
}

TEST(PetStateMachine, EqualPriorityEventWaitsUntilTheOneShotEnds)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::KnownMarked);
    ASSERT_EQ(machine.action(), "celebrate");

    // Reading starts at the same priority as the celebration, so it is held back until the celebration ends.
    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), "celebrate");

    machine.advance(800ms);
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, NewWordAlsoWaitsForReading)
{
    // A known-word or new-word event is dropped while reading plays: it is at the same tier.
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);
    machine.handle(PetEvent::KnownMarked);
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, SelectionDoesNotInterruptAClick)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::Click);
    machine.handle(PetEvent::SelectionShown);
    EXPECT_EQ(machine.action(), "click_react");
}

TEST(PetStateMachine, OneShotReturnsToIdleWhenNothingIsHeld)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::KnownMarked);
    machine.advance(800ms); // 8 frames at 10 fps
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ThinkingGivesWayToTheExplanation)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationRequested);
    ASSERT_EQ(machine.action(), "thinking");

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, ThinkingGivesUpAfterItsTimeoutWithoutAnExplanation)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationRequested);
    machine.advance(PetStateMachine::kThinkingTimeout - 1ms);
    ASSERT_EQ(machine.action(), "thinking");

    machine.advance(1ms);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ExplanationHiddenEndsReadingAndReturnsToIdle)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);
    machine.handle(PetEvent::ExplanationHidden);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, DragInterruptsEverythingAndReleaseGoesToIdle)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);

    machine.handle(PetEvent::DragStart);
    ASSERT_EQ(machine.action(), "pickup");

    machine.handle(PetEvent::Click);
    machine.advance(10s);
    EXPECT_EQ(machine.action(), "pickup");

    // Release returns to idle, not to the reading that was playing before the grab.
    machine.handle(PetEvent::DragEnd);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, BudgetPauseSleepsUntilResumed)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::BudgetPaused);
    ASSERT_EQ(machine.action(), "sleep");

    machine.advance(10s);
    EXPECT_EQ(machine.action(), "sleep");

    machine.handle(PetEvent::BudgetResumed);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ClickWakesSleepAndTheSleepReturns)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::BudgetPaused);
    machine.handle(PetEvent::Click);
    ASSERT_EQ(machine.action(), "click_react");

    machine.advance(333ms);
    EXPECT_EQ(machine.action(), "sleep");
}

TEST(PetStateMachine, IdleCooldownIsTwentyToFortySeconds)
{
    auto shortest = makeMachine(0.0);
    shortest.advance(19'900ms);
    EXPECT_EQ(shortest.action(), "idle");
    shortest.advance(100ms);
    EXPECT_EQ(shortest.action(), "look_around");

    // The same source picks the action, so 0.999 also lands on the last choice: stretch.
    auto longest = makeMachine(0.999);
    longest.advance(39'900ms);
    EXPECT_EQ(longest.action(), "idle");
    longest.advance(100ms);
    EXPECT_EQ(longest.action(), "stretch");
}

TEST(PetStateMachine, RandomIdleDoesNotRepeatTheLastPick)
{
    // The random source always returns 0, so the first pick is the first choice: look around.
    auto machine = makeMachine(0.0);
    machine.advance(20s);
    ASSERT_EQ(machine.action(), "look_around");

    machine.advance(1s); // 6 frames at 6 fps
    ASSERT_EQ(machine.action(), "idle");

    // The next pick must differ from the last one, so it is the other choice: stretch.
    machine.advance(20s);
    EXPECT_EQ(machine.action(), "stretch");
}

TEST(PetStateMachine, RandomIdleNeverRepeatsWhenOnlyOneChoiceIsLeft)
{
    // Stretch is optional in the first version (PHASE3 3.1). Without it, look around is the only candidate,
    // and playing it twice in a row is what 3.3 forbids, so the dog stays idle instead.
    auto playbook = dogPlaybook();
    playbook.actions.erase("stretch");
    playbook.roles.erase(Role::Stretch);
    playbook.idlePool = {"look_around"};
    PetStateMachine machine{playbook, RandomSource{[] { return 0.0; }}};
    machine.advance(20s);
    ASSERT_EQ(machine.action(), "look_around");

    machine.advance(1s);
    machine.advance(20s);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, AnIdlePoolActionTheDataNamesIsPlayedByTheTimerAndEndsIntoIdle)
{
    // The product only drives the pool through the idle timer, so a pool action no role uses still plays.
    auto playbook = dogPlaybook();
    playbook.actions.emplace("wobble", playback(4, 4, false));
    playbook.idlePool = {"wobble"};
    PetStateMachine machine{playbook, RandomSource{[] { return 0.0; }}};

    machine.advance(20s);
    ASSERT_EQ(machine.action(), "wobble");

    machine.advance(1s); // 4 frames at 4 fps
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ARoleIsPlayedByTheActionTheDataNamesForIt)
{
    auto playbook = dogPlaybook();
    playbook.actions.emplace("reading", playback(6, 4, true, std::nullopt, true));
    playbook.actions.erase("study");
    playbook.roles[Role::Study] = "reading";
    PetStateMachine machine{playbook, RandomSource{[] { return 0.0; }}};

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), "reading");
}

TEST(PetStateMachine, BlinkingOnlyInTheActionsThatDeclareIt)
{
    auto machine = makeMachine();
    EXPECT_TRUE(machine.blinking());

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_TRUE(machine.blinking());

    machine.handle(PetEvent::Click);
    EXPECT_FALSE(machine.blinking());

    machine.handle(PetEvent::BudgetPaused);
    EXPECT_FALSE(machine.blinking());
}

TEST(PetStateMachine, EventsCarryNoText)
{
    // PHASE3 3.9: the event is a bare enum, so no word, selection or explanation text can travel with it.
    static_assert(std::is_enum_v<PetEvent>);
    static_assert(std::is_same_v<std::underlying_type_t<PetEvent>, std::uint8_t>);
    SUCCEED();
}

TEST(PetStateMachine, ThreeMinutesOfQuietYawnsThenSleeps)
{
    auto       machine = makeMachine(0.999);
    auto const elapsed = runUntil(machine, "yawn");
    EXPECT_GE(elapsed, 180s);
    EXPECT_LE(elapsed, 181s);

    machine.advance(833ms); // 5 frames at 6 fps
    EXPECT_EQ(machine.action(), "sleep");
}

TEST(PetStateMachine, TheYawnReturnsToTheActionTheDataMapsToSleep)
{
    // Sleep is played by "nap" in this pet, so the yawn's return must land on nap rather than on a name that is gone.
    auto playbook = dogPlaybook();
    playbook.actions.emplace("nap", playback(4, 3, true));
    playbook.actions.erase("sleep");
    playbook.roles[Role::Sleep] = "nap";
    PetStateMachine machine{playbook, RandomSource{[] { return 0.999; }}};

    runUntil(machine, "yawn");
    machine.advance(833ms);
    EXPECT_EQ(machine.action(), "nap");
}

TEST(PetStateMachine, ExplanationWakesTheDogAfterTheYawnIntoSleep)
{
    // The yawn's return to sleep is situational: a learning event can still start reading.
    auto machine = makeMachine(0.999);
    runUntil(machine, "yawn");
    machine.advance(833ms);
    ASSERT_EQ(machine.action(), "sleep");

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, ClickDuringYawnCancelsTheReturnToSleep)
{
    auto machine = makeMachine(0.999);
    runUntil(machine, "yawn");

    machine.handle(PetEvent::Click);
    ASSERT_EQ(machine.action(), "click_react");

    machine.advance(333ms);
    EXPECT_EQ(machine.action(), "idle");
}

} // namespace
