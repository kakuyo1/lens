#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/domain/pet/hover_sense.h"
#include "core/domain/pet/pet_mood.h"

/**
 * @file pet_state.h
 * @brief The desktop pet's action state machine (PHASE3 3.2, 3.3), playing the actions its data names, and the
 *        temperament the data may tune (ADR 0094).
 *
 * Pure logic: no Qt, no timers, no window. The product decides which role the pet is in, from event types; the
 * pet's playbook decides which named action plays that role. The machine never sees text or selections.
 */

namespace lens::core::pet {

/// @name The temperament the data may carry (ADR 0094). These are the defaults the optional `behavior` block
///       replaces; the constants ADR 0048, 0049, 0076 and 0092 drew as code.
/// @{
/// @brief Idle wait before a random action: the base, plus up to kIdleCooldownSpread at random.
inline constexpr std::chrono::milliseconds kIdleCooldownBase{20'000};
/// @brief The top of the idle wait's jitter. Wide on purpose: an even cadence reads as a metronome.
inline constexpr std::chrono::milliseconds kIdleCooldownSpread{70'000};
/// @brief The chance an idle turn becomes a scene, when the pet has one to offer.
inline constexpr double kSceneChance{1.0 / 6.0};
/// @brief The chance an idle turn becomes a yawn and a nap rather than a random action.
inline constexpr double kNapChance{1.0 / 12.0};
/// @brief How many clicks in a row make the pet angry, and how far apart two of them may be.
inline constexpr int                       kAngryClicks{3};
inline constexpr std::chrono::milliseconds kAngryWindow{2'000};
/// @brief The wait between two strolls while truly idle: 120 to 300 seconds, counted in idle time (ADR 0076).
inline constexpr std::chrono::milliseconds kWanderWaitMin{120'000};
inline constexpr std::chrono::milliseconds kWanderWaitMax{300'000};
/// @brief The most steps one stroll takes; the draw is 1..steps. A pounce gait still takes exactly one.
inline constexpr int kStrollSteps{2};
/// @brief The chance a step that ends on a screen edge becomes a hide.
inline constexpr double kHideChance{0.7};
/// @}

/**
 * @brief The pet's temperament, from animations.json's optional `behavior` block. Every field defaults to the
 *        constant it replaces, so data without the block behaves exactly as before.
 *
 * The machine reads the idle and anger fields; the controller reads the stroll, hide and gaze fields into
 * PetWander and HoverSense. All of them describe the same pet, so they travel as one block.
 */
struct BehaviorSpec {
    std::chrono::milliseconds idleBase{kIdleCooldownBase};
    std::chrono::milliseconds idleSpread{kIdleCooldownSpread};
    double                    sceneChance{kSceneChance};
    double                    napChance{kNapChance};
    int                       angryClicks{kAngryClicks};
    std::chrono::milliseconds angryWindow{kAngryWindow};
    std::chrono::milliseconds wanderWaitMin{kWanderWaitMin};
    std::chrono::milliseconds wanderWaitMax{kWanderWaitMax};
    int                       strollSteps{kStrollSteps};
    double                    hideChance{kHideChance};
    /// The eyes' reach in canvas pixels. Must stay beyond the near ring, HoverSense::kNearRadius + kExitMargin.
    double gazeRadius{HoverSense::kGazeRadius};
};

/// @brief A situation the product plays a pet through. Closed by the product: a pet chooses the art for each role.
enum class Role : std::uint8_t {
    Idle,
    Study,
    Thinking,
    Celebrate,
    Encourage,
    Sleep,
    ClickReact,
    Pickup,
    LookAround,
    Yawn,
    Stretch,
    /// Optional like stretch: a pet without the art never gets angry (docs/pet-package-format.md).
    Angry,
    /// Optional like angry: a pet without the art shows the review sign with no pose (ADR 0050).
    Ask,
    /// Optional like angry: the landing after a throw plays this when the pet has it, and click_react when it does not
    /// (ADR 0091).
    Dizzy,
};

/// @brief The name an action has in its pet's data, such as "study" or "wobble". The machine does not interpret it.
using ActionName = std::string;

/// @brief What happened. A bare enum, so no word, selection or explanation text can travel with it (PHASE3 3.9).
enum class PetEvent : std::uint8_t {
    DragStart,
    DragEnd,
    Click,
    SelectionShown,
    ExplanationRequested,
    ExplanationShown,
    ExplanationHidden,
    KnownMarked,
    NewWordMarked,
    BudgetPaused,
    BudgetResumed,
    UpdateAvailable,
    /// A review question went up. The word itself never travels with the event (ADR 0050).
    ReviewAsked,
    ReviewRemembered,
    ReviewForgotten,
    ReviewIgnored,
};

/// @brief Playback of one action, from animations.json.
struct ActionSpec {
    int  frames;
    int  fps;
    bool loop;
    /// Role the pet returns to when this one-shot ends. Held states are roles, so the return is to a role too.
    std::optional<Role> returnTo;
    /// Whether the expression layer blinks while this action plays (PHASE3 3.3).
    bool blinks;
    /// How long the action runs before it ends by itself, for a looping action such as a scene. Empty when the
    /// action ends by its frame count (one-shot) or never ends (idle, a nap).
    std::optional<std::chrono::milliseconds> duration;
};

/// @brief Everything the machine plays with, from one pet's animations.json.
struct Playbook {
    /// Playback of every action the pet has, by name.
    std::map<ActionName, ActionSpec> actions;
    /// The action that plays each role. Only stretch may be absent (PHASE3 3.1).
    std::map<Role, ActionName> roles;
    /// The actions the idle timer picks from at random. Only the timer picks them; no event does.
    /// The loader adds every action no role names, so a pet that gains an action gains an idle one
    /// without the data having to list it twice (parseAnimations).
    std::vector<ActionName> idlePool;
};

/// @brief Returns a number in [0, 1). Injected so the idle pick and its cooldown are testable.
using RandomSource = std::function<double()>;

/// @brief Chooses a scene the pet can play, or nothing. Injected so the app decides which scenes a pet has
///        (only a shipped pet has any) and the machine stays free of the scene catalogue.
using SceneSource = std::function<std::optional<ActionName>()>;

/// @brief The moment the idle turn runs in, injected so the day stretch and the after-contexts scale the
///        scene and nap chances (ADR 0093). Empty means no mood: the behavior's own odds, as before.
using MoodSource = std::function<PetMood()>;

/**
 * @brief Chooses the action the pet plays, from events and elapsed time.
 *
 * Priority, highest first: drag, click, event, situational, idle (PHASE3 3.3). A request at the same
 * priority as the action now playing, or below it, is dropped. Thinking is the exception: it only
 * waits for the explanation that an event completes, so any event replaces it. Thinking also ends by
 * itself after kThinkingTimeout, because the specification has no event for a request that is never
 * answered.
 *
 * Reading and sleeping are held states: an event keeps them, and the pet returns to them after a
 * one-shot reaction ends. Dragging is not held, so releasing the pet lands on idle, except while a review question is up:
 * then it lands back on its sign.
 *
 * Idle is not a road to sleep. Every idle turn first rolls for a scene (kSceneChance, ADR 0049); a scene
 * is a looping action whose duration ends it. Miss that, and the turn becomes a nap on kNapChance or an
 * action from the pool. A nap ends by itself after nextNap(), so the pet is never asleep for good. Only
 * the budget's hold on sleep outlasts it (PetEvent::BudgetPaused). When the app injects a mood, the two
 * chances are scaled by it (ADR 0093); without one the odds are the behavior's own. A long reading's
 * context also turns the next idle turn into a stretch, and a pet that wakes from sleep stretches once
 * (ADR 0098).
 *
 * A run of kAngryClicks clicks inside kAngryWindow enters the angry action directly, because click_react
 * is usually still playing and a request at the same priority would be dropped. The role is optional.
 *
 * An ambient loop (playAmbient) is the app's own movement: a role-less, looping action the controller starts
 * from true idle and ends with stopAmbient. Any event wins over it, and the idle clock does not run under it.
 */
struct PetStateMachine {
    /// @brief How long a nap lasts: drawn per nap between these two.
    static constexpr std::chrono::milliseconds kNapMin{90'000};
    static constexpr std::chrono::milliseconds kNapMax{300'000};
    /// @brief How many of the actions just played the idle pick refuses to repeat.
    static constexpr std::size_t kRecentIdle{2};
    /// @brief How long thinking waits for its explanation before it gives up.
    static constexpr std::chrono::milliseconds kThinkingTimeout{30'000};

    /// @param playbook What the pet plays. It must come from parseAnimations, which checks every role it needs.
    /// @param random Random source in [0, 1).
    /// @param scenes What scenes the pet can play, read only on the idle turn's scene roll. Empty for a pet with
    ///        no scenes, which leaves the turn to the nap and pool below.
    /// @param behavior The pet's temperament. Absent fields are already at the shipped defaults.
    /// @param mood Where the moment comes from, read only on the idle turn. Empty keeps the behavior's own odds.
    PetStateMachine(Playbook            playbook,
                    RandomSource const& random,
                    SceneSource const&  scenes   = {},
                    BehaviorSpec        behavior = {},
                    MoodSource const&   mood     = {});

    /// @return The name of the action being played now.
    ActionName const& action() const
    {
        return current;
    }

    /// @return Whether the idle role is playing. False during a reaction, a scene, a reading or sleeping state, a drag,
    ///         and an idle-pool action or ambient loop, which both play with no role.
    bool idle() const
    {
        return currentRole == Role::Idle;
    }

    /// @return Whether the current action is a loop that playAmbient() started, such as a walk or an edge peek.
    bool ambient() const
    {
        return ambientLoop;
    }

    /// @return Whether the expression layer should blink now, from the current action's `blinks` flag.
    bool blinking() const;

    /// @brief Applies one event. A drag ends only through DragEnd.
    /// @param event What happened. Carries no text.
    void handle(PetEvent event);

    /// @brief Starts a scene now, as the idle turn's scene roll would. Ignored unless `name` is an action with a
    ///        duration, which is what marks a scene, and while the pet is being dragged.
    /// @param name Scene action as the app's catalogue and the pet's data spell it.
    void playScene(ActionName const& name);

    /// @brief Starts a looping, role-less action while the pet is idle, for the app's own movement (a walk, an edge
    ///        peek). It takes the idle priority, so any event wins, and the idle clock stays paused while it plays.
    /// @param name Action name from animations.json.
    /// @return Whether it started. False while anything but the idle role is playing, while dragging, and for an
    ///         unknown or one-shot action.
    bool playAmbient(ActionName const& name);

    /// @brief Ends the ambient loop and returns to idle. Does nothing when an event has already taken over.
    void stopAmbient();

    /// @brief Starts a hover reaction only while the pet is idle: a greeting or the fuller reaction from its data.
    ///        The reaction is a one-shot at situational priority, so a click, a drag or a learning event still wins.
    /// @param name Action name from animations.json.
    /// @return Whether it started. False while anything else is playing, and for a looping or unknown action.
    bool playHover(ActionName const& name);

    /// @brief Starts the action mapped to `role` now, at situational priority, while the pet is idle. The entrance
    ///        ceremony's look and the goodbye's heart are played through it, so they need no event of their own.
    /// @return Whether it started. False while anything else is playing, for a looping action, and for a role the pet
    ///         has no art for.
    bool playRole(Role role);

    /// @brief Stops whatever is playing and returns to idle with a fresh idle wait. The controller uses it when a
    ///        ceremony takes over the pet, including one that reverses direction.
    void rest();

    /// @brief Moves the clock on by `step`: takes an idle turn when one is due, and ends actions that have run out.
    /// @param step Time since the last call.
    void advance(std::chrono::milliseconds step);

private:
    enum class Priority : std::uint8_t {
        Idle,
        Situational,
        Event,
        Click,
        Drag,
    };

    /// @brief Starts the action that plays `next`, if the request beats the action now playing. A role the pet has
    ///        no action for is ignored.
    void request(Role next, Priority requested);
    /// @brief Starts `name` at `requested` priority, if that beats the action now playing.
    /// @param role The role `name` plays, or nothing when the idle timer picked it from the pool.
    void play(ActionName name, std::optional<Role> role, Priority requested);
    /// @brief Starts `name` unconditionally, resetting the elapsed time and the pending return from its `returnTo`.
    void enter(ActionName name, std::optional<Role> role, Priority priority);
    /// @brief Starts the action that plays `role`, which must be one the playbook maps.
    void enterRole(Role role, Priority priority);
    /// @brief Holds `state` (reading or sleeping) and starts it at event priority.
    void hold(Role state);
    /// @brief Releases `state`, and ends it if it is playing.
    void release(Role state);
    /// @brief Ends a one-shot or a held state: to the pending return, else the held state, else idle.
    void settle();
    /// @brief What an idle wait that has run out becomes: a stretch after a long reading, a nap, or one action
    ///        from the pool (ADR 0098).
    void takeIdleTurn();
    /// @return The next idle action from the pool, or nothing only when the pool is empty: the memory of what
    ///         was just played never holds back the last action the pool has.
    std::optional<ActionName> pickRandomIdle();
    /// @brief Plays a stretch, or a look around for a pet without one, as a mood's visible reply.
    /// @return Whether a gesture started.
    bool                      replyWithStretch();
    std::chrono::milliseconds nextIdleWait();
    /// @return How long the nap about to start lasts, in [kNapMin, kNapMax).
    std::chrono::milliseconds nextNap();
    std::chrono::milliseconds durationOf(ActionName const& action) const;

    Playbook     playbook;
    RandomSource random;
    SceneSource  scenes;
    BehaviorSpec behavior;
    MoodSource   mood;
    ActionName   current;
    /// The role the current action plays. Empty when the idle timer picked it from the pool.
    std::optional<Role> currentRole;
    Priority            priority{Priority::Idle};
    /// Time spent in the current action.
    std::chrono::milliseconds elapsed{0};
    /// Time spent idle, for the random pick.
    std::chrono::milliseconds idleMs{0};
    std::chrono::milliseconds idleWait{0};
    /// How long the nap now running lasts, drawn when it started.
    std::chrono::milliseconds napFor{0};
    /// Whether the after-reading context has already had its idle-turn stretch; cleared when the context ends.
    bool repliedToReading{false};
    /// The held state an event keeps: reading while an explanation is up, sleep while the budget is paused.
    std::optional<Role> held;
    /// Where the current action goes when it ends, from its `returnTo`. Any event clears it first, so a
    /// reaction cancels a yawn's return to sleep.
    std::optional<Role> pendingReturn;
    /// The last kRecentIdle actions the idle timer played, oldest first. Only the newest entries the
    /// pool can spare are held back, so the pick is never left with nothing.
    std::deque<ActionName> recentIdle;
    /// Clicks in a row, and the time since the last one, for the angry threshold. Any other event clears both.
    int                       clicks{0};
    std::chrono::milliseconds sinceClick{0};
    bool                      dragging{false};
    /// Whether `current` is a loop playAmbient() started. Any other enter() clears it.
    bool ambientLoop{false};
};

} // namespace lens::core::pet
