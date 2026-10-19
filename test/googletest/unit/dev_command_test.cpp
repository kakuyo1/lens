/**
 * @file dev_command_test.cpp
 * @brief The developer command table's grammar and the shape of its answers (docs/adr/0068): a line parses to one
 *        command or is refused with a reason, and a state answer keeps every key, null where nothing was observed.
 */

#include <gtest/gtest.h>

#include "core/dev_command.h"

#include <nlohmann/json.hpp>

namespace {

using lens::core::dev::SettingKey;
using lens::core::dev::Verb;
using lens::core::dev::WaitFor;
using lens::core::dev::parseCommand;

TEST(DevCommandParse, SurfaceShowAndHideNameTheSurface)
{
    auto const shown = parseCommand("surface show settings");
    ASSERT_TRUE(shown.command);
    EXPECT_EQ(shown.command->verb, Verb::SurfaceShow);
    EXPECT_EQ(shown.command->surface, "settings");

    auto const hidden = parseCommand("  surface   hide   tray ");
    ASSERT_TRUE(hidden.command);
    EXPECT_EQ(hidden.command->verb, Verb::SurfaceHide);
    EXPECT_EQ(hidden.command->surface, "tray");
}

TEST(DevCommandParse, HideAllTakesNoName)
{
    auto const parsed = parseCommand("surface hide-all");
    ASSERT_TRUE(parsed.command);
    EXPECT_EQ(parsed.command->verb, Verb::SurfaceHideAll);
    EXPECT_FALSE(parseCommand("surface hide-all settings").command);
}

TEST(DevCommandParse, WaitReadsItsConditionAndSurface)
{
    auto const painted = parseCommand("wait painted:settings");
    ASSERT_TRUE(painted.command);
    EXPECT_EQ(painted.command->verb, Verb::Wait);
    EXPECT_EQ(painted.command->waitFor, WaitFor::Painted);
    EXPECT_EQ(painted.command->surface, "settings");
    EXPECT_EQ(parseCommand("wait hidden:tray").command->waitFor, WaitFor::Hidden);
}

TEST(DevCommandParse, WaitSecondsHoldsTheScript)
{
    // A hold is the form a measurement run uses: it keeps a surface on screen while the pipeline fills a tally window.
    auto const held = parseCommand("wait 25s");
    ASSERT_TRUE(held.command);
    EXPECT_EQ(held.command->verb, Verb::Wait);
    EXPECT_EQ(held.command->waitFor, WaitFor::Seconds);
    EXPECT_EQ(held.command->waitSeconds, 25);
    EXPECT_TRUE(parseCommand("wait 1s").command);
    EXPECT_TRUE(parseCommand("wait 120s").command);
    // A condition wait carries no duration, and its own ceiling is what bounds it.
    EXPECT_EQ(parseCommand("wait painted:settings").command->waitSeconds, 0);
    for (auto const* line : {"wait 0s", "wait 121s", "wait s", "wait 25", "wait abcs", "wait -5s", "wait 9999s"}) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
}

TEST(DevCommandParse, RefusesWithAReason)
{
    // A refused line is never silently dropped: it has no command and says why.
    for (auto const* line : {"", "   ", "frobnicate", "surface", "surface show", "surface show nowhere", "surface toggle settings", "state now", "wait settings", "wait visible:nowhere", "wait later:settings"}) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
}

TEST(DevCommandParse, SelectionTakesARectangleAndItsQuotedText)
{
    auto const parsed = parseCommand(R"(selection 160,175,620,70 "the quiet  library")");
    ASSERT_TRUE(parsed.command);
    EXPECT_EQ(parsed.command->verb, Verb::Selection);
    EXPECT_EQ(parsed.command->region.x, 160);
    EXPECT_EQ(parsed.command->region.y, 175);
    EXPECT_EQ(parsed.command->region.width, 620);
    EXPECT_EQ(parsed.command->region.height, 70);
    // The text keeps the spaces inside its quotes, which the token split would have collapsed.
    EXPECT_EQ(parsed.command->selectedText, "the quiet  library");
}

TEST(DevCommandParse, SelectionAllowsANegativeOrigin)
{
    // A screen left of or above the primary has negative physical coordinates.
    auto const parsed = parseCommand(R"(selection -1200,-40,300,20 "x")");
    ASSERT_TRUE(parsed.command);
    EXPECT_EQ(parsed.command->region.x, -1200);
    EXPECT_EQ(parsed.command->region.y, -40);
}

TEST(DevCommandParse, SelectionRefusesAMalformedLine)
{
    for (auto const* line : {
             "selection",
             "selection 160,175,620,70",
             R"(selection 160,175,620,70 "")",
             R"(selection 160,175,620 "x")",
             R"(selection 160,175,0,70 "x")",
             R"(selection 160,175,620,70 "a"b")",
             R"(selection 160,175,620,70 the quiet)",
             R"(selection 160.5,175,620,70 "x")",
         }) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
}

