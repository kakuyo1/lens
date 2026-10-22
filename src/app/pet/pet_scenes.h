#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <QPointF>
#include <QString>

/**
 * @file pet_scenes.h
 * @brief The scenes the app ships, read from data/scenes/scenes.json (ADR 0049).
 *
 * A scene is a prop rig baked into a back and a front sheet, plus the pet's own transform for each frame. The
 * catalogue belongs to the application rather than to a pet package; a pet can play a scene only when its action
 * table names it, which is also how a scene stays exclusive to one pet. Every shipped pet gets four, and the pet is
 * aligned by the feet measured when it loads. A broken catalogue turns scenes off for every pet rather than starting
 * half of one, and says why in the log.
 */

namespace lens::app::pet {

/// @brief One frame's transform of the pet: the shift in canvas units, and the whole-number scale about its feet.
struct SceneFrame {
    double scale;
    double dx;
    double dy;
};

/// @brief One scene: where its sheets are, how it layers, and how the pet sits in each frame.
struct Scene {
    std::string id;
    /// The scene's name as the authoring data writes it; the app does not show it yet.
    QString name;
    int     frames;
    int     fps;
    /// The pet's feet for which the props were drawn, in canvas units. The stage shifts by the pet's own feet minus this.
    QPointF                 foot;
    int                     backLayer;
    int                     frontLayer;
    QString                 backSheet;
    QString                 frontSheet;
    std::vector<SceneFrame> perFrame;
};

/// @brief The shipped scene catalogue.
class PetScenes {
public:
    /// An empty catalogue: the app starts with no scenes, and load() replaces it.
    PetScenes() = default;

    /**
     * @brief Reads `scenes.json` and checks every sheet against its frame count.
     * @param directory The data/scenes folder.
     * @return The catalogue; empty when the folder or a file is missing or wrong, with the reason logged.
     */
    static PetScenes load(std::filesystem::path const& directory);

    /// @return The scene called `id`, or nullptr.
    Scene const* find(std::string const& id) const;

    bool                      empty() const;
    std::vector<Scene> const& all() const;

private:
    explicit PetScenes(std::vector<Scene> scenes);

    std::vector<Scene> scenes_;
};

} // namespace lens::app::pet
