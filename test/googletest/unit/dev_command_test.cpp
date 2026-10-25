/**
 * @file dev_command_test.cpp
 * @brief The developer command table's grammar and the shape of its answers (docs/adr/0068): a line parses to one
 *        command or is refused with a reason, and a state answer keeps every key, null where nothing was observed.
 */

#include <gtest/gtest.h>

#include "core/dev/dev_command.h"

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

TEST(DevCommandParse, SurfaceRouteNamesTheSurfaceAndItsTarget)
{
    auto const category = parseCommand("surface route settings learning");
    ASSERT_TRUE(category.command);
    EXPECT_EQ(category.command->verb, Verb::SurfaceRoute);
    EXPECT_EQ(category.command->surface, "settings");
    EXPECT_EQ(category.command->target, "learning");

    // A sub-page rides the same token: the panel splits the path and walks its own steps.
    auto const page = parseCommand("surface route settings general/font");
    ASSERT_TRUE(page.command);
    EXPECT_EQ(page.command->target, "general/font");

    // Only a surface whose row names a route method takes a target, and it takes exactly one.
    for (auto const* line : {
             "surface route tray learning",
             "surface route nowhere general/font",
             "surface route settings",
             "surface route settings general font",
         }) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
}

TEST(DevCommandParse, SurfaceScrollPinsTheContentPosition)
{
    auto const scrolled = parseCommand("surface scroll settings 250");
    ASSERT_TRUE(scrolled.command);
    EXPECT_EQ(scrolled.command->verb, Verb::SurfaceScroll);
    EXPECT_EQ(scrolled.command->surface, "settings");
    EXPECT_EQ(scrolled.command->scrollY, 250);

    // The top is a position like any other. Whether the surface has anything to scroll, and whether
    // it is up, is the window's to answer at run time, so a known name parses even for a surface
    // that would refuse the line (docs/adr/0105).
    auto const top = parseCommand("surface scroll tray 0");
    ASSERT_TRUE(top.command);
    EXPECT_EQ(top.command->scrollY, 0);

    for (auto const* line : {
             "surface scroll nowhere 250",
             "surface scroll settings -1",
             "surface scroll settings 12.5",
             "surface scroll settings",
             "surface scroll settings 250 extra",
         }) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
}

TEST(DevCommandParse, SurfaceWheelTurnsTheReadersOwnTicks)
{
    auto const down = parseCommand("surface wheel words 30");
    ASSERT_TRUE(down.command);
    EXPECT_EQ(down.command->verb, Verb::SurfaceWheel);
    EXPECT_EQ(down.command->surface, "words");
    EXPECT_EQ(down.command->wheelSteps, 30);

    // Up is the same gesture the other way: the sign is the whole difference.
    auto const up = parseCommand("surface wheel words -30");
    ASSERT_TRUE(up.command);
    EXPECT_EQ(up.command->wheelSteps, -30);

    // The touchpad's shape of the same gesture: pixel deltas in phases, and the momentum a
    // Flickable keeps up when the fingers lift.
    auto const pad = parseCommand("surface wheel words 12 pad");
    ASSERT_TRUE(pad.command);
    EXPECT_EQ(pad.command->wheelSteps, 12);
    EXPECT_TRUE(pad.command->wheelPad);

    for (auto const* line : {
             "surface wheel nowhere 30",
             "surface wheel words 0",
             "surface wheel words 2.5",
             "surface wheel words",
             "surface wheel words 30 extra",
             "surface wheel words 12 taps",
             "surface wheel words 12 pad extra",
         }) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
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

    // The pet's switch and choice are settings too, so a dev run can drive the ceremony without a pointer.
    auto const petOn = parseCommand("setting set petEnabled on");
    ASSERT_TRUE(petOn.command);
    EXPECT_EQ(petOn.command->settingKey, SettingKey::PetEnabled);
    EXPECT_TRUE(std::get<bool>(petOn.command->settingValue));

    auto const pet = parseCommand("setting set pet ghost");
    ASSERT_TRUE(pet.command);
    EXPECT_EQ(pet.command->settingKey, SettingKey::Pet);
    EXPECT_EQ(std::get<std::string>(pet.command->settingValue), "ghost");
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
             "setting set petEnabled maybe",
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
    EXPECT_TRUE(state.at("pet").is_null()) << "an unbound pet is null, not an absent key";
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

TEST(DevCommandState, ThePetReportsItsSwitchWindowAndAction)
{
    lens::core::dev::StateInput input;
    input.pet = lens::core::dev::PetState{.id = "ghost", .enabled = true, .shown = true, .action = "walk"};

    auto const pet = lens::core::dev::stateJson(input).at("pet");
    ASSERT_TRUE(pet.is_object());
    EXPECT_EQ(pet.at("id").get<std::string>(), "ghost");
    EXPECT_TRUE(pet.at("enabled").get<bool>());
    EXPECT_TRUE(pet.at("shown").get<bool>());
    EXPECT_EQ(pet.at("action").get<std::string>(), "walk");
}

} // namespace
