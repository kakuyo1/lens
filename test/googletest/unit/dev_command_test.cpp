/**
 * @file dev_command_test.cpp
 * @brief The developer command table's grammar and the shape of its answers (docs/adr/0068): a line parses to one
 *        command or is refused with a reason, and a state answer keeps every key, null where nothing was observed.
 */

#include <gtest/gtest.h>

#include "core/dev_command.h"

#include <nlohmann/json.hpp>

namespace {

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

TEST(DevCommandParse, RefusesWithAReason)
{
    // A refused line is never silently dropped: it has no command and says why.
    for (auto const* line : {"", "   ", "frobnicate", "surface", "surface show", "surface show nowhere", "surface toggle settings", "state now", "wait settings", "wait visible:nowhere", "wait later:settings"}) {
        auto const parsed = parseCommand(line);
        EXPECT_FALSE(parsed.command) << line;
        EXPECT_FALSE(parsed.error.empty()) << line;
    }
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
