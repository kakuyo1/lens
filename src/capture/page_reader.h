/** @file page_reader.h
 * @brief The seam the scan's track reads through: a window's page as a handle with words on it.
 *
 * Everything here is what the reader of the moment answers -- UI Automation in production
 * (src/app/platform/page_reader_uia.h), a scripted fake in the tests. The policy that decides when
 * to ask what lives in TrackCore (track_core.h); everything that knows a particular reader's
 * mechanics -- page-tree search, caching, the character walks -- lives behind this header.
 *
 * @note Pure std: the geometry type is Band from word_track.h, and no failure mode crosses this
 *       seam as anything but "cannot answer" -- a null handle, an empty string, an absent box.
 */
#pragma once

#include "capture/word_track.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace lens::capture {

/// @brief One word of a page: an opaque handle the adapter owns, good for asking where the word is
///        and for stepping along the reading order. Copying one copies the handle, not the word.
class Word {
public:
    virtual ~Word() = default;
};

/// @brief One read of a word: the text as the page spells it, untrimmed, and the boxes that say
///        where it sits.
/// @note The trimming policy -- which trailing punctuation to leave out, and how to cut the box to
///       the letters -- is the reader's caller's (capture::trimmedWord); the adapter only says where
///       the letters end when its mechanics can read that (docs/adr/0164).
struct WordRead {
    std::string         text;       ///< The word unit's spelling, trailing punctuation included, UTF-8.
    Band                fullBox{};  ///< The word unit's first box.
    std::optional<Band> lettersEnd; ///< The first trailing character's box -- its left edge is where the letters end -- when the reader read one; empty when the letters occupy nothing at all; nothing when the reader could not say.
};

/// @brief One page of a window, as the track reads it. Single-threaded: every call comes from the
///        track's own thread, in its own order, while the reader that issued the page lives.
class Page {
public:
    virtual ~Page() = default;

    /// @return Whether the page is still one of its window's documents; false when the tree cannot say.
    virtual bool present() const = 0;
    /// @return The page's box as it stands now, in physical screen pixels, clipped to its frames; empty when the element cannot say.
    virtual Band box() const = 0;
    /// @return The whole document text (the names are read here; docs/adr/0161).
    virtual std::string text() const = 0;
    /// @return The word at the point, expanded to a word unit; nothing when the page answers none there.
    virtual std::shared_ptr<Word> wordAt(int x, int y) = 0;
    /// @return The word's first box; nothing when it has none.
    virtual std::optional<Band> box(Word const& word) = 0;
    /// @return The word's text and boxes, untrimmed (see WordRead); nothing when the word reads no box.
    virtual std::optional<WordRead> read(Word const& word) = 0;
    /// @return Whether the word is the topmost content at @p box: false when another control covers
    ///         the word's place on screen. A reader with no hit-test mechanics answers true -- it
    ///         sees no overlays, and a place it cannot judge keeps its line.
    virtual bool visible(Word const&, Band const&)
    {
        return true;
    }
    /// @return What one occlusion hit-test costs this reader lately; zero when it has no mechanics.
    ///         The track skips the sight pass on readers whose hit-test is too expensive to pay per
    ///         place: a PDF viewer's point query costs tens of milliseconds (docs/adr/0193).
    virtual std::chrono::microseconds sightCost() const
    {
        return {};
    }
    /// @brief One word along the reading order.
    /// @param delta +1 or -1.
    /// @param awaiting Set when the move left the laid-out text -- a PDF page below the viewport
    ///        whose words have no boxes yet, say. The step answers nothing, but this is not the
    ///        end: the walk stops here for the moment and asks again once the page may have been
    ///        laid out (docs/adr/0199).
    /// @return The word there; nothing at either end of the document, when the walk fails, or
    ///         while it awaits layout.
    virtual std::shared_ptr<Word> step(Word const& word, int delta, bool* awaiting = nullptr) = 0;
};

/// @brief Where a track's pages come from. The implementor caches; drop() forgets the cache.
class PageReader {
public:
    virtual ~PageReader() = default;

    /// @brief The page to read: cached while it is the one on screen, else a fresh search. Nothing
    ///        while the window answers with none.
    /// @param band The scan band, in physical screen pixels; a search prefers the pages it holds.
    virtual std::shared_ptr<Page> acquire(Band const& band) = 0;
    /// @brief Forget the cached page; the next acquire searches anew.
    virtual void drop() = 0;
    /// @brief The largest document of the window, read fresh, with no page held: the names' fallback
    ///        before the first acquire (docs/adr/0161).
    virtual std::string anyText() = 0;
};

}
