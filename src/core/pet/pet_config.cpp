/**
 * @file pet_config.cpp
 * @brief Implementation of the pet data parsers: pet.json, animations, anchors and accessories.
 */

#include "core/pet/pet_config.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "core/pet/pet_package.h"

namespace lens::core::pet {

namespace {

using nlohmann::json;

struct NamedRole {
    std::string_view name;
    Role             role;
    bool             required;
};

/// Role names as they appear in the data files. Stretch, angry and ask are optional (PHASE3 3.1, ADR 0049, ADR 0050):
/// a pet without the art simply never stretches, never gets angry, or shows the review sign with no pose.
constexpr std::array<NamedRole, 13> kNamedRoles{{
    {"idle", Role::Idle, true},
    {"study", Role::Study, true},
    {"thinking", Role::Thinking, true},
    {"celebrate", Role::Celebrate, true},
    {"encourage", Role::Encourage, true},
    {"sleep", Role::Sleep, true},
    {"click_react", Role::ClickReact, true},
    {"pickup", Role::Pickup, true},
    {"look_around", Role::LookAround, true},
    {"yawn", Role::Yawn, true},
    {"stretch", Role::Stretch, false},
    {"angry", Role::Angry, false},
    {"ask", Role::Ask, false},
}};

/// @return The role with this name in the data files, or nothing.
std::optional<Role> roleNamed(std::string const& name)
{
    for (auto const& entry : kNamedRoles) {
        if (entry.name == name) return entry.role;
    }
    return std::nullopt;
}

/// @return The slot with this name in accessories.json, or nothing.
std::optional<Slot> slotNamed(std::string const& name)
{
    if (name == "head") return Slot::Head;
    if (name == "face") return Slot::Face;
    if (name == "body") return Slot::Body;
    return std::nullopt;
}

/// @return `entry[key]` as an integer. A missing or non-integer value is refused as `invalid`, naming `owner`.
std::int64_t integerOf(json const& entry, char const* key, Refusal invalid, std::string const& owner)
{
    if (not entry.contains(key) or not entry.at(key).is_number_integer())
        throw Refused(invalid, owner + "." + key + " must be an integer");
    return entry.at(key).get<std::int64_t>();
}

/// @return `entry[key]` as an integer in [low, high]. Below `low` is refused as `invalid`; above `high` is TooLarge.
int integerIn(json const& entry, char const* key, std::int64_t low, std::int64_t high, Refusal invalid, std::string const& owner)
{
    auto const value = integerOf(entry, key, invalid, owner);
    if (value < low) throw Refused(invalid, owner + "." + key + " is below " + std::to_string(low));
    if (value > high) throw Refused(Refusal::TooLarge, owner + "." + key + " is above " + std::to_string(high));
    return static_cast<int>(value);
}

/// One playback entry. `returnTo` is resolved to a role here, and checked against the document's roles later.
ActionSpec parseSpec(json const& entry, std::string const& name)
{
    auto const owner = "animations: " + name;
    if (not entry.is_object()) throw Refused(Refusal::BadAnimation, owner + " must be an object");
    if (not entry.contains("loop") or not entry.at("loop").is_boolean())
        throw Refused(Refusal::BadAnimation, owner + ".loop must be a boolean");

    ActionSpec spec{
        .frames   = integerIn(entry, "frames", 1, kMaxFrames, Refusal::BadAnimation, owner),
        .fps      = integerIn(entry, "fps", 1, kMaxFps, Refusal::BadAnimation, owner),
        .loop     = entry.at("loop").get<bool>(),
        .returnTo = std::nullopt,
        .blinks   = false,
        .duration = std::nullopt,
    };
    if (entry.contains("duration")) {
        spec.duration = std::chrono::milliseconds{
            integerIn(entry, "duration", 1, kMaxDurationMs, Refusal::BadAnimation, owner)};
    }
    if (entry.contains("returnTo")) {
        if (not entry.at("returnTo").is_string()) throw Refused(Refusal::BadAnimation, owner + ".returnTo must be a string");
        auto const target = roleNamed(entry.at("returnTo").get<std::string>());
        if (not target) throw Refused(Refusal::BadAnimation, owner + ".returnTo names an unknown role");
        spec.returnTo = target;
    }
    if (entry.contains("blink")) {
        if (not entry.at("blink").is_boolean()) throw Refused(Refusal::BadAnimation, owner + ".blink must be a boolean");
        spec.blinks = entry.at("blink").get<bool>();
    }
    return spec;
}

/// @return An anchor point from a `[x, y]` pair of integers. Refuses any other shape.
Point parsePoint(json const& value, std::string const& owner)
{
    if (not value.is_array() or value.size() != 2 or not value.at(0).is_number_integer() or
        not value.at(1).is_number_integer())
        throw Refused(Refusal::BadAnchors, "anchors: " + owner + " must be a pair of integers");
    return Point{value.at(0).get<int>(), value.at(1).get<int>()};
}

/// @return The anchor point stored under `key` in one frame's entry. Refuses it if the key is absent.
Point pointOf(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key)) throw Refused(Refusal::BadAnchors, "anchors: " + owner + " is missing " + key);
    return parsePoint(entry.at(key), owner + "." + key);
}

} // namespace

