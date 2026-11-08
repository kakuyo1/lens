/** @file scan_session_test.cpp
 * @brief The scan session's gates, track life, judgement and invalidation, over a scripted track.
 *
 * The first duty-side scan-loop tests: a scripted ScanTrack stands in for the UIA shell, the
 * marshal is synchronous, and the judgement runs on the repository's own word list (docs/adr/0165).
 */

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QString>
#include <QTemporaryDir>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "app/duties/scan_session.h"
#include "capture/track_core.h"
#include "core/document/known_store.h"

namespace {

using lens::app::ScanCandidate;
using lens::app::ScanForeground;
using lens::app::ScanFacts;
using lens::app::ScanPlacement;
using lens::app::ScanSession;
using lens::app::ScanStep;
using lens::app::ScanTrack;
using lens::app::TrackSink;
using lens::core::KnownStore;
using lens::core::WordVerdict;

/// @brief The word list the judgement runs on, loaded once per process from the repository's data.
void loadWordlistOnce()
{
    static bool const loaded = [] {
        auto const data = std::filesystem::path{LENS_SOURCE_DIR} / "data";
        lens::core::loadWordlist(data / "wordlist.txt");
        lens::core::loadIrregulars(data / "irregulars.tsv");
        return true;
    }();
    (void)loaded;
}

/// A track that records the bands it was resized to. The frames never come from it: the test calls
/// the sink the session handed the factory, the way the worker thread would.
class FakeTrack final : public ScanTrack {
public:
    explicit FakeTrack(std::function<void(QRect)> onBand)
        : onBand_{std::move(onBand)}
    {}

    void setBand(QRect const band) override
    {
        onBand_(band);
    }

private:
    std::function<void(QRect)> onBand_;
};

struct Harness {
    Harness()
        : store(KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json"))
    {
        loadWordlistOnce();
        store.setLevel(1);
    }

    QTemporaryDir      temporary;
    KnownStore         store;
    int                started = 0; ///< Tracks the factory built.
    std::vector<QRect> bands;       ///< Every setBand the live track took.
    TrackSink          deliver;     ///< The live track's frame sink; empty while no track runs.
    /// Every step the marshaled frame path routed; the regression the frame route once dropped.
    std::vector<ScanStep> routed;

    ScanSession session{store,
                        [](std::function<void()> work) { work(); },
                        [this](quintptr, QRect, TrackSink sink) {
                            ++started;
                            deliver = std::move(sink);
                            return std::make_unique<FakeTrack>([this](QRect const band) { bands.push_back(band); });
                        },
                        [this](ScanStep const& step) { routed.push_back(step); }};

    /// @brief Facts with a Chrome window in front, placed on a monitor; the one foreground the scan reads.
    ScanFacts chrome(QRect const band = QRect{0, 0, 1920, 400}) const
    {
        ScanFacts facts;
        facts.autoScan  = true;
        facts.scanReady = true;
        ScanForeground foreground;
        foreground.kind      = ScanForeground::Kind::Chrome;
        foreground.window    = 42;
        foreground.context   = QStringLiteral("chrome-doc");
        foreground.placement = ScanPlacement{.band = band, .screen = QRect{0, 0, 1920, 1080}, .origin = QPoint{0, 0}, .scale = 1.0};
        facts.foreground     = foreground;
        return facts;
    }
};

/// @brief Facts with a window in front that the scan does not read.
ScanFacts foregroundOf(ScanForeground::Kind const kind)
{
    ScanFacts facts;
    facts.autoScan  = true;
    facts.scanReady = true;
    ScanForeground foreground;
    foreground.kind    = kind;
    foreground.context = QStringLiteral("other");
    facts.foreground   = foreground;
    return facts;
}

lens::capture::TrackFrame frameWithAdded(int const id, std::string const& text)
{
    lens::capture::TrackFrame frame;
    frame.added.push_back({id, text});
    frame.visible.push_back({id, 10, 20, 30, 12});
    return frame;
}

TEST(ScanSession, GatesRefuseSilently)
{
    Harness h;
    auto    facts = h.chrome();

    facts.budgetPaused = true;
    EXPECT_FALSE(h.session.tick(facts).cleared);
    EXPECT_EQ(h.started, 0);

    facts.budgetPaused = false;
    facts.scanReady    = false;
    EXPECT_FALSE(h.session.tick(facts).cleared);
    EXPECT_EQ(h.started, 0);

    facts.scanReady = true;
    facts.autoScan  = false;
    EXPECT_FALSE(h.session.tick(facts).cleared);
    EXPECT_EQ(h.started, 0);
}

TEST(ScanSession, TheGatesTearNothingDownWhileATrackRuns)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_EQ(h.started, 1);

