#pragma once

/**
 * @file dev_command.h
 * @brief The developer command table's grammar and the shape of its answers: one text line in, one parsed command or the
 *        reason it was refused out, and the JSON a `state` or `version` line answers with.
 *
 * Pure: no Qt and no window, so the grammar is tested without the application running. What a command does is in
 * app/dev_surface.h. The table is compiled into the application only by LENS_ENABLE_DEV_SURFACE (docs/adr/0068).
 */

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lens::core::dev {

/// The verbs the table takes. At most ten (docs/adr/0068): a new surface is a new name, not a new verb.
enum class Verb : std::uint8_t {
    SurfaceShow,
    SurfaceHide,
    SurfaceHideAll,
    Wait,
    State,
    Version,
    Help,
};

/// The condition a `wait` line polls for, on the surface it names.
enum class WaitFor : std::uint8_t {
    Visible,
    Hidden,
    /// At least one frame painted after the wait began.
    Painted,
};

/// One parsed line. Only the fields its verb reads are set; the rest keep their defaults.
struct Command {
    Verb        verb{Verb::Help};
    std::string surface;
    WaitFor     waitFor{WaitFor::Visible};
};

/// The outcome of parsing one line: a command, or the reason the line was refused. Exactly one of the two is set.
struct Parse {
    std::optional<Command> command;
    std::string            error;
};

/// The surface names the table knows, in the order `state` reports them. Each is a window's object name in QML.
inline constexpr std::array<std::string_view, 12> kSurfaceNames{
    "settings",
    "tray",
    "stats",
    "words",
    "yearWords",
    "cost",
    "bubble",
    "selectionBar",
    "notice",
    "update",
    "removal",
    "reviewSign",
};

/// @param name A candidate surface name.
/// @return Whether @p name is one of kSurfaceNames.
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

/// Everything a `state` answer reports, gathered by the application.
struct StateInput {
    Build                      build;
    std::optional<std::string> theme;
    int                        windowsTotal{0};
    int                        windowsVisible{0};
    std::vector<SurfaceState>  surfaces;
};

/// @return The `state` answer: `{"ok": true, "build", "theme", "windows", "surfaces"}`, every key present.
[[nodiscard]] nlohmann::json stateJson(StateInput const& input);

/// @return The `version` answer: `{"ok": true, "build"}`.
[[nodiscard]] nlohmann::json versionJson(Build const& build);

/// @return The `help` answer: `{"ok": true, "commands": [...]}`, one grammar line per verb.
[[nodiscard]] nlohmann::json helpJson();

} // namespace lens::core::dev
