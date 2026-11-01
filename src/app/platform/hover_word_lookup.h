#pragma once

/**
 * @file hover_word_lookup.h
 * @brief Reads the word under the pointer from the text the window under it exposes to UI Automation.
 *
 * Nothing is clicked, invoked or copied: the lookup only reads. A link is read through its own text
 * range, so the word under the pointer comes back, not the whole headline. The text provider is the
 * page's document or the element itself, which is what makes this work where RangeFromPoint does not:
 * the point-to-range call returns the wrong place in Chromium, so it is not used.
 *
 * @note Windows only, like the rest of src/app. Each call is a few cross-process round trips and
 *       the window it asks may be busy or gone, so it runs on a worker thread, never the hook's
 *       or the main one.
 */

#include <QPoint>
#include <QRect>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace lens::app {

/// @brief One word read under the pointer.
struct HoverWord {
    QString text; ///< The word as the page holds it; the caller decides whether it is a word.
    QRect   box;  ///< Its box in physical screen pixels, the coordinates the mouse hook reports.
};

/**
 * @brief The word under @p at, when the window there may be looked up in.
 *
 * Refused, with nothing returned, when the window under the point belongs to this process, when its
 * executable is not in @p whitelist, when the element under the point is a password field, or when no
 * text provider within reach holds the point.
 *
 * @param at        Pointer position in physical screen pixels.
 * @param whitelist Executable names allowed, lowercase, as the settings store them.
 * @return The word and its box, or nothing.
 */
std::optional<HoverWord> hoverWordAt(QPoint at, std::vector<std::string> const& whitelist);

/**
 * @brief Where the pointer is, in physical screen pixels.
 * @note Not QCursor::pos(): Qt reports that in device-independent pixels, which at 125% scaling is
 *       0.8 of the position UI Automation and WindowFromPoint take, so every lookup lands elsewhere.
 */
QPoint hoverCursorPos();

}