    auto paused         = h.chrome();
    paused.budgetPaused = true;
    EXPECT_FALSE(h.session.tick(paused).cleared);
    EXPECT_EQ(h.started, 1);

    h.session.setScanPaused(true);
    // The pause itself invalidates; the gate afterwards is silent.
    auto unpaused      = h.chrome();
    unpaused.scanReady = false;
    EXPECT_FALSE(h.session.tick(unpaused).cleared);
}

TEST(ScanSession, TheFirstChromeTickClearsAndStartsTheTrack)
{
    Harness    h;
    auto const step = h.session.tick(h.chrome());
    EXPECT_TRUE(step.cleared);
    ASSERT_TRUE(step.warm);
    EXPECT_EQ(*step.warm, quintptr{42});
    EXPECT_EQ(h.started, 1);
}

TEST(ScanSession, ALevelDriftInvalidatesAndRescansInTheSameTick)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_EQ(h.started, 1);

    h.store.setLevel(h.store.level() + 1);
    auto const step = h.session.tick(h.chrome());
    EXPECT_TRUE(step.cleared);
    ASSERT_TRUE(step.warm);
    EXPECT_EQ(h.started, 2);
}

TEST(ScanSession, LeavingChromeTearsDownOnceAndTheMarkHoldShieldsItsOwn)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_EQ(h.started, 1);

    EXPECT_TRUE(h.session.tick(foregroundOf(ScanForeground::Kind::Other)).cleared);
    EXPECT_EQ(h.started, 1);
    // The second tick away is a no-op: the teardown is once per leaving.
    EXPECT_FALSE(h.session.tick(foregroundOf(ScanForeground::Kind::Other)).cleared);

    // The hold keeps the gate open while the foreground is gone or is this process.
    h.session.tick(h.chrome());
    ASSERT_EQ(h.started, 2);
    h.session.setMarkButtonHeld(true);
    EXPECT_FALSE(h.session.tick(foregroundOf(ScanForeground::Kind::OwnProcess)).cleared);
    EXPECT_EQ(h.started, 2);
    ScanFacts gone;
    gone.autoScan  = true;
    gone.scanReady = true;
    EXPECT_FALSE(h.session.tick(gone).cleared);
    // Another application in front is still the page leaving.
    EXPECT_TRUE(h.session.tick(foregroundOf(ScanForeground::Kind::Other)).cleared);
}

TEST(ScanSession, AContextChangeRestartsTheTrackAndWarmsTheWindow)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_EQ(h.started, 1);

    auto other                = h.chrome();
    other.foreground->context = QStringLiteral("chrome-other");
    auto const step           = h.session.tick(other);
    EXPECT_TRUE(step.cleared);
    ASSERT_TRUE(step.warm);
    EXPECT_EQ(*step.warm, quintptr{42});
    EXPECT_EQ(h.started, 2);
}

TEST(ScanSession, AJobInFlightBlocksTheTrackButNotTheWarm)
{
    Harness h;
    auto    facts             = h.chrome();
    facts.jobOrUploadInFlight = true;
    auto const step           = h.session.tick(facts);
    EXPECT_TRUE(step.cleared);
    ASSERT_TRUE(step.warm);
    EXPECT_EQ(h.started, 0);

    facts.jobOrUploadInFlight = false;
    h.session.tick(facts);
    EXPECT_EQ(h.started, 1);
}

