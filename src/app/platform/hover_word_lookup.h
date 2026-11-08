#pragma once

/**
 * @file hover_word_lookup.h
 * @brief Reads the word under the pointer from the text the window under it exposes to UI Automation,
 *        and follows a page's words while it scrolls.
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

#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "capture/track_core.h"

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

/**
 * @brief Follows a browser page's words while it scrolls, on a worker thread of its own.
 *
 * The policy -- page acceptance, the anchor, the reading cadences -- is capture::TrackCore's; this
 * shell owns only the thread, the band the duty sets, and the delivery of the frames (docs/adr/0164).
 * A track that loses its page reports so in the frame it delivers.
 *
 * @note The deliver callback runs on the worker thread; it must only hand the frame to the caller's thread.
 */
class PageTrack {
public:
    using Deliver = std::function<void(capture::TrackFrame)>;

    /// @brief Starts tracking @p window's page inside @p band, which is in physical screen pixels.
    PageTrack(quintptr window, QRect band, Deliver deliver);
    /// @brief Stops the worker; a step in progress finishes first.
    ~PageTrack();

    PageTrack(PageTrack const&)            = delete;
    PageTrack& operator=(PageTrack const&) = delete;

    /// @brief The visible area moved or resized; the next step places words inside it.
    void setBand(QRect band);

private:
    void run(std::stop_token stop);

    quintptr     window_;
    Deliver      deliver_;
    std::mutex   guard_;
    QRect        band_;
    std::jthread worker_;
};

/**
 * @brief Ask a window for its whole UI Automation tree, which makes a browser build its web content.
 * @note Blocks for the query; call it off the main thread.
 */
void warmPageTreeOf(quintptr window);

}
