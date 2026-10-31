#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <QPointF>
#include <QString>

#include "core/domain/pet/accessory.h"
#include "core/domain/pet/pet_config.h"

/**
 * @file pet_assets.h
 * @brief One pet's data folder, read at start-up and whenever the reader picks another pet, and checked against its
 *        sprite sheets (PHASE3 3.8, skills/lens-pet-maker/references/pet-package-format.md).
 *
 * The core parsers own pet.json, animations, anchors and the accessory catalogue. This file adds what only the window
 * needs: sheet paths, expression indices, layer order, each accessory's picture, and the checks on the files
 * themselves. Everything is checked before the pet starts, so a missing or wrong-sized sheet refuses the whole pet
 * and never a half-drawn one. The review sign is the one exception: a bundled pet's sign that fails is dropped alone.
 */

namespace lens::app::pet {

/// @brief Where a pet came from. The install folder and the reader's own folder are the only two sources.
enum class PetSource : std::uint8_t {
    Shipped,
    Installed,
};

/// @brief One width step of a review sign: the sheet that draws the sign at that word width.
struct ReviewSignBin {
    /// The word width this step is drawn for, in pixels.
    int width;
    /// The board's width in pixels at this step, without the outline and the shadow.
    int boardWidth;
    /// One frame's cell in the sheet, in pixels. The sheet holds the frames side by side.
    int cellWidth;
    int cellHeight;
    /// Absolute path of the sheet.
    QString sheet;
};

/// @brief A bundled pet's own review sign, as its sign.json and sheets describe it (ADR 0125, 0126).
struct ReviewSign {
    /// Frames in each sheet, and the beats each frame stays.
    int frames;
    int every;
    /// One beat of the shared clock, in milliseconds.
    int beatMs;
    /// Pixels of outline and shadow around the art.
    int margin;
    /// The sign's base row in sprite units: the pet's feet line.
    int     foot;
    int     wordTop;
    QString wordColor;
    /// Width steps, in ascending order of width.
    std::vector<ReviewSignBin> bins;
};

/**
 * @brief Text a package supplied, made safe to show: control and format characters removed, in NFC (skills/lens-pet-maker/references/pet-package-format.md
 *        section 5, rule 13). Used for the display name and for the folder names the refusal list shows.
 * @param text The text as the package wrote it.
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
    /// The idle figure's ink bounds in sheet units, right and bottom exclusive. Hover distance is measured from this
    /// box, so empty canvas margins do not make the pet feel farther away. A frame with nothing drawn covers the canvas.
    double inkLeft;
    double inkTop;
    double inkRight;
    /// The lowest drawn row's bottom edge plus one, in sheet units: the pet's feet. A scene aligns the pet by it,
    /// and a frame with nothing drawn gives the canvas bottom.
    double inkBottom;
    /// The ink box, in sheet units, of the edge-peek action's first frame: the sliver a hidden pet leaves visible is
    /// measured on the figure, not on the square canvas. The idle figure's box when the pet has no peek action.
    double                                        peekInkLeft;
    double                                        peekInkRight;
    Layers                                        layers;
    QString                                       expressionSheet;
    core::pet::Animations                         animations;
    core::pet::AnchorTable                        anchors;
    std::vector<core::pet::AccessorySpec>         catalogue;
    std::map<core::pet::ActionName, ActionSheets> sheets;
    std::map<std::string, AccessoryArt>           accessories;
    /// Expression indices the blink plays through, one per blink frame.
    std::vector<int> blinkExpressions;
    /// The review sign of a bundled pet. Empty for a package, and for a bundled pet whose sign is missing or fails its checks.
    std::optional<ReviewSign> reviewSign;

    /**
     * @brief Checks the whole folder, then reads every file under it and checks each sheet against its frame count.
     * @param directory One pet's folder, data/pet/<id> or a package under %APPDATA%\Lens\pets. Sheet paths in the
     *                  documents are relative to it.
     * @param source Where the pet came from. Only a shipped pet's review sign is read.
     * @return The whole pet.
     * @throws core::pet::Refused with the reason the package is refused, which the settings page shows.
     */
    static PetAssets load(std::filesystem::path const& directory, PetSource source);
};

} // namespace lens::app::pet
