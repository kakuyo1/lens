/** @file track_core.h
 * @brief The scan's acquisition policy, carried by one object stepped once every ~16 ms: which page
 *        to keep, where the anchor goes, which words are read when, and what one step reports.
 *
 * Pure std: no Qt, no COM, no clock of its own -- the caller hands in the time. Everything about
 * HOW a page is read (tree search, caching, word-unit quirks) lives behind the PageReader seam
 * (page_reader.h); everything about WHEN and WHETHER belongs here. Verdicts are not this module's
 * business: the frames it sends are judged by the duty that owns the scan (docs/adr/0164).
 */
#pragma once

#include "capture/page_reader.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace lens::capture {

/// Words one track step reads at most while the anchor holds still; short steps keep the anchor read every frame or two.
constexpr int kTrackChunkWords = 12;
/// Steps the anchor must hold still before the page's names are re-read; the read costs a few hundred ms.
constexpr int kStillSteps = 8;
/// Steps the anchor must hold still before its words are drawn: a scroll in flight can show an old place for a step.
constexpr int kSettleSteps = 3;
/// Steps the anchor may read empty or outside the band before the track gives it up; a read that races a scroll is brief.
constexpr int kMissedSteps = 6;
/// Empty anchor words read in a row before the page is dropped and searched for anew.
constexpr int kEmptyAnchors = 3;
/// The anchor moves once it comes within this share of the band's height of the band's top or bottom edge.
constexpr int kEdgeShare = 5;
/// How often the names of the page are re-read once they are known; the page's whole text costs about 300 ms.
constexpr std::chrono::seconds kNameRefresh{5};
/// The wait before the names are asked for again while they are still unknown.
constexpr std::chrono::seconds kNameRetry{1};
/// How often a settled page is checked to still be a document of its window: a tab switch takes it out of the tree,
/// and its last box can keep reading as in view (docs/adr/0163).
constexpr std::chrono::milliseconds kPageCheckPeriod{500};
/// The wait between fresh page searches while the window answers with none; a search walks the window's whole tree.
constexpr std::chrono::milliseconds kPageSearchPeriod{250};

/// @brief A word the track read off the page, first occurrence only.
struct ReadWord {
    int         id = 0;
    std::string text; ///< The letters as the scan spells them, trailing punctuation left out, UTF-8.
};

/// @brief What one step of a track reports. A step that changes nothing reports nothing.
struct TrackFrame {
    std::vector<PlacedSpan> visible;      ///< The words inside the band, where they are now. Sent when the page moved or words were read.
    std::vector<ReadWord>   added;        ///< Words read in this step, with names left out.
    bool                    lost = false; ///< The track lost its page: every id it handed out is void, and the caller clears its caches.
};

/// @brief The track's diagnostic counters. The shell logs the 1 s summary; @p note carries a page
///        refusal's own line (docs/adr/0163), logged on the step it happens.
struct TrackReport {
    int         moves         = 0; ///< Anchor moves.
    int         refusals      = 0; ///< Anchor moves the page refused.
    int         losses        = 0;
    int         missedReads   = 0; ///< Anchor reads that came back empty or outside the band.
    int         framesSent    = 0;
    int         nameReads     = 0;
    int         pageSearches  = 0;
    int         spanCount     = 0;
    int         visibleTop    = 0;
    int         visibleBottom = 0;
    std::string note; ///< A page refusal, e.g. "refused: it answers 77x854 in a 1921x854 band"; empty otherwise.
};

/// @brief One step's output.
struct TrackOutput {
    std::optional<TrackFrame>  frame;
    std::optional<TrackReport> report;
};

/// @brief Follows a browser page's words while it scrolls.
///
/// One word is the anchor: its box is read every step, and every other word sits at its offset from
/// the anchor, so the words move with the page during a scroll. Words are read past the anchor in
/// short steps, and only once the anchor has held still, so no read straddles a scroll. When the
/// anchor nears an edge of the band it moves to a word near the middle, so it stays readable. A
/// page is kept only while it fills the band and still belongs to its window; a track that loses
/// its page reports so.
///
/// @note One instance is stepped by one thread, in order, with @p now monotonic; the reader it is
///       built on outlives it. The id an added word carries holds between lost frames.
class TrackCore {
public:
    /// @param reader The pages the track reads through; outlives *this.
    explicit TrackCore(PageReader& reader);

    /// @brief One step, on the caller's cadence (~16 ms).
    /// @param band The scan area, in physical screen pixels; it may change between steps.
    /// @param now The caller's steady clock.
    /// @return What changed; a frame and a report are independent, and either may be absent.
    TrackOutput step(Band band, std::chrono::steady_clock::time_point now);

private:
    std::optional<TrackFrame> acquire(Band const& band, std::chrono::steady_clock::time_point now);
    std::optional<TrackFrame> trackStep(Band const& band, Band const& visible, std::chrono::steady_clock::time_point now);
    std::optional<TrackFrame> lose();
    /// @return Whether the anchor moved to a word nearer the middle of @p band.
    bool moveAnchorIfNearEdge(Band const& band);
    /// @return Whether words past the last one read forward were added.
    bool extendForward(Band const& band, TrackFrame& frame);
    /// @return Whether words past the first one read backward were added.
    bool extendBackward(Band const& band, TrackFrame& frame);
    void refreshNamesIfDue(std::chrono::steady_clock::time_point now);
    /// @brief Send a word the track has not sent before: names and repeats draw no line of their own.
    void rememberWord(int id, std::string const& text, std::vector<ReadWord>& added);

    PageReader&                           reader_;
    std::shared_ptr<Page>                 page_;
    std::shared_ptr<Word>                 anchor_;
    Band                                  anchorBox_{};
    std::vector<TrackedSpan>              spans_; ///< In reading order, so the words past either end are the next to read.
    std::shared_ptr<Word>                 forward_;
    std::shared_ptr<Word>                 backward_;
    bool                                  forwardDone_  = false;
    bool                                  backwardDone_ = false;
    std::unordered_set<std::string>       names_;
    std::unordered_set<std::string>       wordsSeen_; ///< The words a line already stands for, once each.
    bool                                  namesLoaded_ = false;
    std::chrono::steady_clock::time_point namesAt_{};
    int                                   stillSteps_ = 0;
    int                                   nextId_     = 0;
    // Diagnostic counters for the track, one summary line a second: a track that has stopped drawing
    // its lines shows which of its reads stopped, instead of going silent.
    int                                   moves_    = 0;
    int                                   refusals_ = 0;
    int                                   losses_   = 0;
    int                                   missed_   = 0;
    int                                   reads_    = 0; ///< Name reads the page answered in the last second.
    int                                   searches_ = 0; ///< Page searches walked over the window's tree.
    std::chrono::steady_clock::time_point reportedAt_{};
    int                                   missedSteps_  = 0;  ///< Steps in a row the anchor has read empty or outside the band.
    int                                   framesSent_   = 0;  ///< Frames returned in the last second.
    int                                   emptyAnchors_ = 0;  ///< Anchor words read empty in a row.
    std::chrono::steady_clock::time_point pageSearchAt_{};    ///< When the page was last searched for.
    bool                                  cacheWarm_ = false; ///< Whether the reader is believed to hold a page for this window; a search is asked for only on the slow clock while it does not.
    bool                                  lines_     = false; ///< Whether the last frame sent drew its words.
    std::chrono::steady_clock::time_point pageCheckAt_{};
    std::string                           note_; ///< A page refusal to report with this step's output, if any.
};

}