TEST(DevCommandParse, SettingSetNamesTheSettingAndItsValue)
{
    auto const theme = parseCommand("setting set theme light");
    ASSERT_TRUE(theme.command);
    EXPECT_EQ(theme.command->verb, Verb::SettingSet);
    EXPECT_EQ(theme.command->settingKey, SettingKey::Theme);
    EXPECT_EQ(std::get<std::string>(theme.command->settingValue), "light");

    auto const blur = parseCommand("setting set liquidBlur 40");
    ASSERT_TRUE(blur.command);
    EXPECT_EQ(blur.command->settingKey, SettingKey::LiquidBlur);
    EXPECT_EQ(std::get<int>(blur.command->settingValue), 40);

    auto const animations = parseCommand("setting set animationsEnabled off");
    ASSERT_TRUE(animations.command);
    EXPECT_EQ(animations.command->settingKey, SettingKey::AnimationsEnabled);
    EXPECT_FALSE(std::get<bool>(animations.command->settingValue));

    auto const quality = parseCommand("setting set glassQuality smooth");
    ASSERT_TRUE(quality.command);
    EXPECT_EQ(quality.command->settingKey, SettingKey::GlassQuality);
    EXPECT_EQ(std::get<int>(quality.command->settingValue), 3);
}

TEST(DevCommandParse, SettingSetRefusesAKeyOrValueOffTheWhitelist)
{
    for (auto const* line : {
             "setting",
             "setting set theme",
             "setting set theme sepia",
             "setting set nothing 1",
             "setting set liquidBlur forty",
             "setting set animationsEnabled maybe",
             "setting set glassQuality fastest",
             "setting get theme",
             "setting theme light",
         }) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
}

TEST(DevCommandParse, QuitTakesNoArguments)
{
    auto const parsed = parseCommand("quit");
    ASSERT_TRUE(parsed.command);
    EXPECT_EQ(parsed.command->verb, Verb::Quit);
    EXPECT_FALSE(parseCommand("quit now").command);
}

TEST(DevCommandState, UnsetFieldsAreNullNotAbsent)
{
    lens::core::dev::StateInput input;
    input.build          = {.config = "optimized", .qmlDebug = false};
    input.windowsTotal   = 12;
    input.windowsVisible = 2;
    input.surfaces       = {
        {.name = "settings", .visible = true, .painted = 41, .serial = 812, .publishes = 812, .live = true, .sheet = "1075x1231"},
        {.name = "reviewSign"},
    };

    auto const state = lens::core::dev::stateJson(input);
    EXPECT_TRUE(state.at("ok").get<bool>());
    EXPECT_EQ(state.at("build").at("config").get<std::string>(), "optimized");
    EXPECT_TRUE(state.at("theme").is_null());
    EXPECT_EQ(state.at("windows").at("visible").get<int>(), 2);

    ASSERT_EQ(state.at("surfaces").size(), 2u);
    EXPECT_EQ(state.at("surfaces")[0].at("painted").get<int>(), 41);
    EXPECT_EQ(state.at("surfaces")[0].at("sheet").get<std::string>(), "1075x1231");

    // A surface with no window and no sheet keeps all seven keys, each one null.
    auto const& empty = state.at("surfaces")[1];
    EXPECT_EQ(empty.size(), 7u);
    EXPECT_TRUE(empty.at("visible").is_null());
    EXPECT_TRUE(empty.at("sheet").is_null());
}

} // namespace