TEST(ScanSession, AFrameThroughTheTrackSinkRoutesItsStepToTheOwner)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_EQ(h.started, 1);
    ASSERT_TRUE(h.deliver);

    // The production track's worker hands frames to the sink; each one's step must reach the
    // owner's route, or the placed words never reach the surface (the regression the frame path
    // once carried: onFrame ran and judged, but its answer was dropped on the floor).
    h.deliver(frameWithAdded(1, "ubiquitous"));
    ASSERT_EQ(h.routed.size(), 1);
    ASSERT_TRUE(h.routed.front().placed);
    EXPECT_EQ(h.routed.front().placed->words.size(), 1);
    EXPECT_EQ(h.routed.front().placed->words.front().lemma, QString::fromStdString("ubiquitous"));

    // A lost frame routes its clear the same way.
    lens::capture::TrackFrame lost;
    lost.lost = true;
    h.deliver(lost);
    ASSERT_EQ(h.routed.size(), 2);
    EXPECT_TRUE(h.routed.back().cleared);
}

TEST(ScanSession, TheBandIsFollowedOnTheLiveTrack)
{
    Harness h;
    h.session.tick(h.chrome(QRect{0, 0, 1920, 400}));
    ASSERT_EQ(h.started, 1);

    h.session.tick(h.chrome(QRect{0, 0, 1920, 500}));
    EXPECT_EQ(h.started, 1);
    ASSERT_EQ(h.bands.size(), 1);
    EXPECT_EQ(h.bands.back(), QRect(0, 0, 1920, 500));

    h.session.tick(h.chrome(QRect{0, 0, 1920, 500}));
    EXPECT_EQ(h.bands.size(), 1);

    // An unresolved placement leaves the track exactly as it is.
    auto unplaced = h.chrome();
    unplaced.foreground->placement.reset();
    h.session.tick(unplaced);
    EXPECT_EQ(h.bands.size(), 1);
}

TEST(ScanSession, FramesAreJudgedAndPlacedOnEveryFrame)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_TRUE(h.deliver);
    h.store.mark("window", WordVerdict::Known);

    lens::capture::TrackFrame frame;
    frame.added.push_back({1, "ubiquitous"});
    frame.added.push_back({2, "window"});
    frame.visible.push_back({1, 10, 20, 30, 12});
    frame.visible.push_back({2, 50, 20, 40, 12});
    auto const first = h.session.onFrame(h.session.generation(), frame);
    ASSERT_TRUE(first.placed);
    EXPECT_EQ(first.placed->screen, QRect(0, 0, 1920, 1080));
    EXPECT_EQ(first.placed->origin, QPoint(0, 0));
    ASSERT_EQ(first.placed->words.size(), 2);
    EXPECT_EQ(first.placed->words[0].lemma, QStringLiteral("ubiquitous"));
    EXPECT_EQ(first.placed->words[0].state, lens::core::CandidateState::New);
    EXPECT_EQ(first.placed->words[0].x, 10);
    EXPECT_EQ(first.placed->words[1].lemma, QStringLiteral("window"));
    EXPECT_EQ(first.placed->words[1].state, lens::core::CandidateState::Known);
    EXPECT_EQ(first.placed->words[1].width, 40);

    // Sent on every frame, even when empty: a frame with no candidate in view takes the last one's lines down.
    lens::capture::TrackFrame visibleOnly;
    visibleOnly.visible.push_back({1, 10, 20, 30, 12});
    auto const second = h.session.onFrame(h.session.generation(), visibleOnly);
    ASSERT_TRUE(second.placed);
    ASSERT_EQ(second.placed->words.size(), 1);
    EXPECT_EQ(second.placed->words[0].lemma, QStringLiteral("ubiquitous"));
}

