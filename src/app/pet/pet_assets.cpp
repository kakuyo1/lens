/**
 * @file pet_assets.cpp
 * @brief Implementation of PetAssets::load: the data files, and the size of every sprite sheet they name.
 */

#include "app/pet/pet_assets.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <QImage>
#include <QImageReader>
#include <QSize>

#include "app/pet/pet_store.h"

namespace lens::app::pet {

namespace {

using nlohmann::json;

/// Alpha at or above this counts as drawn, so faint anti-aliased edges do not pull the centre sideways.
constexpr int kInkAlpha = 40;

/// @return The document under `directory`, parsed. Refuses a file that cannot be opened.
json readDocument(std::filesystem::path const& directory, char const* name)
{
    auto stream = std::ifstream{directory / name};
    if (not stream) throw std::runtime_error(std::string{"pet: cannot open "} + name);
    return json::parse(stream);
}

/// @return The absolute path of a file named relative to the data directory, as Qt reads it.
QString absolutePath(std::filesystem::path const& directory, std::string const& relative)
{
    return QString::fromStdWString((directory / relative).wstring());
}

/// @return The pixel size of a sprite sheet. Refuses one that is missing or unreadable.
QSize sheetSize(QString const& path)
{
    auto const size = QImageReader{path}.size();
    if (not size.isValid()) throw std::runtime_error("pet: sheet missing or unreadable: " + path.toStdString());
    return size;
}

/// @brief Refuses a sheet whose pixel size is not `expected`. Frames are laid out in one row, so the size is exact.
void requireSheet(QString const& path, QSize expected)
{
    if (sheetSize(path) != expected) {
        throw std::runtime_error("pet: sheet is not " + std::to_string(expected.width()) + "x" +
                                 std::to_string(expected.height()) + ": " + path.toStdString());
    }
}

/// @return The horizontal centre of what the first frame of the sheet at `path` draws, in sheet units. The canvas
///         centre when it draws nothing.
double inkCentreOf(QString const& path, int canvas)
{
    auto const sheet = QImageReader{path}.read().convertToFormat(QImage::Format_ARGB32);
    auto       left  = canvas;
    auto       right = 0;
    for (auto y = 0; y < canvas; ++y) {
        for (auto x = 0; x < canvas; ++x) {
            if (qAlpha(sheet.pixel(x, y)) < kInkAlpha) continue;
            left  = std::min(left, x);
            right = std::max(right, x + 1);
        }
    }
    return right == 0 ? canvas / 2.0 : (left + right) / 2.0;
}

/// @return The expression index of each frame, or nothing when the action keeps expression 0.
std::vector<int> expressionsOf(json const& entry)
{
    if (not entry.contains("expressions")) return {};
    return entry.at("expressions").get<std::vector<int>>();
}

} // namespace

PetAssets PetAssets::load(std::filesystem::path const& directory)
{
    auto const  pet    = readDocument(directory, "pet.json");
    auto const& layers = pet.at("layers");

    PetAssets assets{};
    assets.canvas = pet.at("canvas").get<int>();
    assets.scale  = pet.at("scale").get<int>();
    if (assets.scale < kMinScale or assets.scale > kMaxScale)
        throw std::runtime_error("pet: scale must be between " + std::to_string(kMinScale) + " and " + std::to_string(kMaxScale));
    assets.layers          = Layers{layers.at("body").get<int>(), layers.at("expression").get<int>(), layers.at("effect").get<int>()};
    assets.expressionSheet = absolutePath(directory, pet.at("expression").get<std::string>());

    // The expression sheet is one row of expression cells; only its height is fixed by the canvas.
    auto const expressionSize = sheetSize(assets.expressionSheet);
    if (expressionSize.height() != assets.canvas or expressionSize.width() % assets.canvas != 0)
        throw std::runtime_error("pet: expression sheet is not a row of canvas-sized cells: " +
                                 assets.expressionSheet.toStdString());

    auto const animations = readDocument(directory, "animations.json");
    assets.animations     = core::pet::parseAnimations(animations);
    for (auto const& [name, entry] : animations.at("actions").items()) {
        if (name == "blink") {
            assets.blinkExpressions = expressionsOf(entry);
            continue;
        }
        auto const frames = assets.animations.playbook.actions.at(name).frames;
        auto const row    = QSize{frames * assets.canvas, assets.canvas};

        ActionSheets pictures;
        pictures.body = absolutePath(directory, entry.at("sheet").get<std::string>());
        requireSheet(pictures.body, row);
        if (entry.contains("effect")) {
            pictures.effect = absolutePath(directory, entry.at("effect").get<std::string>());
            requireSheet(pictures.effect, row);
        }
        pictures.expressions = expressionsOf(entry);
        assets.sheets.emplace(name, std::move(pictures));
    }
    assets.inkCentre = inkCentreOf(assets.sheets.at("idle").body, assets.canvas);

    assets.anchors = core::pet::parseAnchors(readDocument(directory, "anchors.json"), assets.animations.playbook);

    auto const accessories = readDocument(directory, "accessories.json");
    assets.catalogue       = core::pet::parseAccessories(accessories, assets.animations.playbook);
    for (auto const& entry : accessories.at("accessories")) {
        auto const asset = absolutePath(directory, entry.at("asset").get<std::string>());
        requireSheet(asset, QSize{assets.canvas, assets.canvas});
        assets.accessories.emplace(entry.at("id").get<std::string>(), AccessoryArt{asset, entry.at("zIndex").get<int>()});
    }
    return assets;
}

} // namespace lens::app::pet
