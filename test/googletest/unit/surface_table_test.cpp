/**
 * @file surface_table_test.cpp
 * @brief Offline tests for the one surface table (core/domain/surface_table.h).
 *
 * These tests hold the windows' topology as the one table declares it, so a row mis-flagged fails
 * here rather than resolving to nullptr at GlassBackdrop::surface() in silence (docs/adr/0084).
 */

#include <gtest/gtest.h>

#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/domain/surface_table.h"

using lens::core::surface::devCount;
using lens::core::surface::devNames;
using lens::core::surface::find;
using lens::core::surface::kRows;
using lens::core::surface::Row;
using lens::core::surface::sheetCount;

TEST(SurfaceTable, EveryNameIsUniqueAndResolvable)
{
    std::set<std::string_view> seen;
    for (Row const& row : kRows) {
        EXPECT_FALSE(row.name.empty());
        EXPECT_TRUE(seen.insert(row.name).second) << "duplicate surface name: " << std::string(row.name);
        // The anti-silent-nullptr guarantee: whatever a row flags as owning a sheet, find() resolves
        // it, so GlassBackdrop::surface(image://glass/<name>) never misses a sheet the table promised.
        EXPECT_EQ(find(row.name), &row);
    }
    EXPECT_EQ(find("noSuchSurface"), nullptr);
}

TEST(SurfaceTable, TheDevListIsExactlyTheRowsFlaggedDev)
{
    auto const names = devNames();
    ASSERT_EQ(names.size(), devCount());
    std::size_t index = 0;
    for (Row const& row : kRows) {
        if (not row.dev)
            continue;
        ASSERT_LT(index, names.size());
        EXPECT_EQ(names[index], row.name);
        ++index;
    }
    EXPECT_EQ(index, names.size());
}

TEST(SurfaceTable, ReviewSignIsTheOnlyReportedSurfaceWithoutASheet)
{
    // Its answer card is a Bubble, which owns the sheet; the sign itself is a window the dev surface
    // reports but that holds no glass of its own (docs/adr/0067).
    std::vector<std::string_view> reportedWithoutSheet;
    for (Row const& row : kRows)
        if (row.dev and not row.sheet)
            reportedWithoutSheet.push_back(row.name);
    ASSERT_EQ(reportedWithoutSheet.size(), 1u);
    EXPECT_EQ(reportedWithoutSheet.front(), "reviewSign");
}

TEST(SurfaceTable, ThePreviewCardsAreTheOnlySheetsTheDevSurfaceDoesNotReport)
{
    std::vector<std::string_view> sheetNotReported;
    for (Row const& row : kRows)
        if (row.sheet and not row.dev)
            sheetNotReported.push_back(row.name);
    // Exactly the five preview cards and nothing else: a separate window holds one backdrop
    // (docs/adr/0041), so each takes a sheet, while the dev surface reports none of them.
    ASSERT_EQ(sheetNotReported.size(), 5u);
    EXPECT_EQ(sheetNotReported[0], "preview");
    EXPECT_EQ(sheetNotReported[1], "themePreview");
    EXPECT_EQ(sheetNotReported[2], "vocabPreview");
    EXPECT_EQ(sheetNotReported[3], "selectionBarPreview");
    EXPECT_EQ(sheetNotReported[4], "notePreview");
}

TEST(SurfaceTable, AnOpenerIsAShowMethodOnAReportedSurface)
{
    for (Row const& row : kRows) {
        if (row.open == nullptr) {
            EXPECT_FALSE(row.takesFrom) << std::string(row.name) << " takes `from` but has no opener";
            EXPECT_EQ(row.openArity, 0) << std::string(row.name) << " carries an arity with no opener";
            continue;
        }
        EXPECT_TRUE(row.dev) << std::string(row.name) << " is openable but the dev surface does not report it";
        std::string_view const method = row.open;
        EXPECT_EQ(method.find("show"), 0u) << std::string(method) << " is not a show method";
        if (row.takesFrom)
            EXPECT_GE(row.openArity, 1) << std::string(row.name) << " takes `from` but its arity leaves no room for it";
    }
}

/// The arity names real parameters of the method the opener calls, and the dev surface invokes with
/// exactly that many arguments -- a count one short answers "not callable", which is how `surface
/// show words` went missing until the column existed. This pins the three shapes the openers have.
TEST(SurfaceTable, TheOpenerAritiesNameTheMethodsRealParameters)
{
    for (auto const& [name, expected] : std::initializer_list<std::pair<std::string_view, int>>{
             {"settings", 1}, {"tray", 0}, {"stats", 1}, {"words", 2}, {"yearWords", 1}, {"cost", 1}}) {
        Row const* const row = find(name);
        ASSERT_NE(row, nullptr) << std::string(name);
        EXPECT_EQ(row->openArity, expected) << std::string(name) << " changed its opener's shape";
    }
}

TEST(SurfaceTable, SheetCountMatchesTheSheetRows)
{
    std::size_t counted = 0;
    for (Row const& row : kRows)
        if (row.sheet)
            ++counted;
    EXPECT_EQ(sheetCount(), counted);
}
