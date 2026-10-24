/**
 * @file pet_scenes.cpp
 * @brief Implementation of the shipped scene catalogue: scenes.json, and the size of every sheet it names.
 */

#include "app/pet/pet_scenes.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

#include <QImageReader>

#include <nlohmann/json.hpp>

#include "app/paths.h"
#include "util/log.h"

namespace lens::app::pet {

namespace {

using nlohmann::json;

/// The top weight one mood entry may carry: above it one scene would starve the rest of the pool.
constexpr double kMaxSceneWeight{10.0};

/// @return `entry[key]` as a lottery weight in (0, kMaxSceneWeight], or 1 when the key is absent.
double moodWeightOf(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key)) return 1.0;
    if (not entry.at(key).is_number()) throw std::runtime_error{owner + "." + key + " must be a number"};
    auto const weight = entry.at(key).get<double>();
    if (not std::isfinite(weight) or weight <= 0.0)
        throw std::runtime_error{owner + "." + key + " must be a positive weight"};
    if (weight > kMaxSceneWeight)
        throw std::runtime_error{owner + "." + key + " is above " + std::to_string(kMaxSceneWeight)};
    return weight;
}

/// @return `entry[key]` as a number. Refuses a missing or non-numeric value, naming `owner`.
double numberOf(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key) or not entry.at(key).is_number())
        throw std::runtime_error{owner + "." + key + " must be a number"};
    return entry.at(key).get<double>();
}

/// @return `entry[key]` as an integer. Refuses a missing or non-integer value, naming `owner`.
int integerOf(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key) or not entry.at(key).is_number_integer())
        throw std::runtime_error{owner + "." + key + " must be an integer"};
    return entry.at(key).get<int>();
}

/// @return `entry[key]` as an integer, or @p fallback when the key is absent; a present value must be non-negative.
int optionalIntegerOf(json const& entry, char const* key, int fallback, std::string const& owner)
{
    if (not entry.contains(key)) return fallback;
    auto const value = integerOf(entry, key, owner);
    if (value < 0) throw std::runtime_error{owner + "." + key + " must not be negative"};
    return value;
}

/// @return `entry[key]` as a number, or @p fallback when the key is absent.
double optionalNumberOf(json const& entry, char const* key, double fallback, std::string const& owner)
{
    if (not entry.contains(key)) return fallback;
    return numberOf(entry, key, owner);
}

/// @return The entry style `name` spells, or a refusal naming @p owner.
SceneEntryStyle entryStyleOf(std::string const& name, std::string const& owner)
{
    if (name == "hop") return SceneEntryStyle::Hop;
    if (name == "settle") return SceneEntryStyle::Settle;
    if (name == "fade") return SceneEntryStyle::Fade;
    throw std::runtime_error{owner + ".style must be hop, settle or fade"};
}

/// @return `[x, y]` as a point. Refuses any other shape.
QPointF pointOf(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key) or not entry.at(key).is_array() or entry.at(key).size() != 2)
        throw std::runtime_error{owner + "." + key + " must be a pair"};
    auto const& pair = entry.at(key);
    if (not pair.at(0).is_number() or not pair.at(1).is_number())
        throw std::runtime_error{owner + "." + key + " must be a pair of numbers"};
    return QPointF{pair.at(0).get<double>(), pair.at(1).get<double>()};
}

/// @brief Refuses a sheet whose declared size is not `width` by `height`. The header is read; nothing is decoded.
void requireSize(std::filesystem::path const& file, int width, int height, std::string const& owner)
{
    auto const size = QImageReader{widen(file)}.size();
    if (size.width() != width or size.height() != height) {
        throw std::runtime_error{owner + " is " + std::to_string(size.width()) + "x" + std::to_string(size.height()) +
                                 ", want " + std::to_string(width) + "x" + std::to_string(height)};
    }
}

} // namespace

PetScenes::PetScenes(std::vector<Scene> scenes)
    : scenes_{std::move(scenes)}
{}

