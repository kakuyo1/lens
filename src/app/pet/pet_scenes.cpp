/**
 * @file pet_scenes.cpp
 * @brief Implementation of the shipped scene catalogue: scenes.json, and the size of every sheet it names.
 */

#include "app/pet/pet_scenes.h"

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

/// @return The absolute path of a sheet, as Qt reads it.
QString absolutePath(std::filesystem::path const& file)
{
    return QString::fromStdWString(file.wstring());
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
    auto const size = QImageReader{absolutePath(file)}.size();
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
            scene.backSheet  = absolutePath(back);
            scene.frontSheet = absolutePath(front);

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
