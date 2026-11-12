#pragma once

/**
 * @file page_reader_scintilla.h
 * @brief The scan's page reader over a Scintilla editor, behind the capture::PageReader seam.
 *
 * A Scintilla window keeps its document as UTF-8 bytes and answers a small set of messages about
 * them: the whole text, the byte position at a point, and the pixel position of a byte. Nothing
 * here goes through UI Automation or OCR. The reader is chosen by the window it is given, so the
 * track's policy (TrackCore) never learns which app it is reading.
 *
 * @note Windows only. Lives on the track's worker thread; the messages are sent with a timeout so
 *       an editor that stopped answering cannot hold the track.
 */

#include "capture/page_reader.h"

#include <QtGlobal>

#include <memory>

namespace lens::app::page_reader_scintilla {

/// @brief The pages of one window's Scintilla editors, read through their own messages.
class ScintillaPageReader final : public capture::PageReader {
public:
    /// @param window The top-level window handle whose editors are read; held for the reader's life.
    explicit ScintillaPageReader(quintptr window);
    ~ScintillaPageReader() override;

    std::shared_ptr<capture::Page> acquire(capture::Band const& band) override;
    void                           drop() override;
    std::string                    anyText() override;

private:
    quintptr                       window_;
    std::shared_ptr<capture::Page> page_;
};

/// @return A reader for @p window when it owns a Scintilla editor; nothing when it does not.
std::unique_ptr<capture::PageReader> makeReader(quintptr window);

}
