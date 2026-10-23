/**
 * @file pet_state_test.cpp
 * @brief PetStateMachine, offline: the clock is stepped by the test and the random source is injected.
 *
 * Covers PHASE3 3.2, 3.3 and the state-machine rows of 3.9. The playbook below is copied by hand from the shipped
 * data/pet/dog/animations.json; this test does not read that file, so a change there must be copied here.
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/domain/pet/pet_state.h"

namespace {

using lens::core::pet::ActionSpec;
using lens::core::pet::Playbook;
using lens::core::pet::PetEvent;
using lens::core::pet::PetStateMachine;
using lens::core::pet::RandomSource;
using lens::core::pet::Role;
using lens::core::pet::SceneSource;
using namespace std::chrono_literals;

ActionSpec playback(int frames, int fps, bool loop, std::optional<Role> returnTo = std::nullopt, bool blinks = false, std::optional<std::chrono::milliseconds> duration = std::nullopt)
{
    return ActionSpec{.frames = frames, .fps = fps, .loop = loop, .returnTo = returnTo, .blinks = blinks, .duration = duration};
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
            {"hover_notice", playback(2, 10, false)},
            {"hover_dwell", playback(2, 10, false)},
            {"walk", playback(6, 8, true)},
            {"edge_peek", playback(6, 5, true)},
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

/// A draw that turns an idle turn into a fidget: above the nap's chance, and -- the wait and the pick
/// read the same value -- a 41 s wait that lands on the dog's first pool choice, look around.
constexpr double kFidgets{0.3};

/// A draw below the nap's chance: every idle turn it is drawn on becomes a yawn and a nap.
constexpr double kNaps{0.0};

PetStateMachine makeMachine(double random = kFidgets)
{
    return PetStateMachine{dogPlaybook(), RandomSource{[random] { return random; }}};
}

/// A source that hands out `values` in order and repeats the last one afterwards. The machine reads it
/// once on entering idle, once more for the die, and a third time for the pick or the nap, so a case
/// that has to tell those draws apart spells them out here.
RandomSource scripted(std::vector<double> values)
{
    return [values = std::move(values), at = std::size_t{0}]() mutable {
        auto const value = values[std::min(at, values.size() - 1)];
        ++at;
        return value;
    };
}

PetStateMachine makeMachine(RandomSource source)
{
    return PetStateMachine{dogPlaybook(), source};
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

TEST(PetStateMachine, HoverReactionPlaysOnlyWhileIdle)
{
    auto machine = makeMachine();
    EXPECT_TRUE(machine.playHover("hover_notice"));
    EXPECT_EQ(machine.action(), "hover_notice");

    machine.advance(200ms);
    EXPECT_EQ(machine.action(), "idle");

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_FALSE(machine.playHover("hover_dwell"));
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, HoverReactionRejectsLoopingAndUnknownActions)
{
    auto machine = makeMachine();
    EXPECT_FALSE(machine.playHover("idle"));
    EXPECT_FALSE(machine.playHover("no_such_action"));
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, PlayRoleStartsAOneShotAndRestReturnsToIdle)
{
    auto machine = makeMachine();

    EXPECT_TRUE(machine.playRole(Role::LookAround));
    EXPECT_EQ(machine.action(), "look_around");
    EXPECT_FALSE(machine.playRole(Role::Encourage)) << "a second gesture does not cut into the first";

    // The controller uses rest() when a ceremony takes the pet back, for a reversal or a goodbye.
    machine.rest();
    EXPECT_EQ(machine.action(), "idle");
    EXPECT_FALSE(machine.playRole(Role::Idle)) << "idle loops, so it is not a gesture";

    EXPECT_TRUE(machine.playRole(Role::Encourage));
    EXPECT_EQ(machine.action(), "encourage");
}

TEST(PetStateMachine, AmbientLoopStartsOnlyFromIdleAndOnlyForLoopingActions)
{
    auto machine = makeMachine();
    EXPECT_FALSE(machine.playAmbient("celebrate")) << "a one-shot is not an ambient loop";
    EXPECT_FALSE(machine.playAmbient("no_such_action"));
    EXPECT_EQ(machine.action(), "idle");

    EXPECT_TRUE(machine.playAmbient("walk"));
    EXPECT_EQ(machine.action(), "walk");
    EXPECT_TRUE(machine.ambient());
    EXPECT_FALSE(machine.idle());

    // Already walking: a second ambient loop is not a new request.
    EXPECT_FALSE(machine.playAmbient("edge_peek"));
    EXPECT_EQ(machine.action(), "walk");
}

TEST(PetStateMachine, AmbientLoopKeepsTheIdleClockPaused)
{
    // Left to its idle timer, this machine would fidget after about 41 s. A walk must hold that back.
    auto machine = makeMachine(kFidgets);
    ASSERT_TRUE(machine.playAmbient("walk"));
    machine.advance(120s);
    EXPECT_EQ(machine.action(), "walk");
    EXPECT_FALSE(machine.idle());
}

TEST(PetStateMachine, StopAmbientReturnsToIdle)
{
    auto machine = makeMachine();
    ASSERT_TRUE(machine.playAmbient("edge_peek"));
    machine.stopAmbient();
    EXPECT_EQ(machine.action(), "idle");
    EXPECT_FALSE(machine.ambient());
    EXPECT_TRUE(machine.idle());
}

TEST(PetStateMachine, AnEventWinsOverAnAmbientLoopAndItIsNotResumed)
{
    auto machine = makeMachine();
    ASSERT_TRUE(machine.playAmbient("walk"));

    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "click_react");
    EXPECT_FALSE(machine.ambient());

    machine.advance(333ms); // 4 frames at 12 fps
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, StopAmbientAfterAnEventLeavesTheEventAlone)
{
    auto machine = makeMachine();
    ASSERT_TRUE(machine.playAmbient("walk"));
    machine.handle(PetEvent::Click);
    machine.stopAmbient();
    EXPECT_EQ(machine.action(), "click_react");
}

TEST(PetStateMachine, AmbientLoopCannotStartWhileBusyOrDragging)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);
    EXPECT_FALSE(machine.playAmbient("walk"));

    auto dragging = makeMachine();
    dragging.handle(PetEvent::DragStart);
    EXPECT_FALSE(dragging.playAmbient("walk"));
}

TEST(PetStateMachine, HoverDoesNotCutIntoAnAmbientLoop)
{
    auto machine = makeMachine();
    ASSERT_TRUE(machine.playAmbient("walk"));
    EXPECT_FALSE(machine.playHover("hover_notice"));
    EXPECT_EQ(machine.action(), "walk");
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

TEST(PetStateMachine, IdleCooldownIsTwentyToNinetySeconds)
{
    // 0 draws the base wait itself, and the die then turns that turn into a nap.
    auto shortest = makeMachine(kNaps);
    shortest.advance(19'900ms);
    EXPECT_EQ(shortest.action(), "idle");
    shortest.advance(100ms);
    EXPECT_EQ(shortest.action(), "yawn");

    // The same source is read again by the die and once more by the pick, so 0.999 draws all but a hair
    // of the wait and then the dog's last choice: stretch.
    auto longest = makeMachine(0.999);
    longest.advance(89'000ms);
    EXPECT_EQ(longest.action(), "idle");
    longest.advance(1'000ms);
    EXPECT_EQ(longest.action(), "stretch");
}

TEST(PetStateMachine, RandomIdleDoesNotRepeatTheLastPicks)
{
    // The dog's pool is two, so the memory can hold only one action back and the picks alternate.
    auto machine = makeMachine(kFidgets);
    machine.advance(41s);
    ASSERT_EQ(machine.action(), "look_around");

    machine.advance(1s); // 6 frames at 6 fps
    ASSERT_EQ(machine.action(), "idle");

    // Look around is held back, so the only choice left is stretch.
    machine.advance(41s);
    EXPECT_EQ(machine.action(), "stretch");
}

TEST(PetStateMachine, RandomIdleRepeatsWhenThePoolHasNothingElseToPlay)
{
    // Stretch is optional in the first version (PHASE3 3.1). Without it the pool holds one action, and
    // holding that one back would leave the pet with nothing to play ever again -- so it plays it.
    auto playbook = dogPlaybook();
    playbook.actions.erase("stretch");
    playbook.roles.erase(Role::Stretch);
    playbook.idlePool = {"look_around"};
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};
    machine.advance(41s);
    ASSERT_EQ(machine.action(), "look_around");

    machine.advance(1s);
    machine.advance(41s);
    EXPECT_EQ(machine.action(), "look_around");
}

TEST(PetStateMachine, AnIdlePoolActionTheDataNamesIsPlayedByTheTimerAndEndsIntoIdle)
{
    // The product only drives the pool through the idle timer, so a pool action no role uses still plays.
    auto playbook = dogPlaybook();
    playbook.actions.emplace("wobble", playback(4, 4, false));
    playbook.idlePool = {"wobble"};
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};

    machine.advance(41s);
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

TEST(PetStateMachine, AnIdleTurnNapsOnlyWhenTheDieSaysSo)
{
    // The same turn, twice: one source draws below the nap's chance and the other above it. Nothing else
    // about the two machines differs, so the die is the whole difference between them.
    auto napping = makeMachine(kNaps);
    napping.advance(41s);
    EXPECT_EQ(napping.action(), "yawn");

    auto fidgeting = makeMachine(kFidgets);
    fidgeting.advance(41s);
    EXPECT_EQ(fidgeting.action(), "look_around");
}

TEST(PetStateMachine, ANapEndsByItselfAndTheDogWakesIntoIdle)
{
    // Nothing has to happen on the desktop for the nap to end: the clock drawn when it started is enough.
    auto machine = makeMachine(kNaps);
    machine.advance(20s);
    ASSERT_EQ(machine.action(), "yawn");

    machine.advance(833ms); // 5 frames at 6 fps
    ASSERT_EQ(machine.action(), "sleep");

    machine.advance(PetStateMachine::kNapMin); // the shortest nap, since 0 draws its lower bound
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, TheNapsLengthIsDrawnRatherThanFixed)
{
    // Draws, in the order the machine reads them: the wait, the scene roll, the die, then the nap. A
    // constant source cannot be both below the nap's chance and high in the nap's range, so this one
    // is spelled out.
    auto machine = makeMachine(scripted({0.0, 0.0, 0.0, 0.999}));
    machine.advance(20s);
    ASSERT_EQ(machine.action(), "yawn");
    machine.advance(833ms); // 5 frames at 6 fps
    ASSERT_EQ(machine.action(), "sleep");

    machine.advance(299'000ms); // 0.999 of [kNapMin, kNapMax)
    EXPECT_EQ(machine.action(), "sleep");
    machine.advance(1'000ms);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, TheBudgetsSleepOutlastsANapAndOnlyTheReaderEndsIt)
{
    // The same action, entered by the budget rather than by a yawn. That sleep is the reader's, so the
    // nap's clock must not take it away.
    auto machine = makeMachine(kNaps);
    machine.handle(PetEvent::BudgetPaused);
    ASSERT_EQ(machine.action(), "sleep");

    machine.advance(PetStateMachine::kNapMax * 2);
    EXPECT_EQ(machine.action(), "sleep");

    machine.handle(PetEvent::BudgetResumed);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, TheYawnReturnsToTheActionTheDataMapsToSleep)
{
    // Sleep is played by "nap" in this pet, so the yawn's return must land on nap rather than on a name that is gone.
    auto playbook = dogPlaybook();
    playbook.actions.emplace("nap", playback(4, 3, true));
    playbook.actions.erase("sleep");
    playbook.roles[Role::Sleep] = "nap";
    PetStateMachine machine{playbook, RandomSource{[] { return kNaps; }}};

    machine.advance(20s);
    ASSERT_EQ(machine.action(), "yawn");
    machine.advance(833ms);
    EXPECT_EQ(machine.action(), "nap");
}

TEST(PetStateMachine, ExplanationWakesTheDogFromANap)
{
    // The yawn's return to sleep is situational: a learning event can still start reading.
    auto machine = makeMachine(kNaps);
    machine.advance(20s);
    runUntil(machine, "sleep");

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), "study");
}

TEST(PetStateMachine, ClickDuringYawnCancelsTheReturnToSleep)
{
    auto machine = makeMachine(kNaps);
    machine.advance(20s);
    ASSERT_EQ(machine.action(), "yawn");

    machine.handle(PetEvent::Click);
    ASSERT_EQ(machine.action(), "click_react");

    machine.advance(333ms);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ThreeQuickClicksMakeTheDogAngryAndItEnds)
{
    // Anger is not a random action: it is the third click in a row, inside kAngryWindow. By then
    // click_react is playing, so the machine enters the action directly rather than requesting it.
    auto playbook = dogPlaybook();
    playbook.actions.emplace("angry", playback(5, 10, false));
    playbook.roles.emplace(Role::Angry, "angry");
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::Click);
    machine.advance(1s);
    machine.handle(PetEvent::Click);
    ASSERT_EQ(machine.action(), "click_react");
    machine.advance(1s);

    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "angry");

    machine.advance(500ms); // 5 frames at 10 fps
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ClicksSpreadPastTheWindowDoNotAddUp)
{
    auto playbook = dogPlaybook();
    playbook.actions.emplace("angry", playback(5, 10, false));
    playbook.roles.emplace(Role::Angry, "angry");
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::Click);
    machine.advance(PetStateMachine::kAngryWindow);
    machine.handle(PetEvent::Click);
    machine.advance(PetStateMachine::kAngryWindow);
    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "click_react");
}

TEST(PetStateMachine, AnotherEventBreaksTheAngryRun)
{
    auto playbook = dogPlaybook();
    playbook.actions.emplace("angry", playback(5, 10, false));
    playbook.roles.emplace(Role::Angry, "angry");
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::Click);
    machine.handle(PetEvent::Click);
    machine.advance(500ms);
    machine.handle(PetEvent::SelectionShown);
    machine.advance(500ms);
    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "click_react");
}

TEST(PetStateMachine, APetWithoutAnAngryActionNeverGetsAngry)
{
    // The role is optional (ADR 0049): a package without it keeps its old behaviour, however often it is poked.
    auto machine = makeMachine(kFidgets);
    for (int i = 0; i < PetStateMachine::kAngryClicks * 2; ++i) {
        machine.handle(PetEvent::Click);
        machine.advance(100ms);
    }
    EXPECT_EQ(machine.action(), "click_react");
}

TEST(PetStateMachine, AnIdleTurnCanPlayASceneAndItsDurationEndsIt)
{
    auto playbook = dogPlaybook();
    playbook.actions.emplace("swing", playback(8, 8, true, std::nullopt, false, 3s));
    PetStateMachine machine{
        playbook,
        RandomSource{[] { return kNaps; }},
        SceneSource{[]() -> std::optional<std::string> { return "swing"; }},
    };

    machine.advance(20s);
    ASSERT_EQ(machine.action(), "swing");

    // A looping action with a duration: its frames would never end it, the clock does.
    machine.advance(2'999ms);
    EXPECT_EQ(machine.action(), "swing");
    machine.advance(1ms);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, AnEmptySceneSourceLeavesTheTurnToTheNap)
{
    // The roll hits, but this pet has no scene: the turn falls through to the nap as it did before.
    auto machine = PetStateMachine{
        dogPlaybook(),
        RandomSource{[] { return kNaps; }},
        SceneSource{[]() -> std::optional<std::string> { return std::nullopt; }},
    };
    machine.advance(20s);
    EXPECT_EQ(machine.action(), "yawn");
}

TEST(PetStateMachine, ASceneCanBeStartedOnDemandAndStillEndsByItsDuration)
{
    // The settings page's scene chips ask for one directly; it is the same action, so the clock ends it.
    auto playbook = dogPlaybook();
    playbook.actions.emplace("swing", playback(8, 8, true, std::nullopt, false, 3s));
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};

    machine.playScene("swing");
    EXPECT_EQ(machine.action(), "swing");
    machine.advance(3s);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, AnOnDemandSceneRefusesAnActionThatIsNotOne)
{
    // A duration is what marks a scene. An ordinary one-shot must not be started behind the idle timer's back.
    auto machine = makeMachine();
    machine.playScene("celebrate");
    EXPECT_EQ(machine.action(), "idle");
}

/// The dog's playbook with the review sign: the ask action, and the angry one that an ignored question plays.
Playbook dogWithSign(bool withAngry)
{
    auto playbook = dogPlaybook();
    playbook.actions.emplace("ask", playback(4, 4, true));
    playbook.roles.emplace(Role::Ask, "ask");
    if (withAngry) {
        playbook.actions.emplace("angry", playback(5, 10, false));
        playbook.roles.emplace(Role::Angry, "angry");
    }
    return playbook;
}

TEST(PetStateMachine, ReviewQuestionHoldsTheSignUntilRememberedThenCelebrates)
{
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    EXPECT_EQ(machine.action(), "ask");
    machine.advance(60s);
    EXPECT_EQ(machine.action(), "ask") << "a loop with no duration keeps the sign up until an answer";

    machine.handle(PetEvent::ReviewRemembered);
    EXPECT_EQ(machine.action(), "celebrate");
    machine.advance(1s);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, ForgottenAnswerPlaysEncourage)
{
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    machine.handle(PetEvent::ReviewForgotten);
    EXPECT_EQ(machine.action(), "encourage");
}

TEST(PetStateMachine, IgnoredQuestionPlaysAngryAndEndsIntoIdle)
{
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    machine.handle(PetEvent::ReviewIgnored);
    EXPECT_EQ(machine.action(), "angry");
    machine.advance(1s);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, IgnoredQuestionWithoutAngryJustGoesIdle)
{
    auto machine = PetStateMachine{dogWithSign(false), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    machine.handle(PetEvent::ReviewIgnored);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, AClickDuringTheSignReturnsToTheSign)
{
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "click_react");
    machine.advance(1s);
    EXPECT_EQ(machine.action(), "ask");
}

TEST(PetStateMachine, APetWithoutAskNeverHoldsASignAndSurvivesAReaction)
{
    // The question still goes up in the app, so the event must be harmless here: nothing may be held that
    // the playbook cannot play, or the click's return would look up a role the pet does not have.
    auto machine = makeMachine();

    machine.handle(PetEvent::ReviewAsked);
    EXPECT_EQ(machine.action(), "idle");
    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), "click_react");
    machine.advance(1s);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, IdleIsOnlyTheIdleRoleAndNotAnyOtherAction)
{
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    EXPECT_TRUE(machine.idle());
    machine.handle(PetEvent::ReviewAsked);
    EXPECT_FALSE(machine.idle());
}

TEST(PetStateMachine, ADragDuringAQuestionReleasesBackToItsSign)
{
    // The sign is still up in its own window, so letting go of the pet must not leave the pet idle under it.
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    machine.handle(PetEvent::DragStart);
    EXPECT_EQ(machine.action(), "pickup");
    machine.handle(PetEvent::DragEnd);
    EXPECT_EQ(machine.action(), "ask");
}

TEST(PetStateMachine, AReviewQuestionPreemptsAnAmbientLoopAndTheAnswerEndsIntoIdle)
{
    // The controller replays the peek after an answer; the machine only has to hand the idle role back.
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};
    ASSERT_TRUE(machine.playAmbient("edge_peek"));

    machine.handle(PetEvent::ReviewAsked);
    EXPECT_EQ(machine.action(), "ask");
    EXPECT_FALSE(machine.ambient());

    machine.handle(PetEvent::ReviewRemembered);
    EXPECT_EQ(machine.action(), "celebrate");
    machine.advance(1s);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, AnAnswerGivenDuringADragReleasesToIdle)
{
    auto machine = PetStateMachine{dogWithSign(true), RandomSource{[] { return kFidgets; }}};

    machine.handle(PetEvent::ReviewAsked);
    machine.handle(PetEvent::DragStart);
    machine.handle(PetEvent::ReviewRemembered);
    machine.handle(PetEvent::DragEnd);
    EXPECT_EQ(machine.action(), "idle");
}

TEST(PetStateMachine, AnOptionalDizzyGesturePlaysWhenThePetHasIt)
{
    auto playbook               = dogPlaybook();
    playbook.actions["dizzy"]   = playback(6, 6, false);
    playbook.roles[Role::Dizzy] = "dizzy";
    PetStateMachine machine{playbook, RandomSource{[] { return kFidgets; }}};

    ASSERT_TRUE(machine.playRole(Role::Dizzy));
    EXPECT_EQ(machine.action(), "dizzy");
    machine.advance(1s);
    EXPECT_EQ(machine.action(), "idle");
}

} // namespace
