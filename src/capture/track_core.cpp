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

/// @brief Whether two placed sets carry the same words in the same order: the frame gate ignores the
///        sub-pixel jitter of the anchor's re-read box, which shifts every place by a pixel or none.
bool sameIds(std::vector<PlacedSpan> const& a, std::vector<PlacedSpan> const& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id) return false;
    return true;
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
        summary              = moves_ + refusals_ + losses_ + missed_ + framesSent_ + reads_ + searches_ + sightChecks_ > 0;
        report.moves         = moves_;
        report.refusals      = refusals_;
        report.refusalsMoved = refusalsMoved_;
        report.losses        = losses_;
        report.missedReads   = missed_;
        report.missedEmpty   = missedEmpty_;
        report.missedOut     = missedOut_;
        report.framesSent    = framesSent_;
        report.nameReads     = reads_;
        report.pageSearches  = searches_;
        report.spanCount     = static_cast<int>(spans_.size());
        report.visibleTop    = visible.y;
        report.visibleBottom = visible.y + visible.height - 1;
        report.sightChecks   = sightChecks_;
        moves_ = refusals_ = refusalsMoved_ = losses_ = missed_ = missedEmpty_ = missedOut_ = framesSent_ = reads_ = searches_ = sightChecks_ = 0;
        reportedAt_                                                                                                                           = now;
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
    chunkRaced_    = false;
    auto const box = page_->box(*anchor_);
    if (not box or emptyOf(*box) or not holdsCenter(visible, *box)) {
        // A read that races a scroll comes back empty for a step or two: the lines hide until the page reads again.
        // Split the miss for diagnosis: an absent/zero box vs a centre outside the band (docs/adr/0189).
        // Both give up at kMissedSteps -- a sustained empty is the anchor scrolled off and retired,
        // not a transient to ride out, so waiting longer only draws stale lines (docs/adr/0189).
        if (not box or emptyOf(*box))
            ++missedEmpty_;
        else
            ++missedOut_;
        ++missed_;
        stillSteps_ = 0;
        if (++missedSteps_ < kMissedSteps) {
            if (not lines_) return std::nullopt;
            lines_ = false;
            lastVisible_.clear();
            return TrackFrame{};
        }
        return lose();
    }
    missedSteps_ = 0;

    bool relayout = false;
    if (*box != anchorBox_) {
        // A word's box keeps its size while the page scrolls; a zoom (or a text reflow) resizes it,
        // and every anchor-relative offset then names a place the new layout no longer holds
        // (docs/adr/0198). The one-pixel slack leaves rounding alone.
        int const dw = box->width - anchorBox_.width;
        int const dh = box->height - anchorBox_.height;
        relayout     = dw > 1 or dw < -1 or dh > 1 or dh < -1;
        anchorBox_   = *box;
        stillSteps_  = 0;
        // A re-anchor only re-bases the spans on a new word; a scroll moves every place on screen,
        // so the verdicts stand for places nothing holds now and must be read again.
        if (not relayout and not moveAnchorIfNearEdge(visible)) invalidateSight();
    } else {
        ++stillSteps_;
    }

    bool const settled = stillSteps_ >= kSettleSteps;
    if (settled and now - pageCheckAt_ >= kPageCheckPeriod) {
        pageCheckAt_ = now;
        if (not page_->present()) {
            dropPage();
            return lose();
        }
        // A page kept from an old search can also be a background tab's sliver: its own reads keep
        // answering, so nothing above catches it, and the article would starve for as long as the
        // sliver's tab stays open. The band says what the reader sees (docs/adr/0163).
        if (not fillsTheBand(visible, band)) {
            note_ = "dropped: it answers " + std::to_string(visible.width) + "x" + std::to_string(visible.height) +
                    " in a " + std::to_string(band.width) + "x" + std::to_string(band.height) + " band";
            dropPage();
            return lose();
        }
    }

    TrackFrame frame;
    if (relayout) relayoutPage();
    // Words are read while the page moves too: a chunk is kept only when the anchor did not move during it.
    // The names are re-read only once the page holds still, since that read is a few hundred ms of COM.
    if (stillSteps_ >= kStillSteps) refreshNamesIfDue(now);
    // A non-English page is held whole: no chunk is walked and no line is drawn, but the anchor and
    // the name refresh keep running, so a page that turns English again resumes (docs/adr/0187).
    bool const draws    = settled and not foreign_;
    bool const extended = not foreign_ and (extendForward(visible, frame) or extendBackward(visible, frame));
    // Words are drawn only once the anchor holds still: a placement made while the page moves sits off its words.
    // A place another control covers draws no line: the sight pass hit-tests it and leaves it out (docs/adr/0193).
    if (draws) {
        frame.visible = placeSpans(spans_, anchorBox_.x, anchorBox_.y, visible);
        applySight(frame.visible, now, not chunkRaced_);
    }
    bool const changed = draws != lines_ or not sameIds(frame.visible, lastVisible_);
    lines_             = draws;
    lastVisible_       = frame.visible;
    if (not extended and not changed) return std::nullopt;
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
    if (not box or emptyOf(*box)) {
        ++refusals_;
        return false;
    }
    // The old anchor must not have moved while the new one was found: both boxes are taken at one
    // scroll position. A scroll in flight moves it -- exactly when re-anchoring is most needed --
    // so this refusal is counted apart to name the churn (docs/adr/0189).
    auto const anchorBox = page_->box(*anchor_);
    if (not anchorBox or *anchorBox != anchorBox_) {
        ++refusals_;
        ++refusalsMoved_;
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
        bool awaiting = false;
        auto next     = page_->step(*cursor, 1, &awaiting);
        if (not next) {
            // A page that has not laid the next words out yet is not the document's end: the walk
            // stops for this step and tries again, rather than being marked finished (docs/adr/0199).
            if (not awaiting) ended = true;
            break;
        }
        // read() carries the word's own box; a separate box() probe would read the same rectangles
        // twice across the process boundary (docs/adr/0188).
        auto const read = page_->read(*next);
        if (read and not emptyOf(read->fullBox)) {
            if (read->fullBox.y > band.y + band.height - 1) break;
            auto const word = trimmedWord(read->text, read->fullBox, read->lettersEnd);
            if (word) {
                fresh.push_back({.id     = nextId_++,
                                 .dx     = read->fullBox.x - anchorBox_.x,
                                 .dy     = read->fullBox.y - anchorBox_.y,
                                 .width  = word->box.width,
                                 .height = word->box.height});
                rememberWord(fresh.back().id, word->text, words);
                sight_[fresh.back().id] = {.word = next};
            }
        }
        cursor = std::move(next);
    }
    // The anchor must not have moved during the read: these words would then sit at two scroll positions.
    auto const anchorBox = page_->box(*anchor_);
    if (not anchorBox or *anchorBox != anchorBox_) {
        chunkRaced_ = true;
        return false;
    }
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
        bool awaiting = false;
        auto next     = page_->step(*cursor, -1, &awaiting);
        if (not next) {
            if (not awaiting) ended = true;
            break;
        }
        auto const read = page_->read(*next);
        if (read and not emptyOf(read->fullBox)) {
            if (read->fullBox.y + read->fullBox.height - 1 < band.y) break;
            auto const word = trimmedWord(read->text, read->fullBox, read->lettersEnd);
            if (word) {
                fresh.push_back({.id     = nextId_++,
                                 .dx     = read->fullBox.x - anchorBox_.x,
                                 .dy     = read->fullBox.y - anchorBox_.y,
                                 .width  = word->box.width,
                                 .height = word->box.height});
                rememberWord(fresh.back().id, word->text, words);
                sight_[fresh.back().id] = {.word = next};
            }
        }
        cursor = std::move(next);
    }
    // The anchor must not have moved during the read: these words would then sit at two scroll positions.
    auto const anchorBox = page_->box(*anchor_);
    if (not anchorBox or *anchorBox != anchorBox_) {
        chunkRaced_ = true;
        return false;
    }
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
    // The same text the names come from says whether the page is English at all: a French, Spanish
    // or pinyin page is held whole, its words neither read nor drawn, until a later read says
    // English again (docs/adr/0187). A transient empty read above keeps the last verdict.
    bool const wasForeign = foreign_;
    foreign_              = pageLanguageOf(text) == PageLanguage::Foreign;
    if (foreign_ and not wasForeign) note_ = "held: the page reads non-English";
}

