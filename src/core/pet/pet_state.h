#pragma once

#include <chrono>
#include <cstdint>
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
};

/// @brief Everything the machine plays with, from one pet's animations.json.
struct Playbook {
    /// Playback of every action the pet has, by name.
    std::map<ActionName, ActionSpec> actions;
    /// The action that plays each role. Only stretch may be absent (PHASE3 3.1).
    std::map<Role, ActionName> roles;
    /// The actions the idle timer picks from at random. Only the timer picks them; no event does.
    std::vector<ActionName> idlePool;
};

/// @brief Returns a number in [0, 1). Injected so the idle pick and its cooldown are testable.
using RandomSource = std::function<double()>;

/**
 * @brief Chooses the action the pet plays, from events and elapsed time.
 *
 * Priority, highest first: drag, click, event, situational, idle (PHASE3 3.3). A request at the same
 * priority as the action now playing, or below it, is dropped. Thinking is the exception: it only waits
 * for the explanation that an event completes, so any event replaces it. Thinking also ends by itself
 * after kThinkingTimeout, because the specification has no event for a request that is never answered.
 *
 * Reading and sleeping are held states: an event keeps them, and the pet returns to them after a
 * one-shot reaction ends. Dragging is not held, so releasing the pet always lands on idle.
 */
struct PetStateMachine {
    /// @brief Quiet time with no event before the pet yawns and falls asleep.
    static constexpr std::chrono::milliseconds kQuietBeforeYawn{180'000};
    /// @brief Idle wait before a random action: this base, plus up to the same again at random.
    static constexpr std::chrono::milliseconds kIdleCooldownBase{20'000};
    /// @brief How long thinking waits for its explanation before it gives up.
    static constexpr std::chrono::milliseconds kThinkingTimeout{30'000};

    /// @param playbook What the pet plays. It must come from parseAnimations, which checks every role it needs.
    /// @param random Random source in [0, 1).
    PetStateMachine(Playbook playbook, RandomSource const& random);

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

    /// @brief Moves the clock on by `step`: starts quiet or idle actions when due, and ends actions that have run out.
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
    /// @return The next idle action from the pool, or nothing when every candidate is the one just played or the pool is empty.
    std::optional<ActionName> pickRandomIdle();
    std::chrono::milliseconds nextIdleWait();
    std::chrono::milliseconds durationOf(ActionName const& action) const;

    Playbook     playbook;
    RandomSource random;
    ActionName   current;
    /// The role the current action plays. Empty when the idle timer picked it from the pool.
    std::optional<Role> currentRole;
    Priority            priority{Priority::Idle};
    /// Time spent in the current action.
    std::chrono::milliseconds elapsed{0};
    /// Time since the last event, for the quiet yawn.
    std::chrono::milliseconds quiet{0};
    /// Time spent idle, for the random pick.
    std::chrono::milliseconds idleMs{0};
    std::chrono::milliseconds idleWait{0};
    /// The held state an event keeps: reading while an explanation is up, sleep while the budget is paused.
    std::optional<Role> held;
    /// Where the current action goes when it ends, from its `returnTo`. Any event clears it first, so a
    /// reaction cancels a yawn's return to sleep.
    std::optional<Role>       pendingReturn;
    std::optional<ActionName> lastIdle;
    bool                      dragging{false};
};

} // namespace lens::core::pet
