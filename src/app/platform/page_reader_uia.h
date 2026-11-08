#pragma once

/**
 * @file page_reader_uia.h
 * @brief The scan's page reader over UI Automation: the page-tree search, its cache, and the word
 *        reads, behind the capture::PageReader seam.
 *
 * Everything mechanical about reading a Chrome page lives here -- the two-pass page selection with
 * its virtual-document rule, the frame clipping, the process-wide page cache, and the word-unit
 * reads with Chrome's refusal of endpoint retraction walked around. Which page to keep and when to
 * ask for one are TrackCore's (docs/adr/0164).
 *
 * @note Windows only. Lives on the track's worker thread, inside the COM apartment its owner set
 *       up; anyTextOf() and warmTreeOf() set up their own and are safe from any thread.
 */

#include "capture/page_reader.h"

#include <memory>
#include <string>

// Forward-declared: the automation instance is created in the implementation, so windows.h stays
// out of this header.
struct IUIAutomation;

namespace lens::app::page_reader_uia {

/// @brief The pages of one window, found in its UI Automation tree and cached between reads: finding
///        a page costs a full tree walk, and a probe runs several times a second.
class UiaPageReader final : public capture::PageReader {
public:
    /// @param window The window handle whose pages are read; held for the reader's life.
    explicit UiaPageReader(quintptr window);

    std::shared_ptr<capture::Page> acquire(capture::Band const& band) override;
    void                           drop() override;
    std::string                    anyText() override;

private:
    quintptr                       window_;
    std::shared_ptr<IUIAutomation> automation_; ///< Created on the first search; the pages it found hold it too.
};

/// @brief The text of the largest document of @p window, read fresh, with no page held: the names'
///        fallback (docs/adr/0161).
std::string anyTextOf(quintptr window);

/// @brief Ask a window for its whole UI Automation tree, which makes a browser build its web content.
/// @note Blocks for the query; call it off the main thread.
void warmTreeOf(quintptr window);

}