void TrackCore::rememberWord(int const id, std::string const& text, std::vector<ReadWord>& added)
{
    if (foreign_) return; // a non-English page teaches no words (docs/adr/0187)
    // Names are not words to learn: the page writes them capitalized throughout. And a word the
    // track has already sent keeps the one line it won -- the reader asked for the first
    // occurrence, not for every one the page holds (docs/adr/0161 keeps the name rule).
    std::string const key = normalizedWord(text);
    if (key.empty() or names_.contains(key)) return;
    if (not wordsSeen_.insert(key).second) return;
    added.push_back({.id = id, .text = text});
}

void TrackCore::applySight(std::vector<PlacedSpan>& placed, std::chrono::steady_clock::time_point const now, bool const mayCheck)
{
    // A reader whose point query costs tens of milliseconds (a PDF viewer) cannot pay it per place:
    // its places draw as they read, the way they did before the sight pass (docs/adr/0193).
    double const costMs = (std::max)(0.5, static_cast<double>(page_->sightCost().count()) / 1000.0);
    if (std::chrono::microseconds{static_cast<long long>(costMs * 1000)} > kSightCostCeiling) return;
    if (sightCreditAt_ != std::chrono::steady_clock::time_point{}) {
        auto const elapsed = std::chrono::duration<double, std::milli>(now - sightCreditAt_).count();
        sightCreditMs_ += (std::min)(elapsed, 100.0) * kSightSharePercent / 100.0;
    }
    sightCreditAt_                 = now;
    int                     budget = kSightChecksPerStep;
    std::vector<PlacedSpan> kept;
    kept.reserve(placed.size());
    for (auto const& span : placed) {
        SpanSight& sight = sight_[span.id];
        bool const stale = not sight.checked or now - sight.checkedAt >= kSightRefresh;
        if (mayCheck and stale) {
            // The first sweep keeps the reveal's pace; the refresh spends accrued credit, one hit at
            // its own cost, so a slow reader refreshes slower rather than starving the anchor read.
            bool const pay = sight.checked ? sightCreditMs_ >= costMs : budget > 0;
            if (pay) {
                if (sight.checked)
                    sightCreditMs_ -= costMs;
                else
                    --budget;
                sight.checked   = true;
                sight.checkedAt = now;
                sight.occluded  = not page_->visible(*sight.word, {.x = span.x, .y = span.y, .width = span.width, .height = span.height});
                ++sightChecks_;
            }
        }
        // A place not tested yet draws nothing: its line waits for the step that tests it, so no
        // line ever stands over a control the track has not looked at.
        if (sight.checked and not sight.occluded) kept.push_back(span);
    }
    placed.swap(kept);
}

