#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <QPointF>
#include <QString>

#include "core/pet/accessory.h"
#include "core/pet/pet_config.h"

/**
 * @file pet_assets.h
 * @brief One pet's data folder, read at start-up and whenever the reader picks another pet, and checked against its
 *        sprite sheets (PHASE3 3.8, docs/pet-package-format.md).
 *
 * The core parsers own pet.json, animations, anchors and the accessory catalogue. This file adds what only the window
 * needs: sheet paths, expression indices, layer order, each accessory's picture, and the checks on the files
 * themselves. Everything is checked before the pet starts, so a missing or wrong-sized sheet refuses the whole pet
 * and never a half-drawn one.
 */

namespace lens::app::pet {

/**
 * @brief Text a package supplied, made safe to show: control and format characters removed, in NFC (docs/pet-package-format.md
 *        section 5, rule 13). Used for the display name and for the folder names the refusal list shows.
 * @param text The text as the package wrote it.
 * @return The text without those characters.
 */
QString plainText(QString const& text);

/// @brief Layer order from pet.json. A larger z is drawn on top.
struct Layers {
    int body;
    int expression;
    int effect;
};

/// @brief Where one action's pictures are, and which expression each frame wears.
struct ActionSheets {
    /// Absolute path of the body sheet: one frame per cell, left to right.
    QString body;
    /// Absolute path of the effect sheet; empty when the action has no effect layer.
    QString effect;
    /// Absolute path of the left-hand effect sheet; empty when the action has none (only the ask action holds a sign).
    QString effectLeft;
    /// Where the right hand grips the sign's pole in each frame, in sheet units; empty when the action has no right hand.
    std::vector<QPointF> gripRight;
    /// Where the left hand grips the sign's pole in each frame, in sheet units; empty when the action has no left hand.
    std::vector<QPointF> gripLeft;
    /// Expression sheet index for each frame; empty means expression 0 throughout.
    std::vector<int> expressions;
};

/// @brief One accessory's picture and stacking, from accessories.json.
struct AccessoryArt {
    QString asset;
    int     zIndex;
};

/// @brief Everything the pet draws with, parsed and verified.
struct PetAssets {
    /// The name the settings show: the package's own, with control and format characters removed, or the id when it has none.
    QString name;
    /// Side of one square frame, in sheet units.
    int canvas;
    /// Whole-number magnification on screen.
    int scale;
    /// Horizontal centre of the figure the idle action draws in its first frame, in sheet units. The art is not
    /// centred on its canvas, so the settings preview centres on this instead; a frame with nothing drawn gives the canvas centre.
    double inkCentre;
    /// The lowest drawn row's bottom edge plus one, in sheet units: the pet's feet. A scene aligns the pet by it,
    /// and a frame with nothing drawn gives the canvas bottom.
    double                                        inkBottom;
    Layers                                        layers;
    QString                                       expressionSheet;
    core::pet::Animations                         animations;
    core::pet::AnchorTable                        anchors;
    std::vector<core::pet::AccessorySpec>         catalogue;
    std::map<core::pet::ActionName, ActionSheets> sheets;
    std::map<std::string, AccessoryArt>           accessories;
    /// Expression indices the blink plays through, one per blink frame.
    std::vector<int> blinkExpressions;

    /**
     * @brief Checks the whole folder, then reads every file under it and checks each sheet against its frame count.
     * @param directory One pet's folder, data/pet/<id> or a package under %APPDATA%\Lens\pets. Sheet paths in the
     *                  documents are relative to it.
     * @return The whole pet.
     * @throws core::pet::Refused with the reason the package is refused, which the settings page shows.
     */
    static PetAssets load(std::filesystem::path const& directory);
};

} // namespace lens::app::pet
