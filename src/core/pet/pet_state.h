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

/**
 * @file pet_state.h
 * @brief The desktop pet's action state machine (PHASE3 3.2, 3.3), playing the actions its data names.
 *
 * Pure logic: no Qt, no timers, no window. The product decides which role the pet is in, from event types; the
 * pet's playbook decides which named action plays that role. The machine never sees text or selections.
 */

namespace lens::core::pet {

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
 * one-shot reaction ends. Dragging is not held, so releasing the pet always lands on idle.
 *
 * Idle is not a road to sleep. Every idle turn first rolls for a scene (kSceneChance, ADR 0049); a scene
 * is a looping action whose duration ends it. Miss that, and the turn becomes a nap on kNapChance or an
 * action from the pool. A nap ends by itself after nextNap(), so the pet is never asleep for good. Only
 * the budget's hold on sleep outlasts it (PetEvent::BudgetPaused).
 *
 * A run of kAngryClicks clicks inside kAngryWindow enters the angry action directly, because click_react
 * is usually still playing and a request at the same priority would be dropped. The role is optional.
 */
struct PetStateMachine {
    /// @brief Idle wait before a random action: this base, plus up to kIdleCooldownSpread at random.
    static constexpr std::chrono::milliseconds kIdleCooldownBase{20'000};
    /// @brief The top of the idle wait's jitter. Wide on purpose: an even cadence reads as a metronome.
    static constexpr std::chrono::milliseconds kIdleCooldownSpread{70'000};
    /// @brief The chance an idle turn becomes a scene, when the pet has one to offer.
    static constexpr double kSceneChance{1.0 / 6.0};
    /// @brief The chance an idle turn becomes a yawn and a nap rather than a random action.
    static constexpr double kNapChance{1.0 / 12.0};
    /// @brief How many clicks in a row make the pet angry, and how far apart two of them may be.
    static constexpr int                       kAngryClicks{3};
    static constexpr std::chrono::milliseconds kAngryWindow{2'000};
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
    PetStateMachine(Playbook playbook, RandomSource const& random, SceneSource const& scenes = {});

    /// @return The name of the action being played now.
    ActionName const& action() const
    {
        return current;
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
    /// @brief What an idle wait that has run out becomes: a nap, or one action from the pool.
    void takeIdleTurn();
    /// @return The next idle action from the pool, or nothing only when the pool is empty: the memory of what
    ///         was just played never holds back the last action the pool has.
    std::optional<ActionName> pickRandomIdle();
    std::chrono::milliseconds nextIdleWait();
    /// @return How long the nap about to start lasts, in [kNapMin, kNapMax).
    std::chrono::milliseconds nextNap();
    std::chrono::milliseconds durationOf(ActionName const& action) const;

    Playbook     playbook;
    RandomSource random;
    SceneSource  scenes;
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
};

} // namespace lens::core::pet
