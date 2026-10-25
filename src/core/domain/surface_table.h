#pragma once

/**
 * @file surface_table.h
 * @brief Every surface the application raises -- a panel, a transient card, the action bar, the
 *        pet's sign -- one row each, as every table that lists windows reads it.
 *
 * One set of windows was listed four times by hand -- the glass sheet set (glass_backdrop.cpp),
 * the dev surface's reported names (dev_command.h), its opener table (dev_surface.cpp) and the
 * surface test's sheet map (tst_phase2e.qml) -- and the copies had drifted: a name wrong in the
 * glass list resolves to nullptr at GlassBackdrop::surface() in silence. The rows here are what
 * those lists read instead (docs/adr/0084).
 *
 * A row's flags say which subsystem lists the window:
 *   - @c sheet: GlassBackdrop owns a GlassSheet for it, so `image://glass/<name>` resolves.
 *   - @c dev:   the dev surface's `state` answer lists it (docs/adr/0068).
 *   - @c open:  the Main.qml method that raises it for a dev `surface show` command; a surface a
 *               flow raises rather than a click carries none.
 *   - @c route: the window method that walks a dev `surface route` target into the panel's own
 *               pages; a surface with no internal navigation carries none (docs/adr/0103).
 *
 * Deliberately Qt-free: the dev grammar is core, and core has no Qt. What a surface looks like --
 * its placement, its shadow margin, the protocol every surface hand-implements -- is not here; this
 * is only the set of windows and which tables name them. The wider protocol is candidate 1's next
 * step, and it hangs off this table.
 *
 * The QML side keeps the matching rule: a surface's root window carries `objectName`, spelled
 * exactly as the row's @c name (docs/QML.md). A unit test cannot read QML, so the surface test
 * (tst_phase2e.qml) is what asserts the ones it exercises draw from the sheet their row names.
 */

#include <array>
#include <cstddef>
#include <iterator>
#include <string_view>

namespace lens::core::surface {

/// @brief One surface, as every table that lists windows reads it.
struct Row {
    std::string_view name;      ///< The window's objectName in QML, and the image://glass/<name> key.
    bool             sheet;     ///< GlassBackdrop owns a sheet: image://glass/<name> resolves to it.
    bool             dev;       ///< The dev surface's `state` answer lists it.
    char const*      open;      ///< The Main.qml method a dev `surface show` calls, or nullptr.
    bool             takesFrom; ///< That method takes the `from` element a click came from.
    int              openArity; ///< That method's parameter count: the dev opener passes exactly this many.
    char const*      route;     ///< The window method a dev `surface route` target goes to, or nullptr.
};

/**
 * @brief The table, in the order the glass backdrop builds its sheets and the dev surface reports
 *        the dev rows.
 *
 * The five preview cards come last because they are glass-only: a separate window holds one
 * backdrop (docs/adr/0041), so each takes a sheet, while the dev surface does not report them.
 */
inline constexpr Row kRows[]{
    // The panels and the action surfaces: a sheet each, dev-reported, and most raised by a click the
    // dev surface can replay. The settings panel is also the one that routes: its catalog and sub-pages
    // are pages of its own (docs/adr/0103).
    {"settings", true, true, "showSettings", true, 1, "routeTo"},
    {"tray", true, true, "showTrayMenu", false, 0, nullptr},
    {"stats", true, true, "showStats", true, 1, nullptr},
    {"words", true, true, "showWords", true, 2, nullptr},
    {"yearWords", true, true, "showYearWords", true, 1, nullptr},
    {"cost", true, true, "showCost", true, 1, nullptr},
    // Raised by a flow rather than a click, so no dev opener; still a sheet and dev-reported.
    {"bubble", true, true, nullptr, false, 0, nullptr},
    {"selectionBar", true, true, nullptr, false, 0, nullptr},
    {"notice", true, true, nullptr, false, 0, nullptr},
    {"update", true, true, nullptr, false, 0, nullptr},
    {"removal", true, true, nullptr, false, 0, nullptr},
    // The review sign: the dev surface reports it, but the review flow raises it and it holds no
    // sheet of its own -- its answer card is a Bubble, which has one (docs/adr/0067).
    {"reviewSign", false, true, nullptr, false, 0, nullptr},
    // The preview cards: glass-only. The settings preview's card is a second Bubble window; the
    // theme, level and action-bar dropdowns are transient cards beside their panel; the refresh
    // tier's note is a fifth. Each is its own window, so each takes its own sheet (docs/adr/0041).
    {"preview", true, false, nullptr, false, 0, nullptr},
    {"themePreview", true, false, nullptr, false, 0, nullptr},
    {"vocabPreview", true, false, nullptr, false, 0, nullptr},
    {"selectionBarPreview", true, false, nullptr, false, 0, nullptr},
    {"notePreview", true, false, nullptr, false, 0, nullptr},
};

/**
 * @brief Find the row a window's name names.
 * @param name A surface name: an objectName, or an image://glass/<name> key.
 * @return The row, or nullptr when no surface carries the name.
 */
constexpr Row const* find(std::string_view const name)
{
    for (Row const& row : kRows)
        if (row.name == name)
            return &row;
    return nullptr;
}

/// @return How many surfaces own a glass sheet.
constexpr std::size_t sheetCount()
{
    std::size_t count = 0;
    for (Row const& row : kRows)
        if (row.sheet)
            ++count;
    return count;
}

/// @return How many surfaces the dev surface reports.
constexpr std::size_t devCount()
{
    std::size_t count = 0;
    for (Row const& row : kRows)
        if (row.dev)
            ++count;
    return count;
}

/// @return The names the dev surface reports, in table order.
constexpr std::array<std::string_view, devCount()> devNames()
{
    std::array<std::string_view, devCount()> names{};
    std::size_t                              index = 0;
    for (Row const& row : kRows)
        if (row.dev)
            names[index++] = row.name;
    return names;
}

/// @brief What a table is allowed to look like, checked at compile time below.
constexpr bool tableIsSound()
{
    for (std::size_t index = 0; index < std::size(kRows); ++index) {
        Row const& row = kRows[index];
        if (row.name.empty())
            return false;
        // A dev opener only makes sense for a window the dev surface can find and report.
        if (row.open != nullptr and not row.dev)
            return false;
        // A route method is the same kind of dev entry as an opener, so it lives by the same rule.
        if (row.route != nullptr and not row.dev)
            return false;
        // `takesFrom` qualifies an opener; without one it says nothing. An opener's arity names
        // real parameters of the method it calls -- the dev surface invokes with exactly that many
        // arguments, and a count that disagrees with the method is a `surface show` that answers
        // "not callable" for a surface the table claims to open.
        if (row.takesFrom and row.open == nullptr)
            return false;
        if (row.open != nullptr and (row.openArity < 0 or row.openArity > 2))
            return false;
        if (row.takesFrom and row.openArity < 1)
            return false;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
            if (kRows[earlier].name == row.name)
                return false; // one window, one row
    }
    return true;
}

static_assert(tableIsSound(), "every surface row has to agree with itself: one name, and an opener only on a reported window");

} // namespace lens::core::surface
