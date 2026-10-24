#pragma once

/**
 * @file dev_command.h
 * @brief The developer command table's grammar and the shape of its answers: one text line in, one parsed command or the
 *        reason it was refused out, and the JSON a `state` or `version` line answers with.
 *
 * Pure: no Qt and no window, so the grammar is tested without the application running. What a command does is in
 * app/dev/dev_surface.h. The table is compiled into the application only by LENS_ENABLE_DEV_SURFACE (docs/adr/0068).
 */

#include "core/domain/surface_table.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lens::core::dev {

/// The verbs the table takes. At most ten (docs/adr/0068): a new surface is a new name, not a new verb.
enum class Verb : std::uint8_t {
    SurfaceShow,
    SurfaceHide,
    SurfaceHideAll,
    SurfaceRoute,
    Wait,
    State,
    Version,
    Help,
    Selection,
    SettingSet,
    Quit,
};

/// The condition a `wait` line polls for, on the surface it names.
enum class WaitFor : std::uint8_t {
    Visible,
    Hidden,
    /// At least one frame painted after the wait began.
    Painted,
    /// A plain hold: nothing is watched, the script waits out the seconds the line gives. A measurement run needs it
    /// because the tallies the glass pipeline logs cover ten seconds each (src/app/glass/glass_stats.h).
    Seconds,
};

/// A rectangle in physical pixels, the coordinates the mouse hook reports.
struct Region {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

/// The settings a `setting set` line may change. A whitelist, not a route to every key the settings file holds.
enum class SettingKey : std::uint8_t {
    Theme,
    LiquidBlur,
    FrostedBlur,
    LiquidRefraction,
    AnimationsEnabled,
    GlassQuality,
    /// The desktop pet's switch and choice. Both go through PetController's real setters, so a dev run sees the same
    /// ceremony a reader's click does.
    PetEnabled,
    Pet,
};

/// @return The document key @p key names, as the settings table or the PET section spells it
///         (core/domain/settings_table.h, core/document/pet_store.cpp).
constexpr std::string_view settingKeyName(SettingKey const key)
{
    switch (key) {
        case SettingKey::Theme: return "theme";
        case SettingKey::LiquidBlur: return "liquidBlur";
        case SettingKey::FrostedBlur: return "frostedBlur";
        case SettingKey::LiquidRefraction: return "liquidRefraction";
        case SettingKey::AnimationsEnabled: return "animationsEnabled";
        case SettingKey::GlassQuality: return "glassQuality";
        case SettingKey::PetEnabled: return "petEnabled";
        case SettingKey::Pet: return "pet";
    }
    return {};
}

/// The value a `setting set` line gives, already checked against what its key takes: a theme name, a whole number or a
/// switch.
using SettingValue = std::variant<std::string, int, bool>;

/// One parsed line. Only the fields its verb reads are set; the rest keep their defaults.
struct Command {
    Verb        verb{Verb::Help};
    std::string surface;
    /// The `<category>` or `<category>/<page>` a `surface route` names; the panel's own method splits it.
    std::string target;
    WaitFor     waitFor{WaitFor::Visible};
    /// How long a `wait <n>s` line holds, in whole seconds; unread by every other wait.
    int          waitSeconds{0};
    Region       region;
    std::string  selectedText;
    SettingKey   settingKey{SettingKey::Theme};
    SettingValue settingValue;
};

/// The outcome of parsing one line: a command, or the reason the line was refused. Exactly one of the two is set.
struct Parse {
    std::optional<Command> command;
    std::string            error;
};

/// The surface names the table knows, in the order `state` reports them. Each is a window's object name in QML.
/// Derived from the one surface table (core/domain/surface_table.h), so this list, the glass sheet set and the dev
/// opener table cannot drift (docs/adr/0084).
inline constexpr auto kSurfaceNames = lens::core::surface::devNames();

[[nodiscard]] bool isSurfaceName(std::string_view name);

/// @brief Parse one line of the command table.
/// @param line One line without its line break. Blank and `#` comment lines are the caller's to skip.
/// @return The command, or an error that names what was wrong with the line.
[[nodiscard]] Parse parseCommand(std::string_view line);

/// The build a report was taken from. Every `state` and `version` answer carries it, so two builds are never compared
/// without the reader seeing that they differ (docs/adr/0068).
struct Build {
    /// "optimized" or "debug", from NDEBUG on the executable.
    std::string config;
    /// Whether the executable was compiled with QT_QML_DEBUG.
    bool qmlDebug{false};
};

/// What the application observed about one surface. An unset field means the surface has no such thing, and it is
/// reported as null, not left out, so a reader never has to tell the two apart.
struct SurfaceState {
    std::string                  name;
    std::optional<bool>          visible;
    std::optional<std::uint64_t> painted;
    std::optional<int>           serial;
    std::optional<int>           publishes;
    std::optional<bool>          live;
    /// The sheet's size as "WIDTHxHEIGHT".
    std::optional<std::string> sheet;
};

/// @brief What the desktop pet is doing, when this build has one to report.
struct PetState {
    std::string id;
    bool        enabled{false};
    bool        shown{false};
    std::string action;
};

/// Everything a `state` answer reports, gathered by the application.
struct StateInput {
    Build                      build;
    std::optional<std::string> theme;
    int                        windowsTotal{0};
    int                        windowsVisible{0};
    std::vector<SurfaceState>  surfaces;
    /// Empty when the pet is not bound, which a build without it reports as null rather than leaving the key out.
    std::optional<PetState> pet;
};

/// @return The `state` answer: `{"ok": true, "build", "theme", "windows", "surfaces"}`, every key present.
[[nodiscard]] nlohmann::json stateJson(StateInput const& input);

/// @return The `version` answer: `{"ok": true, "build"}`.
[[nodiscard]] nlohmann::json versionJson(Build const& build);

/// @return The `help` answer: `{"ok": true, "commands": [...]}`, one grammar line per verb.
[[nodiscard]] nlohmann::json helpJson();

} // namespace lens::core::dev
