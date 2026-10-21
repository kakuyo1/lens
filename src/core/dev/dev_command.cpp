/**
 * @file dev_command.cpp
 * @brief The command table's grammar and answer shapes, for dev_command.h.
 */

#include "core/dev/dev_command.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>
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

/// The longest plain hold a script may ask for. Longer than this and the run is stuck on one line: the harness that
/// started it has its own timeout, and a scripted wait should not outlive the reason it is being measured.
constexpr int kMaxWaitSeconds = 120;

/// The refusal every malformed `wait` line gets, whichever form it was reaching for.
constexpr char const* const kWaitUsage = "wait takes visible:<name>, hidden:<name>, painted:<name> or <n>s";

/**
 * @brief `25s` -> hold the script for twenty-five seconds.
 * @param text what follows `wait` with no condition colon, already trimmed.
 * @return The hold command, or the reason the line was refused.
 * @note Nothing is watched: this is the form that keeps a surface on screen while the pipeline fills a tally window.
 */
Parse parseWaitSeconds(std::string_view const text)
{
    if (text.size() < 2 or text.size() > 4 or text.back() != 's') return refuse(kWaitUsage);
    auto seconds = 0;
    for (auto const digit : text.substr(0, text.size() - 1)) {
        if (digit < '0' or digit > '9') return refuse(kWaitUsage);
        seconds = seconds * 10 + (digit - '0');
    }
    if (seconds < 1 or seconds > kMaxWaitSeconds) return refuse("wait <n>s takes 1 to 120 seconds");
    return accept({.verb = Verb::Wait, .waitFor = WaitFor::Seconds, .waitSeconds = seconds});
}

/// `visible:settings` -> the condition and the surface it names, or a refusal.
Parse parseWait(std::string_view condition)
{
    auto const colon = condition.find(':');
    if (colon == std::string_view::npos)
        return parseWaitSeconds(condition);
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

/// Trim the spaces off both ends of @p text.
std::string_view trimmed(std::string_view text)
{
    auto const first = text.find_first_not_of(' ');
    if (first == std::string_view::npos)
        return {};
    auto const last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

/// @return @p text as a whole number, or nothing when any of it is not digits (a leading minus is allowed).
std::optional<int> intOf(std::string_view text)
{
    int value{};
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} or end != text.data() + text.size())
        return std::nullopt;
    return value;
}

/// `x,y,w,h` in whole pixels. x and y may be negative, a screen left of or above the primary; w and h must be above zero.
std::optional<Region> regionOf(std::string_view text)
{
    std::array<int, 4> fields{};
    for (std::size_t index = 0; index < fields.size(); ++index) {
        // Every field but the last is followed by a comma, and the last one is not.
        auto const last  = index + 1 == fields.size();
        auto const comma = text.find(',');
        if ((comma == std::string_view::npos) != last)
            return std::nullopt;
        auto const field = intOf(text.substr(0, comma));
        if (not field)
            return std::nullopt;
        fields[index] = *field;
        text.remove_prefix(last ? text.size() : comma + 1);
    }
    auto const [x, y, width, height] = fields;
    if (width <= 0 or height <= 0)
        return std::nullopt;
    return Region{.x = x, .y = y, .width = width, .height = height};
}

/// A setting whose value is a whole number. Its setter clamps, so the grammar only checks that the number is whole.
struct WholeSetting {
    std::string_view name;
    SettingKey       key;
};

constexpr std::array<WholeSetting, 3> kWholeSettings{{
    {"liquidBlur", SettingKey::LiquidBlur},
    {"frostedBlur", SettingKey::FrostedBlur},
    {"liquidRefraction", SettingKey::LiquidRefraction},
}};

constexpr std::array<std::string_view, 4> kThemeNames{"light", "dark", "frosted", "liquid"};
/// The quality tiers in the order GlassQuality numbers them (src/app/glass_settings.h).
constexpr std::array<std::string_view, 4> kGlassQualityNames{"auto", "eco", "standard", "smooth"};

/// `setting set <name> <value>`: the name must be on the whitelist, and the value must be one that setting takes.
Parse parseSetting(std::string_view name, std::string_view value)
{
    if (name == "theme") {
        if (std::ranges::find(kThemeNames, value) == kThemeNames.end())
            return refuse("theme takes light, dark, frosted or liquid");
        return accept({.verb = Verb::SettingSet, .settingKey = SettingKey::Theme, .settingValue = std::string{value}});
    }
    if (name == "animationsEnabled") {
        if (value != "on" and value != "off")
            return refuse("animationsEnabled takes on or off");
        return accept({.verb = Verb::SettingSet, .settingKey = SettingKey::AnimationsEnabled, .settingValue = value == "on"});
    }
    if (name == "glassQuality") {
        auto const tier = std::ranges::find(kGlassQualityNames, value);
        if (tier == kGlassQualityNames.end())
            return refuse("glassQuality takes auto, eco, standard or smooth");
        return accept({.verb = Verb::SettingSet, .settingKey = SettingKey::GlassQuality, .settingValue = static_cast<int>(tier - kGlassQualityNames.begin())});
    }
    auto const whole = std::ranges::find(kWholeSettings, name, &WholeSetting::name);
    if (whole == kWholeSettings.end())
        return refuse("unknown setting: " + std::string{name});
    auto const number = intOf(value);
    if (not number)
        return refuse(std::string{name} + " takes a whole number");
    return accept({.verb = Verb::SettingSet, .settingKey = whole->key, .settingValue = *number});
}

/// `selection x,y,w,h "text"`. The text is the quoted rest of the line, so it keeps the spaces inside its quotes.
Parse parseSelection(std::string_view arguments)
{
    char const* const usage = "selection takes x,y,w,h then a quoted text: whole numbers, w and h above zero, text not empty";
    auto const        quote = arguments.find('"');
    if (quote == std::string_view::npos)
        return refuse(usage);
    auto const region = regionOf(trimmed(arguments.substr(0, quote)));
    auto const quoted = trimmed(arguments.substr(quote));
    // One pair of quotes around at least one character: no quote may sit between them.
    if (not region or quoted.size() < 3 or quoted.find('"', 1) != quoted.size() - 1)
        return refuse(usage);
    return accept({.verb = Verb::Selection, .region = *region, .selectedText = std::string{quoted.substr(1, quoted.size() - 2)}});
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

    if (verb == "state" or verb == "version" or verb == "help" or verb == "quit") {
        if (args != 0)
            return refuse(std::string{verb} + " takes no arguments");
        if (verb == "state")
            return accept({.verb = Verb::State});
        if (verb == "version")
            return accept({.verb = Verb::Version});
        if (verb == "quit")
            return accept({.verb = Verb::Quit});
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

    if (verb == "selection")
        return parseSelection(line.substr(line.find(verb) + verb.size()));

    if (verb == "setting") {
        if (args != 3 or tokens[1] != "set")
            return refuse("setting takes set <name> <value>");
        return parseSetting(tokens[2], tokens[3]);
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
                         "wait visible:<name> | hidden:<name> | painted:<name> | <n>s",
                         "selection <x>,<y>,<w>,<h> \"<text>\"",
                         "setting set <name> <value>",
                         "state",
                         "version",
                         "quit",
                         "help",
                     }},
    };
}

} // namespace lens::core::dev
