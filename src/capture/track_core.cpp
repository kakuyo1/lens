/** @file track_core.cpp
 * @brief The scan's acquisition policy, for track_core.h.
 */
#include "track_core.h"

#include <algorithm>
#include <utility>

#include "page_alignment.h"
#include "word_track.h"

namespace lens::capture {
namespace {

/// @brief The part of @p box that lies in @p band; empty when they do not overlap.
Band intersected(Band const& band, Band const& box)
{
    int const left   = (std::max)(band.x, box.x);
    int const right  = (std::min)(band.x + band.width, box.x + box.width);
    int const top    = (std::max)(band.y, box.y);
    int const bottom = (std::min)(band.y + band.height, box.y + box.height);
    return {.x = left, .y = top, .width = (std::max)(0, right - left), .height = (std::max)(0, bottom - top)};
}

/// @brief The centre coordinate the way QRect::center() takes it -- (first + last) / 2, biased one
///        pixel toward the top-left on even dimensions -- which is the arithmetic the gates and the
///        probes below always ran on.
int centerOf(int const first, int const length)
{
    return (2 * first + length - 1) / 2;
}

/// @brief Whether @p band holds the centre of @p box, the rule a placed word is drawn by.
bool holdsCenter(Band const& band, Band const& box)
{
    int const cx = centerOf(box.x, box.width);
    int const cy = centerOf(box.y, box.height);
    return cx >= band.x and cx < band.x + band.width and cy >= band.y and cy < band.y + band.height;
}

/// @brief Whether @p box has no extent, the way an absent box reads.
bool emptyOf(Band const& box)
{
    return box.width <= 0 or box.height <= 0;
}

}

TrackCore::TrackCore(PageReader& reader)
    : reader_{reader}
{}

TrackOutput TrackCore::step(Band band, std::chrono::steady_clock::time_point const now)
{
    // The window's box takes in the browser's tabs, address bar and bookmarks, which are not the page: the page's own box clips.
    Band const visible = page_ ? intersected(band, page_->box()) : band;

    // The summary reports while the anchor is down too: a track stuck in acquire is exactly the one
    // that stops drawing its lines, and its name reads and page searches are the only sign of it.
    // The shell logs it; here it is only gathered.
    TrackReport report;
    bool        summary = false;
    if (reportedAt_ == std::chrono::steady_clock::time_point{}) reportedAt_ = now;
    if (now - reportedAt_ >= std::chrono::seconds{1}) {
        summary              = moves_ + refusals_ + losses_ + missed_ + framesSent_ + reads_ + searches_ > 0;
        report.moves         = moves_;
        report.refusals      = refusals_;
        report.losses        = losses_;
        report.missedReads   = missed_;
        report.framesSent    = framesSent_;
        report.nameReads     = reads_;
        report.pageSearches  = searches_;
        report.spanCount     = static_cast<int>(spans_.size());
        report.visibleTop    = visible.y;
        report.visibleBottom = visible.y + visible.height - 1;
        moves_ = refusals_ = losses_ = missed_ = framesSent_ = reads_ = searches_ = 0;
        reportedAt_                                                               = now;
    }

    auto frame = not anchor_ ? acquire(band, now) : trackStep(band, visible, now);

    TrackOutput out;
    out.frame = std::move(frame);
    if (summary or not note_.empty()) {
        report.note = std::move(note_);
        note_.clear();
        out.report = std::move(report);
    }
    return out;
}

std::optional<TrackFrame> TrackCore::trackStep(Band const& band, Band const& visible, std::chrono::steady_clock::time_point const now)
{
    auto const box = page_->box(*anchor_);
    if (not box or emptyOf(*box) or not holdsCenter(visible, *box)) {
        // A read that races a scroll comes back empty for a step or two: the lines hide until the page reads again.
        ++missed_;
        stillSteps_ = 0;
        if (++missedSteps_ < kMissedSteps) {
            if (not lines_) return std::nullopt;
            lines_ = false;
            return TrackFrame{};
        }
        return lose();
    }
    missedSteps_ = 0;

    if (*box != anchorBox_) {
        anchorBox_  = *box;
        stillSteps_ = 0;
        moveAnchorIfNearEdge(visible);
    } else {
        ++stillSteps_;
    }

    bool const settled = stillSteps_ >= kSettleSteps;
    if (settled and now - pageCheckAt_ >= kPageCheckPeriod) {
        pageCheckAt_ = now;
        if (not page_->present()) {
            reader_.drop();
            page_.reset();
            cacheWarm_ = false;
            return lose();
        }
        // A page kept from an old search can also be a background tab's sliver: its own reads keep
        // answering, so nothing above catches it, and the article would starve for as long as the
        // sliver's tab stays open. The band says what the reader sees (docs/adr/0163).
        if (not fillsTheBand(visible, band)) {
            note_ = "dropped: it answers " + std::to_string(visible.width) + "x" + std::to_string(visible.height) +
                    " in a " + std::to_string(band.width) + "x" + std::to_string(band.height) + " band";
            reader_.drop();
            page_.reset();
            cacheWarm_ = false;
            return lose();
        }
    }

    TrackFrame frame;
    // Words are read while the page moves too: a chunk is kept only when the anchor did not move during it.
    // The names are re-read only once the page holds still, since that read is a few hundred ms of COM.
    if (stillSteps_ >= kStillSteps) refreshNamesIfDue(now);
    bool const extended = extendForward(visible, frame) or extendBackward(visible, frame);
    // Words are drawn only once the anchor holds still: a placement made while the page moves sits off its words.
    if (not extended and settled == lines_) return std::nullopt;
    lines_ = settled;
    if (settled) frame.visible = placeSpans(spans_, anchorBox_.x, anchorBox_.y, visible);
    ++framesSent_;
    return frame;
}

bool TrackCore::moveAnchorIfNearEdge(Band const& band)
{
    int const margin = band.height / kEdgeShare;
    int const centre = centerOf(anchorBox_.y, anchorBox_.height);
    if (centre - band.y >= margin and band.y + band.height - 1 - centre >= margin) return false;
    if (emptyOf(band)) return false;
    auto middle = page_->wordAt(centerOf(band.x, band.width), centerOf(band.y, band.height));
    if (not middle) {
        ++refusals_;
        return false;
    }
    auto const box = page_->box(*middle);
    // The old anchor must not have moved while the new one was found: both boxes are taken at one scroll position.
    auto const anchorBox = page_->box(*anchor_);
    if (not box or emptyOf(*box) or not anchorBox or *anchorBox != anchorBox_) {
        ++refusals_;
        return false;
    }
    rebaseSpans(spans_, box->x - anchorBox_.x, box->y - anchorBox_.y);
    anchor_    = std::move(middle);
    anchorBox_ = *box;
    ++moves_;
    return true;
}

bool TrackCore::extendForward(Band const& band, TrackFrame& frame)
{
    if (forwardDone_ or not forward_ or spans_.empty()) return false;
    auto const& last = spans_.back();
    if (anchorBox_.y + last.dy + last.height >= band.y + band.height - 1) return false;

    auto                     cursor = forward_;
    std::vector<TrackedSpan> fresh;
    std::vector<ReadWord>    words;
    bool                     ended = false;
    for (int walked = 0; walked < kTrackChunkWords; ++walked) {
        auto next = page_->step(*cursor, 1);
        if (not next) {
            ended = true;
            break;
        }
        auto const box = page_->box(*next);
        if (box and not emptyOf(*box)) {
            if (box->y > band.y + band.height - 1) break;
            auto const read = page_->read(*next);
            auto const word = read ? trimmedWord(read->text, read->fullBox, read->lettersEnd) : std::nullopt;
            if (word) {
                fresh.push_back({.id     = nextId_++,
                                 .dx     = box->x - anchorBox_.x,
                                 .dy     = box->y - anchorBox_.y,
                                 .width  = word->box.width,
                                 .height = word->box.height});
                rememberWord(fresh.back().id, word->text, words);
            }
        }
        cursor = std::move(next);
    }
    // The anchor must not have moved during the read: these words would then sit at two scroll positions.
    auto const anchorBox = page_->box(*anchor_);
    if (not anchorBox or *anchorBox != anchorBox_) return false;
    spans_.insert(spans_.end(), fresh.begin(), fresh.end());
    forward_ = std::move(cursor);
    if (ended) forwardDone_ = true;
    frame.added.insert(frame.added.end(), words.begin(), words.end());
    return not fresh.empty();
}

bool TrackCore::extendBackward(Band const& band, TrackFrame& frame)
{
    if (backwardDone_ or not backward_ or spans_.empty()) return false;
    auto const& first = spans_.front();
    if (anchorBox_.y + first.dy <= band.y) return false;

    auto                     cursor = backward_;
    std::vector<TrackedSpan> fresh; // nearest first
    std::vector<ReadWord>    words;
    bool                     ended = false;
    for (int walked = 0; walked < kTrackChunkWords; ++walked) {
        auto next = page_->step(*cursor, -1);
        if (not next) {
            ended = true;
            break;
        }
        auto const box = page_->box(*next);
        if (box and not emptyOf(*box)) {
            if (box->y + box->height - 1 < band.y) break;
            auto const read = page_->read(*next);
            auto const word = read ? trimmedWord(read->text, read->fullBox, read->lettersEnd) : std::nullopt;
            if (word) {
                fresh.push_back({.id     = nextId_++,
                                 .dx     = box->x - anchorBox_.x,
                                 .dy     = box->y - anchorBox_.y,
                                 .width  = word->box.width,
                                 .height = word->box.height});
                rememberWord(fresh.back().id, word->text, words);
            }
        }
        cursor = std::move(next);
    }
    // The anchor must not have moved during the read: these words would then sit at two scroll positions.
    auto const anchorBox = page_->box(*anchor_);
    if (not anchorBox or *anchorBox != anchorBox_) return false;
    spans_.insert(spans_.begin(), fresh.rbegin(), fresh.rend());
    backward_ = std::move(cursor);
    if (ended) backwardDone_ = true;
    frame.added.insert(frame.added.end(), words.begin(), words.end());
    return not fresh.empty();
}

void TrackCore::refreshNamesIfDue(std::chrono::steady_clock::time_point const now)
{
    if (now - namesAt_ < (namesLoaded_ ? kNameRefresh : kNameRetry)) return;
    namesAt_ = now;
    // The names come from the page the track reads, not a fresh search: on a page of frames the
    // search can pick another document, whose names would gate the tracked page's words (docs/adr/0161).
    std::string const text = page_ ? page_->text() : reader_.anyText();
    ++reads_;
    if (text.empty()) return;
    names_       = nameSet(text);
    namesLoaded_ = true;
}

void TrackCore::rememberWord(int const id, std::string const& text, std::vector<ReadWord>& added)
{
    // Names are not words to learn: the page writes them capitalized throughout. And a word the
    // track has already sent keeps the one line it won -- the reader asked for the first
    // occurrence, not for every one the page holds (docs/adr/0161 keeps the name rule).
    std::string const key = normalizedWord(text);
    if (key.empty() or names_.contains(key)) return;
    if (not wordsSeen_.insert(key).second) return;
    added.push_back({.id = id, .text = text});
}

std::optional<TrackFrame> TrackCore::acquire(Band const& band, std::chrono::steady_clock::time_point const now)
{
    refreshNamesIfDue(now);
    // The names decide which words are words to learn, so nothing is read before they are known.
    if (not namesLoaded_) return std::nullopt;
    if (not cacheWarm_) {
        // A search walks the window's whole tree, so a window that answers with none is asked again on a slow clock.
        if (now - pageSearchAt_ < kPageSearchPeriod) return std::nullopt;
        pageSearchAt_ = now;
        ++searches_;
    }
    page_      = reader_.acquire(band); // a cache hit is cheap: asked every step without a page
    cacheWarm_ = page_ != nullptr;
    if (not page_) return std::nullopt;
    Band const visible = intersected(band, page_->box());
    // The browser rebuilds its document on a navigation, a discarded tab or a renderer swap, and the
    // cached page then answers nothing forever. A tab parked in the background answers with less: its
    // box collapses to a sliver of the window, and a search run while the real page answers nothing
    // picks that sliver -- its words then draw in its corner of the screen while the article goes
    // unwired, for as long as the tab stays open (docs/adr/0163). Either way the page is dropped, so
    // the next step searches anew.
    if (not fillsTheBand(visible, band)) {
        note_ = "refused: it answers " + std::to_string(visible.width) + "x" + std::to_string(visible.height) +
                " in a " + std::to_string(band.width) + "x" + std::to_string(band.height) + " band";
        reader_.drop();
        page_.reset();
        cacheWarm_ = false;
        return std::nullopt;
    }
    auto anchor = page_->wordAt(centerOf(visible.x, visible.width), centerOf(visible.y, visible.height));
    if (not anchor) {
        reader_.drop();
        page_.reset();
        cacheWarm_ = false;
        return std::nullopt;
    }
    auto const box = page_->box(*anchor);
    if (not box or emptyOf(*box)) {
        // A point over the page's blank ground reads no word; a few in a row mean the cached page is
        // not the one on screen, so it is dropped for a fresh search too.
        if (++emptyAnchors_ < kEmptyAnchors) return std::nullopt;
        reader_.drop();
        page_.reset();
        cacheWarm_ = false;
        return std::nullopt;
    }
    emptyAnchors_ = 0;

    anchor_         = std::move(anchor);
    anchorBox_      = *box;
    forward_        = anchor_;
    backward_       = anchor_;
    forwardDone_    = false;
    backwardDone_   = false;
    stillSteps_     = 0;
    auto const read = page_->read(*anchor_);
    auto const word = read ? trimmedWord(read->text, read->fullBox, read->lettersEnd) : std::nullopt;
    spans_.assign(1,
                  {.id     = nextId_++,
                   .dx     = 0,
                   .dy     = 0,
                   .width  = word ? word->box.width : box->width,
                   .height = word ? word->box.height : box->height});

    TrackFrame frame;
    if (word) rememberWord(spans_.front().id, word->text, frame.added);
    lines_ = false;
    return frame;
}

std::optional<TrackFrame> TrackCore::lose()
{
    ++losses_;
    missedSteps_ = 0;
    anchor_.reset();
    forward_.reset();
    backward_.reset();
    spans_.clear();
    forwardDone_  = false;
    backwardDone_ = false;
    stillSteps_   = 0;
    lines_        = false;
    // The words it sent belonged to the reading it lost; a new reading starts its first occurrences over.
    wordsSeen_.clear();
    TrackFrame frame;
    frame.lost = true;
    return frame;
}

}
