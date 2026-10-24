#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <QPointF>
#include <QString>

#include "core/domain/pet/pet_mood.h"

/**
 * @file pet_scenes.h
 * @brief The scenes the app ships, read from data/scenes/scenes.json (ADR 0049).
 *
 * A scene is a prop rig baked into a back and a front sheet, plus the pet's own transform for each frame. The
 * catalogue belongs to the application rather than to a pet package; a pet can play a scene only when its action
 * table names it, which is also how a scene stays exclusive to one pet. Every shipped pet gets four, and the pet is
 * aligned by the feet measured when it loads. A broken catalogue turns scenes off for every pet rather than starting
 * half of one, and says why in the log. A scene may carry an optional mood table (ADR 0093), which weights the idle
 * lottery by the stretch of day and the after-contexts; without one it weighs as much as any other. It may also carry
 * an optional entry choreography (ADR 0101), which decides how the pet gets into the props.
 */

namespace lens::app::pet {

/// @brief One frame's transform of the pet: the shift in canvas units, and the whole-number scale about its feet.
struct SceneFrame {
    double scale;
    double dx;
    double dy;
};

/// @brief How the pet enters a scene (ADR 0101). A hop keeps the idle pose until it lands; settle slides the scene
///        pose into place; fade brings the props in around a pet already in its scene pose.
enum class SceneEntryStyle : std::uint8_t {
    Hop,
    Settle,
    Fade,
};

/// @brief The landing effect a hop may carry: a handful of canvas-pixel droplets or dust, drawn by QML.
struct SceneImpact {
    QString kind;
    QPointF at;
};

/// @brief The timing and geometry of one scene entry, from the scene's optional `entry` block (ADR 0101).
struct SceneEntry {
    SceneEntryStyle            style{SceneEntryStyle::Fade};
    int                        prepMs{0};
    int                        moveMs{0};
    int                        propMs{0};
    double                     arc{0};
    double                     cross{1};
    double                     propRise{0};
    std::optional<SceneImpact> impact;
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
    /// The optional mood table: the weights the idle lottery gives the scene per stretch and context.
    core::pet::SceneMood mood{};
    /// The optional entry choreography; a scene without one is a hard cut.
    std::optional<SceneEntry> entry;
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