PetDocument parsePetDocument(json const& document)
{
    if (not document.is_object()) throw Refused(Refusal::Format, "pet: the document must be an object");
    if (integerOf(document, "format", Refusal::Format, "pet") != 1) throw Refused(Refusal::Format, "pet: format must be 1");

    if (not document.contains("id") or not document.at("id").is_string()) throw Refused(Refusal::BadName, "pet: id must be a string");
    auto id = document.at("id").get<std::string>();
    if (not isPetId(id)) throw Refused(Refusal::BadName, "pet: id is not a pet id");

    if (not document.contains("layers") or not document.at("layers").is_object())
        throw Refused(Refusal::Format, "pet: layers must be an object");
    auto const& layers = document.at("layers");

    if (not document.contains("expression") or not document.at("expression").is_string())
        throw Refused(Refusal::Format, "pet: expression must be a path");

    std::optional<std::string> name;
    if (document.contains("name")) {
        if (not document.at("name").is_string()) throw Refused(Refusal::Format, "pet: name must be a string");
        name = document.at("name").get<std::string>();
    }

    // A layer is a z value, so any int will do; only a missing or non-integer one is refused.
    constexpr auto kLow  = static_cast<std::int64_t>(std::numeric_limits<int>::min());
    constexpr auto kHigh = static_cast<std::int64_t>(std::numeric_limits<int>::max());
    return PetDocument{
        .id              = std::move(id),
        .name            = std::move(name),
        .canvas          = integerIn(document, "canvas", kMinCanvas, kMaxCanvas, Refusal::Format, "pet"),
        .scale           = integerIn(document, "scale", kMinScale, kMaxScale, Refusal::Format, "pet"),
        .bodyLayer       = integerIn(layers, "body", kLow, kHigh, Refusal::Format, "pet.layers"),
        .expressionLayer = integerIn(layers, "expression", kLow, kHigh, Refusal::Format, "pet.layers"),
        .effectLayer     = integerIn(layers, "effect", kLow, kHigh, Refusal::Format, "pet.layers"),
        .expression      = document.at("expression").get<std::string>(),
    };
}

Animations parseAnimations(json const& document)
{
    if (not document.is_object() or not document.contains("actions") or not document.at("actions").is_object())
        throw Refused(Refusal::BadAnimation, "animations: missing actions object");
    if (not document.contains("roles") or not document.at("roles").is_object())
        throw Refused(Refusal::BadAnimation, "animations: missing roles object");
    if (not document.contains("idlePool") or not document.at("idlePool").is_array())
        throw Refused(Refusal::BadAnimation, "animations: missing idlePool array");

    Animations animations{};
    auto&      playbook = animations.playbook;
    bool       hasBlink = false;
    for (auto const& [name, entry] : document.at("actions").items()) {
        if (name == "blink") {
            animations.blink = parseSpec(entry, name);
            hasBlink         = true;
            continue;
        }
        if (not isPlainLabel(name)) throw Refused(Refusal::BadAnimation, "animations: an action name is not a plain label");
        playbook.actions.emplace(name, parseSpec(entry, name));
    }
    if (playbook.actions.size() > static_cast<std::size_t>(kMaxActions))
        throw Refused(Refusal::TooLarge, "animations: more than " + std::to_string(kMaxActions) + " actions");
    if (not hasBlink) throw Refused(Refusal::BadAnimation, "animations: blink entry missing");

    for (auto const& [roleName, action] : document.at("roles").items()) {
        auto const role = roleNamed(roleName);
        if (not role) throw Refused(Refusal::BadAnimation, "animations: unknown role " + roleName);
        if (not action.is_string()) throw Refused(Refusal::BadAnimation, "animations: role " + roleName + " must name an action");
        auto const name = action.get<std::string>();
        if (playbook.actions.count(name) == 0)
            throw Refused(Refusal::BadAnimation, "animations: role " + roleName + " names an action with no playback: " + name);
        playbook.roles.emplace(*role, name);
    }
    for (auto const& named : kNamedRoles) {
        if (named.required and playbook.roles.count(named.role) == 0)
            throw Refused(Refusal::MissingRole, "animations: required role missing: " + std::string{named.name});
    }

    for (auto const& [name, spec] : playbook.actions) {
        if (spec.returnTo and playbook.roles.count(*spec.returnTo) == 0)
            throw Refused(Refusal::BadAnimation, "animations: " + name + ".returnTo names a role the document does not map");
    }

    if (document.at("idlePool").size() > static_cast<std::size_t>(kMaxIdlePool))
        throw Refused(Refusal::TooLarge, "animations: idlePool has more than " + std::to_string(kMaxIdlePool) + " entries");
    for (auto const& entry : document.at("idlePool")) {
        if (not entry.is_string()) throw Refused(Refusal::BadAnimation, "animations: idlePool entries must be action names");
        auto const name  = entry.get<std::string>();
        auto const found = playbook.actions.find(name);
        if (found == playbook.actions.end())
            throw Refused(Refusal::BadAnimation, "animations: idlePool names an action with no playback: " + name);
        if (found->second.loop) throw Refused(Refusal::BadAnimation, "animations: idlePool action must be a one-shot: " + name);
        playbook.idlePool.push_back(name);
    }

    // An action no role names is one the pet can only reach through the idle timer, so the pool is the
    // document's list plus those. A package that gains an action gains an idle one without having to
    // name it twice. Looping actions stay out for the same reason the list refuses them: an idle
    // action that never ends is one the pet never leaves.
    for (auto const& [name, spec] : playbook.actions) {
        if (spec.loop or std::find(playbook.idlePool.begin(), playbook.idlePool.end(), name) != playbook.idlePool.end())
            continue;
        auto const isRoleAction = std::any_of(playbook.roles.begin(), playbook.roles.end(), [&name](auto const& named) { return named.second == name; });
        if (not isRoleAction) playbook.idlePool.push_back(name);
    }
    return animations;
}

