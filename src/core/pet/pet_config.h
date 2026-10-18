#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/pet/accessory.h"
#include "core/pet/pet_state.h"

/**
 * @file pet_config.h
 * @brief Reads the desktop pet's data files: pet.json, animations, anchors and accessories (PHASE3 3.1, 3.8).
 *
 * Refusal is all or nothing. A document with a missing required role, a role or idle action that has no playback,
 * a return to a role the document does not map, an unknown role name, or a missing anchor is rejected whole, and
 * the pet does not start with a partial state. Every refusal says why, with a Refused carrying the reason from
 * pet_package.h; a value above a limit is TooLarge.
 */

namespace lens::core::pet {

/// @brief The fields of pet.json the core checks by value. The sheets it names are checked where they are read.
struct PetDocument {
    std::string id;
    /// The display name as written, or nothing. The window side cleans it, since that needs Unicode normalisation.
    std::optional<std::string> name;
    int                        canvas;
    int                        scale;
    int                        bodyLayer;
    int                        expressionLayer;
    int                        effectLayer;
    /// Path of the expression sheet, relative to the pet's folder.
    std::string expression;
};

/// @brief One frame's anchor points, in sheet units: where the head, the face and the body are drawn.
struct Point {
    int x;
    int y;
};

struct Anchors {
    Point head;
    Point face;
    Point body;
};

/// @brief The optional proximity reactions in animations.json. A pet without this block still follows the pointer with
///        its eyes; it has no greeting or fuller reaction to play.
struct HoverSpec {
    /// The one-shot action a near hold plays first. Required when `dwell` is present.
    std::optional<ActionName> notice;
    /// The one-shot action a longer near hold plays once the greeting has had its say.
    std::optional<ActionName> dwell;
    std::chrono::milliseconds noticeDelay{350};
    std::chrono::milliseconds dwellDelay{2600};
    std::chrono::milliseconds cooldown{8000};
};

/// @brief What animations.json holds: the playbook for the state machine, the blink on the expression layer, and the
///        optional hover reactions.
struct Animations {
    Playbook                 playbook;
    ActionSpec               blink;
    std::optional<HoverSpec> hover;
};

/// @brief Anchors for every action, one entry per frame.
using AnchorTable = std::map<ActionName, std::vector<Anchors>>;

/**
 * @brief Parses pet.json and checks each value against the format's rules.
 *
 * `format` must be 1. `id` must be a pet id. `canvas` is 16 to 128, `scale` is 1 to 4, and the three layer values and the
 * expression path are required.
 *
 * @param document The whole parsed pet.json.
 * @return The checked fields.
 * @throws Refused Format for a missing or wrong field, or a value below its minimum; TooLarge above a maximum; BadName
 *         for an id that is not a pet id.
 */
PetDocument parsePetDocument(nlohmann::json const& document);

/**
 * @brief Parses animations.json into the playbook for the state machine and into the blink.
 *
 * The document has three required parts and one optional one. `roles` maps every role the product plays to an action
 * the document defines; only stretch and angry may be left out. `idlePool` lists one-shot actions the idle timer may
 * pick, and may be empty. `actions` gives playback for every action, plus the reserved `blink` entry. Each action needs
 * a boolean `loop`, and `frames` and `fps` of at least 1. `duration` is optional and positive: it ends the action when
 * it runs out, which is how a looping scene finishes. `returnTo` is optional, but when present it must name a role the
 * document maps. `blink: true` on an action marks it as blinking. `hover` is optional: it names the one-shot actions a
 * near hold plays (`notice`, then `dwell`), with optional `noticeDelayMs`, `dwellDelayMs` and `cooldownMs`; a dwell
 * needs a notice, and both actions must be one-shot.
 *
 * The pool the playbook carries is the document's list plus every one-shot action no role names, in
 * document order then by name. Those are the actions nothing else can reach, so a pet that gains one
 * gains an idle one without the data naming it twice.
 *
 * @param document The whole parsed animations.json.
 * @return The state machine's playbook and the blink.
 * @throws Refused BadAnimation or MissingRole, or TooLarge above a limit of pet_package.h; the message names the field.
 */
Animations parseAnimations(nlohmann::json const& document);

/**
 * @brief Parses the anchors of every action, and checks them against that action's frame count.
 *
 * Every action in the playbook needs exactly one entry per frame, each with a head, a face and a body point, each
 * a pair of integers. An anchor entry for an action the playbook does not define is refused.
 *
 * @param document The whole parsed anchors.json, with an `actions` object.
 * @param playbook The playbook from parseAnimations, which gives each action's frame count.
 * @return The anchors of every action.
 * @throws Refused BadAnchors if an action has no anchors, too few or too many, or a malformed point.
 */
AnchorTable parseAnchors(nlohmann::json const& document, Playbook const& playbook);

/**
 * @brief Parses accessories.json into the catalogue a Wardrobe is built from.
 *
 * Each accessory needs an id, which must be unique and plain, a slot that is one of head, face or body, and a
 * supportedActions list. Every name in that list must be an action the playbook defines.
 *
 * @param document The whole parsed accessories.json, with an `accessories` array.
 * @param playbook The playbook from parseAnimations, which says which action names exist.
 * @return The catalogue, in document order.
 * @throws Refused BadAccessory if an accessory is malformed, its slot is unknown, its id is repeated or not plain, or it
 *         names an action the pet does not have; TooLarge above the accessory limit.
 */
std::vector<AccessorySpec> parseAccessories(nlohmann::json const& document, Playbook const& playbook);

} // namespace lens::core::pet