void TrackCore::invalidateSight()
{
    for (auto& entry : sight_)
        entry.second.checked = false;
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
    // A page that answers 0x0 is still laying out -- Chrome's PDF viewer does while it builds its
    // document. Keep it and re-read its box next step; dropping it would need a fresh search and a
    // foreground change before the track starts again. A page that answers less than the band is a
    // background tab's sliver, and its words would draw in its corner while the article goes unwired
    // (docs/adr/0163); that one is dropped so the next step searches anew.
    if (emptyOf(visible)) {
        if (layoutSince_ == std::chrono::steady_clock::time_point{}) {
            layoutSince_ = now;
            note_        = "awaiting layout: it answers " + std::to_string(visible.width) + "x" + std::to_string(visible.height) +
                    " in a " + std::to_string(band.width) + "x" + std::to_string(band.height) + " band";
        }
        if (now - layoutSince_ < kPageLayoutWait) return std::nullopt;
    }
    layoutSince_ = {};
    if (not fillsTheBand(visible, band)) {
        note_ = "refused: it answers " + std::to_string(visible.width) + "x" + std::to_string(visible.height) +
                " in a " + std::to_string(band.width) + "x" + std::to_string(band.height) + " band";
        dropPage();
        return std::nullopt;
    }
    auto anchor = page_->wordAt(centerOf(visible.x, visible.width), centerOf(visible.y, visible.height));
    if (not anchor) {
        dropPage();
        return std::nullopt;
    }
    auto const box = page_->box(*anchor);
    if (not box or emptyOf(*box)) {
        // A point over the page's blank ground reads no word; a few in a row mean the cached page is
        // not the one on screen, so it is dropped for a fresh search too.
        if (++emptyAnchors_ < kEmptyAnchors) return std::nullopt;
        dropPage();
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
    sight_[spans_.front().id] = {.word = anchor_};

    TrackFrame frame;
    if (word) rememberWord(spans_.front().id, word->text, frame.added);
    lines_ = false;
    return frame;
}

void TrackCore::relayoutPage()
{
    // The anchor keeps its id and its handle: its candidate is already judged, and the id is the
    // span the line will draw again once the walk re-reads around it.
    int anchorId = spans_.empty() ? nextId_++ : spans_.front().id;
    for (auto const& span : spans_)
        if (span.dx == 0 and span.dy == 0) {
            anchorId = span.id;
            break;
        }
    Band spanBox = anchorBox_;
    if (auto const read = page_->read(*anchor_))
        if (auto const word = trimmedWord(read->text, read->fullBox, read->lettersEnd); word) spanBox = word->box;

    spans_.clear();
    sight_.clear();
    // Every word is a first occurrence again: the spans are rebuilt with new ids, so the walk must
    // add them again for their lines to be judged (docs/adr/0198).
    wordsSeen_.clear();
    forward_     = anchor_;
    backward_    = anchor_;
    forwardDone_ = backwardDone_ = false;
    lastVisible_.clear();
    spans_.push_back({.id = anchorId, .dx = 0, .dy = 0, .width = spanBox.width, .height = spanBox.height});
    sight_[anchorId] = {.word = anchor_};
}

void TrackCore::dropPage()
{
    reader_.drop();
    page_.reset();
    cacheWarm_   = false;
    layoutSince_ = {};
    // The verdict was read from the page now gone: a fresh page reads its own names and language
    // before it draws, the way a cold start does, rather than inheriting this one (docs/adr/0190).
    names_       = {};
    namesLoaded_ = false;
    foreign_     = false;
    namesAt_     = {};
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
    sight_.clear();
    lastVisible_.clear();
    sightCreditMs_ = 0;
    sightCreditAt_ = {};
    TrackFrame frame;
    frame.lost = true;
    return frame;
}

}
