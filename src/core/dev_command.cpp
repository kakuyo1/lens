/**
 * @file dev_command.cpp
 * @brief The command table's grammar and answer shapes, for dev_command.h.
 */

#include "core/dev_command.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace lens::core::dev {

namespace {

/// Split on spaces, dropping the empty runs between them.
std::vector<std::string_view> tokensOf(std::string_view line)
{
    std::vector<std::string_view> tokens;
    for (auto start = line.find_first_not_of(' '); start != std::string_view::npos; start = line.find_first_not_of(' ')) {
        line.remove_prefix(start);
        auto const end = line.find(' ');
        tokens.push_back(line.substr(0, end));
        line.remove_prefix(end == std::string_view::npos ? line.size() : end);
    }
    return tokens;
}

Parse refuse(std::string error)
{
    return Parse{.command = std::nullopt, .error = std::move(error)};
}

Parse accept(Command command)
{
    return Parse{.command = std::move(command), .error = {}};
}

/// `visible:settings` -> the condition and the surface it names, or a refusal.
Parse parseWait(std::string_view condition)
{
    auto const colon = condition.find(':');
    if (colon == std::string_view::npos)
        return refuse("wait takes visible:<name>, hidden:<name> or painted:<name>");
    auto const kind = condition.substr(0, colon);
    auto const name = condition.substr(colon + 1);
    if (not isSurfaceName(name))
        return refuse("unknown surface: " + std::string{name});
    if (kind == "visible")
        return accept({.verb = Verb::Wait, .surface = std::string{name}, .waitFor = WaitFor::Visible});
    if (kind == "hidden")
        return accept({.verb = Verb::Wait, .surface = std::string{name}, .waitFor = WaitFor::Hidden});
    if (kind == "painted")
        return accept({.verb = Verb::Wait, .surface = std::string{name}, .waitFor = WaitFor::Painted});
    return refuse("unknown wait condition: " + std::string{kind});
}

/// The JSON for an optional value: its value, or null when it is unset.
template <typename T>
nlohmann::json orNull(std::optional<T> const& value)
{
    // Parentheses, not braces: a braced single value is a one-element array in nlohmann::json.
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json buildJson(Build const& build)
{
    return {
        {"config", build.config},
        {"devSurface", true},
        {"qmlDebug", build.qmlDebug},
    };
}

} // namespace

bool isSurfaceName(std::string_view name)
{
    return std::ranges::find(kSurfaceNames, name) != kSurfaceNames.end();
}

Parse parseCommand(std::string_view line)
{
    auto const tokens = tokensOf(line);
    if (tokens.empty())
        return refuse("empty command");

    auto const verb = tokens.front();
    auto const args = tokens.size() - 1;

    if (verb == "state" or verb == "version" or verb == "help") {
        if (args != 0)
            return refuse(std::string{verb} + " takes no arguments");
        if (verb == "state")
            return accept({.verb = Verb::State});
        if (verb == "version")
            return accept({.verb = Verb::Version});
        return accept({.verb = Verb::Help});
    }

    if (verb == "surface") {
        if (args == 1 and tokens[1] == "hide-all")
            return accept({.verb = Verb::SurfaceHideAll});
        if (args != 2 or (tokens[1] != "show" and tokens[1] != "hide"))
            return refuse("surface takes show <name>, hide <name> or hide-all");
        if (not isSurfaceName(tokens[2]))
            return refuse("unknown surface: " + std::string{tokens[2]});
        auto const shown = tokens[1] == "show";
        return accept({.verb = shown ? Verb::SurfaceShow : Verb::SurfaceHide, .surface = std::string{tokens[2]}});
    }

    if (verb == "wait") {
        if (args != 1)
            return refuse("wait takes one condition: visible:<name>, hidden:<name> or painted:<name>");
        return parseWait(tokens[1]);
    }

    return refuse("unknown verb: " + std::string{verb});
}

nlohmann::json stateJson(StateInput const& input)
{
    auto surfaces = nlohmann::json::array();
    for (auto const& surface : input.surfaces) {
        surfaces.push_back({
            {"name", surface.name},
            {"visible", orNull(surface.visible)},
            {"painted", orNull(surface.painted)},
            {"serial", orNull(surface.serial)},
            {"publishes", orNull(surface.publishes)},
            {"live", orNull(surface.live)},
            {"sheet", orNull(surface.sheet)},
        });
    }
    return {
        {"ok", true},
        {"build", buildJson(input.build)},
        {"theme", orNull(input.theme)},
        {"windows", {{"total", input.windowsTotal}, {"visible", input.windowsVisible}}},
        {"surfaces", surfaces},
    };
}

nlohmann::json versionJson(Build const& build)
{
    return {{"ok", true}, {"build", buildJson(build)}};
}

nlohmann::json helpJson()
{
    return {
        {"ok", true},
        {"commands", {
                         "surface show <name>",
                         "surface hide <name>",
                         "surface hide-all",
                         "wait visible:<name> | hidden:<name> | painted:<name>",
                         "state",
                         "version",
                         "help",
                     }},
    };
}

} // namespace lens::core::dev
