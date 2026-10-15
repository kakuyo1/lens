#pragma once

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

/// @brief What animations.json holds: the playbook for the state machine, and the blink on the expression layer.
struct Animations {
    Playbook   playbook;
    ActionSpec blink;
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
 * The document has three parts. `roles` maps every role the product plays to an action the document defines; only
 * stretch may be left out. `idlePool` lists one-shot actions the idle timer may pick, and may be empty. `actions`
 * gives playback for every action, plus the reserved `blink` entry. Each action needs a boolean `loop`, and `frames`
 * and `fps` of at least 1. `returnTo` is optional, but when present it must name a role the document maps.
 * `blink: true` on an action marks it as blinking.
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