PetScenes PetScenes::load(std::filesystem::path const& directory)
{
    try {
        auto stream = std::ifstream{directory / "scenes.json"};
        if (not stream) throw std::runtime_error{"cannot open " + narrow(directory / "scenes.json")};
        auto const document = json::parse(stream);

        auto const canvas = integerOf(document, "canvas", "scenes");
        if (canvas <= 0) throw std::runtime_error{"scenes.canvas must be positive"};
        if (not document.contains("scenes") or not document.at("scenes").is_array())
            throw std::runtime_error{"scenes.scenes must be a list"};

        auto scenes = std::vector<Scene>{};
        for (auto const& entry : document.at("scenes")) {
            if (not entry.is_object()) throw std::runtime_error{"a scene must be an object"};
            auto const id    = entry.at("id").get<std::string>();
            auto const owner = "scene " + id;

            auto scene   = Scene{};
            scene.id     = id;
            scene.name   = QString::fromStdString(entry.value("name", id));
            scene.frames = integerOf(entry, "frames", owner);
            scene.fps    = integerOf(entry, "fps", owner);
            if (scene.frames <= 0 or scene.fps <= 0) throw std::runtime_error{owner + " has no frames"};
            scene.foot = pointOf(entry, "foot", owner);

            if (not entry.contains("layers") or not entry.at("layers").is_object())
                throw std::runtime_error{owner + ".layers must be an object"};
            scene.backLayer  = integerOf(entry.at("layers"), "back", owner + ".layers");
            scene.frontLayer = integerOf(entry.at("layers"), "front", owner + ".layers");

            auto const sheets = entry.at("sheets");
            auto const back   = directory / sheets.at("back").get<std::string>();
            auto const front  = directory / sheets.at("front").get<std::string>();
            requireSize(back, scene.frames * canvas, canvas, owner + " back sheet");
            requireSize(front, scene.frames * canvas, canvas, owner + " front sheet");
            scene.backSheet  = widen(back);
            scene.frontSheet = widen(front);

            if (not entry.contains("framesData") or not entry.at("framesData").is_array() or
                static_cast<int>(entry.at("framesData").size()) != scene.frames)
                throw std::runtime_error{owner + " needs one framesData entry per frame"};
            for (auto const& frame : entry.at("framesData")) {
                auto const scale = numberOf(frame, "scale", owner);
                if (scale <= 0) throw std::runtime_error{owner + " scales by a non-positive amount"};
                scene.perFrame.push_back(SceneFrame{
                    .scale = scale,
                    .dx    = numberOf(frame, "dx", owner),
                    .dy    = numberOf(frame, "dy", owner),
                });
            }

            // The entry choreography is optional as a whole (ADR 0101). Durations follow the style: a hop has a
            // crouch, a flight and a rising prop; a settle glides; a fade only brings the props in.
            if (entry.contains("entry")) {
                auto const& block = entry.at("entry");
                if (not block.is_object()) throw std::runtime_error{owner + ".entry must be an object"};
                auto const ownerEntry = owner + ".entry";
                auto const styleName  = block.value("style", std::string{"fade"});
                auto       parsed     = SceneEntry{};
                parsed.style          = entryStyleOf(styleName, ownerEntry);

                auto const hop    = parsed.style == SceneEntryStyle::Hop;
                auto const settle = parsed.style == SceneEntryStyle::Settle;
                parsed.prepMs     = optionalIntegerOf(block, "prepMs", settle ? 120 : hop ? 140
                                                                                          : 0,
                                                  ownerEntry);
                parsed.moveMs     = optionalIntegerOf(block, "moveMs", settle ? 500 : hop ? 440
                                                                                          : 0,
                                                  ownerEntry);
                parsed.propMs     = optionalIntegerOf(block, "propMs", settle ? 220 : hop ? 180
                                                                                          : 260,
                                                  ownerEntry);
                parsed.arc        = optionalNumberOf(block, "arc", hop ? 14.0 : 0.0, ownerEntry);
                parsed.cross      = optionalNumberOf(block, "cross", hop ? 0.6 : 1.0, ownerEntry);
                parsed.propRise   = optionalNumberOf(block, "propRise", hop ? 2.0 : 0.0, ownerEntry);
                if (parsed.prepMs > 10'000 or parsed.moveMs > 10'000 or parsed.propMs > 10'000)
                    throw std::runtime_error{ownerEntry + " has a duration above 10 seconds"};
                if (parsed.arc < 0.0 or parsed.arc > 64.0)
                    throw std::runtime_error{ownerEntry + ".arc must be within 0..64"};
                if (parsed.cross < 0.0 or parsed.cross > 1.0)
                    throw std::runtime_error{ownerEntry + ".cross must be within 0..1"};
                if (parsed.propRise < 0.0 or parsed.propRise > 16.0)
                    throw std::runtime_error{ownerEntry + ".propRise must be within 0..16"};

                if (block.contains("impact")) {
                    auto const& impact = block.at("impact");
                    if (not impact.is_object()) throw std::runtime_error{ownerEntry + ".impact must be an object"};
                    auto const kind = impact.value("kind", std::string{});
                    if (kind != "splash" and kind != "dust")
                        throw std::runtime_error{ownerEntry + ".impact.kind must be splash or dust"};
                    auto const at = pointOf(impact, "at", ownerEntry + ".impact");
                    if (at.x() < 0.0 or at.x() > canvas or at.y() < 0.0 or at.y() > canvas)
                        throw std::runtime_error{ownerEntry + ".impact.at must be inside the canvas"};
                    parsed.impact = SceneImpact{QString::fromStdString(kind), at};
                }
                scene.entry = std::move(parsed);
            }

            // The mood table is optional as a whole and entry by entry (ADR 0093): a scene without one weighs
            // as much as any other, an entry left out keeps 1.
            if (entry.contains("mood")) {
                auto const& moodEntry = entry.at("mood");
                if (not moodEntry.is_object()) throw std::runtime_error{owner + ".mood must be an object"};
                scene.mood.morning      = moodWeightOf(moodEntry, "morning", owner);
                scene.mood.afternoon    = moodWeightOf(moodEntry, "afternoon", owner);
                scene.mood.evening      = moodWeightOf(moodEntry, "evening", owner);
                scene.mood.night        = moodWeightOf(moodEntry, "night", owner);
                scene.mood.afterReading = moodWeightOf(moodEntry, "afterReading", owner);
                scene.mood.afterReturn  = moodWeightOf(moodEntry, "afterReturn", owner);
            }
            scenes.push_back(std::move(scene));
        }
        return PetScenes{std::move(scenes)};
    } catch (std::exception const& e) {
        // The scenes are the app's own data: losing the whole catalogue is better than drawing half a rig.
        LENS_ERROR("the pet scenes are off: {}", e.what());
        return PetScenes{};
    }
}

Scene const* PetScenes::find(std::string const& id) const
{
    for (auto const& scene : scenes_)
        if (scene.id == id) return &scene;
    return nullptr;
}

bool PetScenes::empty() const
{
    return scenes_.empty();
}

std::vector<Scene> const& PetScenes::all() const
{
    return scenes_;
}

} // namespace lens::app::pet
