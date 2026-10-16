/**
 * @file pet_state.cpp
 * @brief Implementation of PetStateMachine: the priority rules, held states, and the random idle pick.
 */

#include "core/pet/pet_state.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace lens::core::pet {

PetStateMachine::PetStateMachine(Playbook playbook_, RandomSource const& random_)
    : playbook{std::move(playbook_)}, random{random_}
{
    enterRole(Role::Idle, Priority::Idle);
}

bool PetStateMachine::blinking() const
{
    return playbook.actions.at(current).blinks;
}

void PetStateMachine::handle(PetEvent event)
{
    pendingReturn.reset();
    switch (event) {
        case PetEvent::DragStart:
            if (dragging) return;
            dragging = true;
            enterRole(Role::Pickup, Priority::Drag);
            return;
        case PetEvent::DragEnd:
            if (not dragging) return;
            dragging = false;
            enterRole(Role::Idle, Priority::Idle);
            return;
        case PetEvent::Click:
            if (not dragging) request(Role::ClickReact, Priority::Click);
            return;
        case PetEvent::SelectionShown:
            request(Role::LookAround, Priority::Event);
            return;
        case PetEvent::ExplanationRequested:
            request(Role::Thinking, Priority::Event);
            return;
        case PetEvent::ExplanationShown:
            hold(Role::Study);
            return;
        case PetEvent::ExplanationHidden:
            release(Role::Study);
            return;
        case PetEvent::KnownMarked:
        case PetEvent::UpdateAvailable:
            request(Role::Celebrate, Priority::Event);
            return;
        case PetEvent::NewWordMarked:
            request(Role::Encourage, Priority::Event);
            return;
        case PetEvent::BudgetPaused:
            hold(Role::Sleep);
            return;
        case PetEvent::BudgetResumed:
            release(Role::Sleep);
            return;
    }
}

void PetStateMachine::advance(std::chrono::milliseconds step)
{
    if (currentRole == Role::Idle) {
        idleMs += step;
        if (idleMs >= idleWait) takeIdleTurn();
        return;
    }
    elapsed += step;
    if (currentRole == Role::Thinking) {
        if (elapsed >= kThinkingTimeout) settle();
        return;
    }
    // A nap is a looping action, so nothing below ends it but the clock drawn when it started. The
    // budget's hold on sleep is longer than any nap: that sleep is the reader's, and it lasts until
    // the reader resumes.
    if (currentRole == Role::Sleep) {
        if (not held and elapsed >= napFor) settle();
        return;
    }
    if (not playbook.actions.at(current).loop and elapsed >= durationOf(current)) settle();
}

void PetStateMachine::takeIdleTurn()
{
    // The idle timer owns the Idle state, so its pick is scheduled rather than requested and does not
    // go through play()'s ladder: nothing can be playing but the idle loop itself.
    if (random() < kNapChance) {
        request(Role::Yawn, Priority::Situational);
    } else if (auto const pick = pickRandomIdle()) {
        enter(*pick, std::nullopt, Priority::Idle);
    }
    // Nothing was left to pick, or the playbook maps no yawn to fall back on. Re-arm rather than
    // rolling again on every tick, which would turn the odds above into a certainty.
    if (currentRole == Role::Idle and idleMs >= idleWait) {
        idleMs   = std::chrono::milliseconds{0};
        idleWait = nextIdleWait();
    }
}

void PetStateMachine::request(Role next, Priority requested)
{
    auto const named = playbook.roles.find(next);
    if (named == playbook.roles.end()) return;
    play(named->second, next, requested);
}

void PetStateMachine::play(ActionName name, std::optional<Role> role, Priority requested)
{
    // Thinking waits for the explanation that the event completes, so any event replaces it.
    auto const givesWayToThinking = currentRole == Role::Thinking and requested == Priority::Event;
    if (requested <= priority and not givesWayToThinking) return;
    enter(std::move(name), role, requested);
}

void PetStateMachine::enter(ActionName name, std::optional<Role> role, Priority newPriority)
{
    // The lookup comes first: `name` is moved into `current` below.
    auto const& spec = playbook.actions.at(name);
    pendingReturn    = spec.returnTo;
    current          = std::move(name);
    currentRole      = role;
    priority         = newPriority;
    elapsed          = std::chrono::milliseconds{0};
    if (role == Role::Idle) {
        idleMs   = std::chrono::milliseconds{0};
        idleWait = nextIdleWait();
    }
    if (role == Role::Sleep) napFor = nextNap();
}

void PetStateMachine::enterRole(Role role, Priority newPriority)
{
    enter(playbook.roles.at(role), role, newPriority);
}

void PetStateMachine::hold(Role state)
{
    held = state;
    request(state, Priority::Event);
}

void PetStateMachine::release(Role state)
{
    if (held == state) held.reset();
    if (currentRole == state) settle();
}

void PetStateMachine::settle()
{
    // A yawn's return to sleep is situational, so a learning event can still wake the pet into reading.
    // A held state returns at event priority, as it started.
    if (pendingReturn) {
        enterRole(*pendingReturn, Priority::Situational);
        return;
    }
    if (held) {
        enterRole(*held, Priority::Event);
        return;
    }
    enterRole(Role::Idle, Priority::Idle);
}

std::optional<ActionName> PetStateMachine::pickRandomIdle()
{
    if (playbook.idlePool.empty()) return std::nullopt;

    // Never the actions just played (PHASE3 3.3), but never down to nothing to play: the memory holds
    // back one fewer than the pool has, so a pool of two alternates and a pool of one repeats itself,
    // which is the only thing it can do. A larger pool gives up the whole kRecentIdle.
    auto const              remembered = std::min(recentIdle.size(), playbook.idlePool.size() - 1);
    std::vector<ActionName> choices;
    for (auto const& candidate : playbook.idlePool) {
        if (std::find(recentIdle.end() - static_cast<std::ptrdiff_t>(remembered), recentIdle.end(), candidate) == recentIdle.end())
            choices.push_back(candidate);
    }

    auto const index = std::min(static_cast<std::size_t>(random() * static_cast<double>(choices.size())),
                                choices.size() - 1);
    recentIdle.push_back(choices[index]);
    if (recentIdle.size() > kRecentIdle) recentIdle.pop_front();
    return choices[index];
}

std::chrono::milliseconds PetStateMachine::nextIdleWait()
{
    auto const jitter = static_cast<std::chrono::milliseconds::rep>(random() * static_cast<double>(kIdleCooldownSpread.count()));
    return kIdleCooldownBase + std::chrono::milliseconds{jitter};
}

std::chrono::milliseconds PetStateMachine::nextNap()
{
    auto const span = static_cast<double>((kNapMax - kNapMin).count());
    return kNapMin + std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(random() * span)};
}

std::chrono::milliseconds PetStateMachine::durationOf(ActionName const& action) const
{
    auto const& spec = playbook.actions.at(action);
    return std::chrono::milliseconds{spec.frames * 1000 / spec.fps};
}

} // namespace lens::core::pet