TEST(ScanSession, AStaleFrameIsNothing)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_TRUE(h.deliver);

    auto const step = h.session.onFrame(h.session.generation() + 1, frameWithAdded(1, "ubiquitous"));
    EXPECT_FALSE(step.cleared);
    EXPECT_FALSE(step.placed);
}

TEST(ScanSession, ALostFrameClearsTheCandidatesAndKeepsTheJudgement)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_TRUE(h.deliver);
    auto const placed = h.session.onFrame(h.session.generation(), frameWithAdded(1, "ubiquitous"));
    ASSERT_TRUE(placed.placed);
    EXPECT_EQ(placed.placed->words.size(), 1);

    lens::capture::TrackFrame lost;
    lost.lost          = true;
    auto const cleared = h.session.onFrame(h.session.generation(), lost);
    EXPECT_TRUE(cleared.cleared);
    EXPECT_FALSE(cleared.placed);

    // The id the track handed out is void, so the same id carries nothing until it is read again.
    lens::capture::TrackFrame visibleOnly;
    visibleOnly.visible.push_back({1, 10, 20, 30, 12});
    auto const empty = h.session.onFrame(h.session.generation(), visibleOnly);
    ASSERT_TRUE(empty.placed);
    EXPECT_TRUE(empty.placed->words.empty());

    // Read again, it is judged from the cache -- the same line comes back, with no second judgement.
    auto const again = h.session.onFrame(h.session.generation(), frameWithAdded(2, "ubiquitous"));
    ASSERT_TRUE(again.placed);
    ASSERT_EQ(again.placed->words.size(), 1);
    EXPECT_EQ(again.placed->words[0].lemma, QStringLiteral("ubiquitous"));
}

TEST(ScanSession, AVerdictChangeRestylesTheCandidatesInPlace)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_TRUE(h.deliver);
    auto const placed = h.session.onFrame(h.session.generation(), frameWithAdded(1, "ubiquitous"));
    ASSERT_EQ(placed.placed->words.size(), 1);
    EXPECT_EQ(placed.placed->words[0].state, lens::core::CandidateState::New);

    // A known mark restyles the line on the next frame, with no invalidation at all.
    h.store.mark("ubiquitous", WordVerdict::Known);
    h.session.onVerdictChanged(QStringLiteral("ubiquitous"));
    lens::capture::TrackFrame visibleOnly;
    visibleOnly.visible.push_back({1, 10, 20, 30, 12});
    auto const known = h.session.onFrame(h.session.generation(), visibleOnly);
    ASSERT_TRUE(known.placed);
    ASSERT_EQ(known.placed->words.size(), 1);
    EXPECT_EQ(known.placed->words[0].state, lens::core::CandidateState::Known);

    // A mastered mark retires it: the surface's judgement is empty, and no candidate is carried.
    h.store.mark("ubiquitous", WordVerdict::Mastered);
    h.session.onVerdictChanged(QStringLiteral("ubiquitous"));
    auto const retired = h.session.onFrame(h.session.generation(), visibleOnly);
    ASSERT_TRUE(retired.placed);
    EXPECT_TRUE(retired.placed->words.empty());
}

TEST(ScanSession, AnInvalidationDropsTheTrackAndTheGenerationMoves)
{
    Harness h;
    h.session.tick(h.chrome());
    ASSERT_TRUE(h.deliver);
    auto const generation = h.session.generation();

    h.session.invalidate();
    EXPECT_EQ(h.session.generation(), generation + 1);
    EXPECT_FALSE(h.session.trackingPage());
    // A frame from the dropped track is refused by its generation.
    auto const stale = h.session.onFrame(generation, frameWithAdded(1, "ubiquitous"));
    EXPECT_FALSE(stale.placed);
    EXPECT_FALSE(stale.cleared);

    // The same context starts a fresh track, because the context the old one ran under is gone.
    h.session.tick(h.chrome());
    EXPECT_EQ(h.started, 2);
}

}
