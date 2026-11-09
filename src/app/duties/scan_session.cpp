/**
 * @file scan_session.cpp
 * @brief The scan session's policy, moved verbatim from the capture duty's scan block (docs/adr/0165).
 */

#include "scan_session.h"

#include <QStringList>

#include <algorithm>

#include "util/log.h"

namespace lens::app {

ScanSession::ScanSession(core::KnownStore& store, Marshal marshal, TrackFactory makeTrack, StepRoute route)
    : store_(store), marshal_(std::move(marshal)), makeTrack_(std::move(makeTrack)), route_(std::move(route))
{}

ScanSession::~ScanSession() = default;

void ScanSession::invalidate()
{
    ++generation_;
    scanContext_.clear();
    track_.reset();
    scanCandidates_.clear();
    scanJudged_.clear();
}

void ScanSession::setScanPaused(bool const paused)
{
    if (scanPaused_ == paused) return;
    scanPaused_ = paused;
    invalidate();
}

void ScanSession::setMarkButtonHeld(bool const held)
{
    markButtonHeld_ = held;
}

void ScanSession::stopTrack()
{
    track_.reset();
    scanContext_.clear();
}

void ScanSession::debugArm(ScanPlacement const placement)
{
    scanScreen_       = placement.screen;
    scanScreenOrigin_ = placement.origin;
    scanScale_        = placement.scale;
}

ScanDebugState ScanSession::debugState() const
{
    auto candidates = 0;
    for (auto const& carried : scanCandidates_)
        candidates += static_cast<int>(carried.second.size());
    return {.tracking = not scanContext_.isEmpty(), .generation = generation_, .candidates = candidates};
}

ScanStep ScanSession::tick(ScanFacts const& facts)
{
    ScanStep step;
    minimumWordLength_ = facts.minimumWordLength;
    if (facts.budgetPaused or scanPaused_ or not facts.autoScan) return step;
    // A level moved by any path (the settings page, a sync round) reaches the scan here, so the candidates re-judge.
    if (store_.level() != scanLevel_) {
        scanLevel_ = store_.level();
        invalidate();
        step.cleared = true;
    }
    // The page text comes through UI Automation, which exposes a browser's page; no other window has text here.
    // The scan is torn down once on leaving the page, not on every tick away from it: a lens card
    // over the browser, or a momentary foreground blip, would otherwise keep rebuilding the track
    // and take the mark button down under the reader's pointer (docs/adr/0161). invalidate
    // clears scanContext_, so the next tick away is a no-op and the return re-scans.
    if (not facts.foreground or facts.foreground->kind != ScanForeground::Kind::Chrome) {
        // The reader's press on the mark button takes Chrome out of the foreground for the click's
        // length, and the button hiding on a verdict can leave the system with no foreground at
        // all -- neither is the page leaving, and a teardown here kills the click's own word list
        // (docs/adr/0161). The hold keeps the gate open; the first tick after the release settles.
        if (markButtonHeld_ and (not facts.foreground or facts.foreground->kind == ScanForeground::Kind::OwnProcess)) return step;
        if (not scanContext_.isEmpty()) {
            invalidate();
            step.cleared = true;
        }
        return step;
    }
    if (scanContext_ != facts.foreground->context) {
        invalidate();
        step.cleared = true;
        scanContext_ = facts.foreground->context;
        // The first query for a browser window makes it build its page tree, so the query runs ahead of the first read.
        step.warm = facts.foreground->window;
    }
    if (facts.jobOrUploadInFlight) return step;
    if (not facts.foreground->placement) return step;
    auto const& placement = *facts.foreground->placement;
    scanScreen_           = placement.screen;
    scanScreenOrigin_     = placement.origin;
    scanScale_            = placement.scale;
    if (not track_) {
        startTrack(facts.foreground->window, placement.band);
    } else if (placement.band != scanBand_) {
        scanBand_ = placement.band;
        track_->setBand(placement.band);
    }
    return step;
}

void ScanSession::startTrack(quintptr const window, QRect const& band)
{
    scanBand_             = band;
    const auto generation = generation_;
    track_                = makeTrack_(window, band, [this, generation](capture::TrackFrame frame) {
        // The track's worker hands each frame to the owner's thread; a frame from a track that was since dropped is refused by its generation.
        // The step the frame answers routes to the owner here, on the owner's thread -- the placed
        // words and the cleared flag are the owner's to emit (docs/adr/0165).
        marshal_([this, generation, frame = std::move(frame)]() mutable { route_(onFrame(generation, std::move(frame))); });
    });
}

ScanStep ScanSession::onFrame(std::uint64_t const generation, capture::TrackFrame frame)
{
    ScanStep step;
    // The scan was invalidated after this frame was read (the window, the level or the setting moved): it is stale.
    if (generation != generation_) return step;
    if (frame.lost) {
        scanCandidates_.clear();
        step.cleared = true;
        return step;
    }
    if (not frame.added.empty()) judgeAdded(frame);
    ScanStep::Placed placed;
    placed.screen = scanScreen_;
    placed.origin = scanScreenOrigin_;
    placed.scale  = scanScale_;
    for (auto const& span : frame.visible) {
        auto const found = scanCandidates_.find(span.id);
        if (found == scanCandidates_.end()) continue;
        // Every occurrence keeps its own box; the judgement is shared by all of them.
        for (auto const& candidate : found->second) {
            if (candidate.state == core::CandidateState::Mastered) continue;
            placed.words.push_back({candidate.lemma, candidate.surface, candidate.state, span.x, span.y, span.width, span.height});
        }
    }
    step.placed = std::move(placed);
    return step;
}

void ScanSession::judgeAdded(capture::TrackFrame const& frame)
{
    QStringList drawn;
    for (auto const& word : frame.added) {
        auto const surface = QString::fromStdString(word.text).trimmed();
        auto const key     = surface.toStdString();
        if (not scanJudged_.contains(key)) {
            // New and Known both draw a line (restyled for Known); a Mastered candidate, the
            // reader's retire verdict or the band's, draws none (docs/adr/0159).
            auto judged = judgeSurface(key);
            for (auto const& [lemma, state] : judged)
                drawn << QString::fromStdString(lemma);
            scanJudged_.emplace(key, std::move(judged));
        }
        for (auto const& [lemma, state] : scanJudged_.at(key))
            scanCandidates_[word.id].push_back({QString::fromStdString(lemma), surface, state});
    }
    if (not drawn.isEmpty()) LENS_INFO("scan: {} candidate(s) read on the page: {}", drawn.size(), drawn.join(QLatin1Char(' ')).toStdString());
}

void ScanSession::onVerdictChanged(QString const& lemma)
{
    const std::string key = lemma.toStdString();

    // Re-judge every surface the changed lemma was judged under, right now: the verdict store
    // already holds the new verdict, and the track's frames carry words only as they are added,
    // so waiting for a re-read would leave the stale lines standing all page long.
    std::unordered_map<std::string, core::CandidateState> fresh;
    for (auto it = scanJudged_.begin(); it != scanJudged_.end();) {
        const bool touched = std::any_of(it->second.begin(), it->second.end(), [&key](auto const& entry) { return entry.first == key; });
        if (not touched) {
            ++it;
            continue;
        }
        auto judged = judgeSurface(it->first);
        if (judged.empty())
            it = scanJudged_.erase(it);
        else {
            for (auto const& [judgedLemma, judgedState] : judged)
                if (judgedLemma == key) fresh.emplace(it->first, judgedState);
            it->second = std::move(judged);
            ++it;
        }
    }

    // The occurrences follow the judgement: a retired lemma's boxes stop being emitted, a restyled
    // one carry their new state on the next frame.
    for (auto& [id, candidates] : scanCandidates_) {
        for (auto& candidate : candidates) {
            if (candidate.lemma.toStdString() != key) continue;
            const auto state = fresh.find(candidate.surface.toStdString());
            candidate.state  = state == fresh.end() ? core::CandidateState::Mastered : state->second;
        }
    }
}

std::vector<std::pair<std::string, core::CandidateState>> ScanSession::judgeSurface(std::string const& surface) const
{
    auto const known    = store_.known();
    auto const mastered = store_.mastered();
    auto const band     = core::difficultyForLevel(store_.level());

    std::vector<std::pair<std::string, core::CandidateState>> judged;
    for (auto const& candidate : core::filterWords(surface, known, mastered, band, minimumWordLength_, false))
        if (candidate.state != core::CandidateState::Mastered) judged.emplace_back(candidate.lemma, candidate.state);
    return judged;
}

}