AnchorTable parseAnchors(json const& document, Playbook const& playbook)
{
    if (not document.is_object() or not document.contains("actions") or not document.at("actions").is_object())
        throw Refused(Refusal::BadAnchors, "anchors: missing actions object");

    AnchorTable table;
    for (auto const& [name, entries] : document.at("actions").items()) {
        auto const playback = playbook.actions.find(name);
        if (playback == playbook.actions.end()) throw Refused(Refusal::BadAnchors, "anchors: unknown action " + name);
        if (not entries.is_array()) throw Refused(Refusal::BadAnchors, "anchors: " + name + " must be an array");

        auto const frames = playback->second.frames;
        if (static_cast<int>(entries.size()) != frames)
            throw Refused(Refusal::BadAnchors, "anchors: " + name + " has " + std::to_string(entries.size()) + " entries for " + std::to_string(frames) + " frames");

        std::vector<Anchors> perFrame;
        for (auto const& entry : entries) {
            if (not entry.is_object()) throw Refused(Refusal::BadAnchors, "anchors: " + name + " entries must be objects");
            perFrame.push_back(Anchors{
                .head = pointOf(entry, "head", name),
                .face = pointOf(entry, "face", name),
                .body = pointOf(entry, "body", name),
            });
        }
        table.emplace(name, std::move(perFrame));
    }

    for (auto const& playable : playbook.actions) {
        if (table.count(playable.first) == 0) throw Refused(Refusal::BadAnchors, "anchors: missing for " + playable.first);
    }
    return table;
}

std::vector<AccessorySpec> parseAccessories(json const& document, Playbook const& playbook)
{
    if (not document.is_object() or not document.contains("accessories") or not document.at("accessories").is_array())
        throw Refused(Refusal::BadAccessory, "accessories: missing accessories array");
    if (document.at("accessories").size() > static_cast<std::size_t>(kMaxAccessories))
        throw Refused(Refusal::TooLarge, "accessories: more than " + std::to_string(kMaxAccessories) + " accessories");

    std::vector<AccessorySpec> catalogue;
    std::set<std::string>      ids;
    for (auto const& entry : document.at("accessories")) {
        if (not entry.is_object() or not entry.contains("id") or not entry.at("id").is_string())
            throw Refused(Refusal::BadAccessory, "accessories: each accessory needs a string id");
        auto id = entry.at("id").get<std::string>();
        if (not isPlainLabel(id)) throw Refused(Refusal::BadAccessory, "accessories: an id is not a plain label");
        if (not ids.insert(id).second) throw Refused(Refusal::BadAccessory, "accessories: duplicate id " + id);

        if (not entry.contains("slot") or not entry.at("slot").is_string())
            throw Refused(Refusal::BadAccessory, "accessories: " + id + " needs a slot");
        auto const slot = slotNamed(entry.at("slot").get<std::string>());
        if (not slot) throw Refused(Refusal::BadAccessory, "accessories: " + id + " names an unknown slot");

        if (not entry.contains("supportedActions") or not entry.at("supportedActions").is_array())
            throw Refused(Refusal::BadAccessory, "accessories: " + id + " needs a supportedActions array");
        std::vector<ActionName> supported;
        for (auto const& name : entry.at("supportedActions")) {
            if (not name.is_string()) throw Refused(Refusal::BadAccessory, "accessories: " + id + ".supportedActions must be strings");
            auto action = name.get<std::string>();
            if (playbook.actions.count(action) == 0)
                throw Refused(Refusal::BadAccessory, "accessories: " + id + " supports an action the pet does not have: " + action);
            supported.push_back(std::move(action));
        }
        catalogue.push_back(AccessorySpec{std::move(id), *slot, std::move(supported)});
    }
    return catalogue;
}

} // namespace lens::core::pet
