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
    quiet = std::chrono::milliseconds{0};
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
    quiet += step;
    if (quiet >= kQuietBeforeYawn) {
        quiet = std::chrono::milliseconds{0};
        request(Role::Yawn, Priority::Situational);
        return;
    }
    if (currentRole == Role::Idle) {
        idleMs += step;
        if (idleMs >= idleWait) {
            if (auto const pick = pickRandomIdle()) play(*pick, std::nullopt, Priority::Situational);
        }
        return;
    }
    elapsed += step;
    if (currentRole == Role::Thinking) {
        if (elapsed >= kThinkingTimeout) settle();
        return;
    }
    if (not playbook.actions.at(current).loop and elapsed >= durationOf(current)) settle();
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
    // Never the action just played (PHASE3 3.3). The pool was checked when the playbook was parsed.
    std::vector<ActionName> choices;
    for (auto const& candidate : playbook.idlePool) {
        if (not lastIdle or candidate != *lastIdle) choices.push_back(candidate);
    }
    if (choices.empty()) return std::nullopt;

    auto const index = std::min(static_cast<std::size_t>(random() * static_cast<double>(choices.size())),
                                choices.size() - 1);
    lastIdle         = choices[index];
    return choices[index];
}

std::chrono::milliseconds PetStateMachine::nextIdleWait()
{
    auto const jitter = static_cast<std::chrono::milliseconds::rep>(random() * static_cast<double>(kIdleCooldownBase.count()));
    return kIdleCooldownBase + std::chrono::milliseconds{jitter};
}

std::chrono::milliseconds PetStateMachine::durationOf(ActionName const& action) const
{
    auto const& spec = playbook.actions.at(action);
    return std::chrono::milliseconds{spec.frames * 1000 / spec.fps};
}

} // namespace lens::core::pet
