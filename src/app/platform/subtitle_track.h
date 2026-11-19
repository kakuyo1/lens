#pragma once

/**
 * @file subtitle_track.h
 * @brief Follows the subtitle PotPlayer is showing: polls its clock, picks the cue at that
 *        position from the media's subtitle, and places the cue's words where the player draws
 *        them, on a worker thread of its own.
 *
 * The choice of file, track and cue is the core subtitle module's (core/subtitle/); this shell
 * owns the clock, the caption layout and the frames. A file swapped inside the player keeps the
 * window, and with it the scan session's context, so the track watches the title and swaps its
 * cue set itself instead of waiting for the session to notice.
 *
 * @note The deliver callback runs on the worker thread; it must only hand the frame across.
 */

#include <QRect>

#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

#include "capture/track_core.h"

namespace lens::app {

class SubtitleTrack {
public:
    using Deliver = std::function<void(capture::TrackFrame)>;

    /// @brief Starts following the subtitles in PotPlayer's window @p window, shown inside @p band
    ///        (physical screen pixels).
    SubtitleTrack(quintptr window, QRect band, Deliver deliver);
    /// @brief Stops the worker; a step in progress finishes first.
    ~SubtitleTrack();

    SubtitleTrack(SubtitleTrack const&)            = delete;
    SubtitleTrack& operator=(SubtitleTrack const&) = delete;

    /// @brief The visible area moved or resized; the next emission places the words again.
    void setBand(QRect band);

private:
    void run(std::stop_token const& stop);

    quintptr     window_;
    Deliver      deliver_;
    std::mutex   guard_;
    QRect        band_;
    std::jthread worker_;
};

}
