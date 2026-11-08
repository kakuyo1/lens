#pragma once

/**
 * @file scan_session.h
 * @brief The scan session: the gates, the page track's life, the per-surface judgement and the
 *        verdict invalidation -- the policy the capture duty's scan block carried, in one module
 *        (docs/adr/0165).
 *
 * The duty gathers what one tick needs to know and executes what the session answers: the timers,
 * the platform reads and the QObject signals stay with the duty, the way the ceremony and stroll
 * modules left their controller an adapter (docs/adr/0139). The judgement is deliberately the same
 * shape it had on the duty -- filterWords per surface, cached by surface, a retired or mastered
 * lemma carried by no candidate (docs/adr/0159); the payload's verdict tokens are minted once, by
 * verdict_view.h (docs/adr/0166).
 *
 * @note No QObject, no clock: the duty hands in the facts, and the frames the track delivers come
 *       back through the marshal the duty injected.
 * @note Everything here stays on the duty side of the PageReader seam (docs/adr/0164): the session
 *       owns the track's life, never a page read.
 */

#include <QPoint>
#include <QRect>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "capture/track_core.h"
#include "core/domain/filter_core.h"
#include "core/document/known_store.h"

namespace lens::app {

/// @brief One candidate the scan shows on a word of the page. New draws the faint line, Known the
///        firmer one; a Mastered candidate is not carried at all (docs/adr/0159).
struct ScanCandidate {
    QString              lemma;
    QString              surface;
    core::CandidateState state = core::CandidateState::New;
};

/// @brief One placed candidate, in the fields the scanPlaced payload spells. The duty projects
///        this into the QVariantMap QML reads; the verdict strings live there, not here.
struct ScanPlacedWord {
    QString              lemma;
    QString              surface;
    core::CandidateState state  = core::CandidateState::New;
    int                  x      = 0;
    int                  y      = 0;
    int                  width  = 0;
    int                  height = 0;
};

/// @brief The monitor placement the duty resolved for the foreground window.
struct ScanPlacement {
    QRect  band;   ///< The window cut to the display it sits on, physical pixels.
    QRect  screen; ///< The display's device-independent geometry, as scanPlaced carries it.
    QPoint origin; ///< The display's top-left, physical pixels.
    double scale = 1.0;
};

/// @brief The window in front, as the scan's gate reads it.
struct ScanForeground {
    /// @brief Where the window sits relative to this process. The scan reads Chrome only, and its
    ///        own process is the one the mark button's hold shields (docs/adr/0161).
    enum class Kind : std::uint8_t { Chrome,
                                     OwnProcess,
                                     Other };
    Kind     kind   = Kind::Other;
    quintptr window = 0;
    QString  context;
    /// Nothing when the window's display could not be resolved or its band is empty: the tick
    /// leaves the track exactly as it is.
    std::optional<ScanPlacement> placement;
};

/// @brief What one scan tick needs to know, gathered by the duty from the settings, the window
///        system and the job slot.
struct ScanFacts {
    bool budgetPaused        = false;
    bool autoScan            = false;
    bool scanReady           = false;
    bool jobOrUploadInFlight = false;
    /// The prose floor the judgement applies. Cached between ticks, which is safe because a change
    /// to it invalidates the generation before any frame can carry the old value.
    int minimumWordLength = 3;
    /// Nothing when no window is in front at all.
    std::optional<ScanForeground> foreground;
};

/// @brief What one tick or frame asks the adapter to do.
struct ScanStep {
    bool cleared = false; ///< The placed lines no longer describe the page: emit scanCleared.
    /// The window whose UI Automation tree to warm, right after its page became the tracked one.
    std::optional<quintptr> warm;
    /// @brief The candidates in view and the monitor they sit on. Present on every frame the scan
    ///        still owns, empty or not: a frame with no candidate in view takes the last one's
    ///        lines down with it.
    struct Placed {
        std::vector<ScanPlacedWord> words;
        QRect                       screen;
        QPoint                      origin;
        double                      scale = 1.0;
    };
    std::optional<Placed> placed;
};

/// @brief The facts a developer surface reports about the session (docs/adr/0165).
struct ScanDebugState {
    bool          tracking   = false; ///< A page's context is live.
    std::uint64_t generation = 0;
    int           candidates = 0; ///< Every candidate the word ids carry, mastered marks included.
};

/// @brief A page track the session owns the life of: started by its factory, resized by setBand,
///        stopped by destruction.
/// @note The production adapter wraps PageTrack; a test scripts one. The session never sees a
///       window handle beyond what it hands the factory.
class ScanTrack {
public:
    virtual ~ScanTrack() = default;
    /// @brief The visible area moved or resized; the next step places words inside it.
    virtual void setBand(QRect band) = 0;
};

/// @brief The frames a track delivers. Called on the track's worker thread; the implementation
///        must only hand the frame across threads.
using TrackSink = std::function<void(capture::TrackFrame frame)>;
/// @brief Queues a call onto the owner's thread; the duty injects the QObject marshal.
using Marshal = std::function<void(std::function<void()> work)>;
/// @brief Hands a step the session produced on the owner's thread back to its owner. The frame
///        path reaches the owner only through this: the marshaled lambda runs on the owner's
///        thread, where the owner can execute what the step asks (emit scanPlaced, scanCleared).
using StepRoute = std::function<void(ScanStep const& step)>;
/// @brief Builds the platform track for @p window inside @p band; the session owns what it returns.
using TrackFactory = std::function<std::unique_ptr<ScanTrack>(quintptr window, QRect band, TrackSink deliver)>;

class ScanSession {
public:
    /// @param store The verdict store the judgement reads; outlives *this.
    /// @param marshal Queues a call onto the owner's thread; frames from the track cross it.
    /// @param makeTrack Builds the platform track; production wraps PageTrack, a test scripts one.
    /// @param route Receives every step the track's frames produce, on the owner's thread. The
    ///              tick's step goes out through tick()'s return instead, so direct onFrame()
    ///              callers -- the tests, the debug surface -- route their own returns and this
    ///              callback stays silent for them.
    ScanSession(core::KnownStore& store, Marshal marshal, TrackFactory makeTrack, StepRoute route);
    ~ScanSession();

    ScanSession(ScanSession const&)            = delete;
    ScanSession& operator=(ScanSession const&) = delete;

    /// @brief The scan is invalid: the generation moves, the track and both caches are dropped.
    /// @note Invalidation announces itself: the duty emits scanCleared after each direct call, and
    ///       a tick reports the ones it made itself in the step.
    void invalidate();
    /// @brief Pause or resume the scan; a change invalidates, the way it always has.
    void setScanPaused(bool paused);
    /// @brief Whether the reader's finger is down on the mark button: while it holds, the
    ///        foreground gate does not tear the page down (docs/adr/0161).
    void setMarkButtonHeld(bool held);
    /// @brief One scan tick, on the duty's 250 ms cadence.
    /// @param facts What the duty gathered; the time is not among them, nothing here ages.
    ScanStep tick(ScanFacts const& facts);
    /// @brief One frame of the track: judge the words it read, and name the candidates it has in view.
    /// @param generation The generation the track was started under; a frame from a track that was
    ///                   since dropped is refused as stale.
    ScanStep onFrame(std::uint64_t generation, capture::TrackFrame frame);
    /// @brief The reader changed @p lemma's verdict: every surface it was judged under is re-judged
    ///        now, against the verdict store as it stands, so the next emission draws it right -- a
    ///        retired lemma's lines drop, a known one's restyle -- with no full scan invalidation.
    void onVerdictChanged(QString const& lemma);
    /// @brief Whether a page is being tracked, the context live. The duty reads autoScan itself,
    ///        live, when it asks on the session's behalf.
    [[nodiscard]] bool trackingPage() const
    {
        return not scanContext_.isEmpty();
    }
    [[nodiscard]] std::uint64_t generation() const
    {
        return generation_;
    }
    [[nodiscard]] bool scanPaused() const
    {
        return scanPaused_;
    }
    /// @brief Stop the track and join its worker. The duty owes the frames this before it dies.
    void stopTrack();
    /// @brief The placement a developer surface arms: the geometry the next frames are placed in,
    ///        with no track to read one. The frames the surface injects land here (docs/adr/0165).
    void                         debugArm(ScanPlacement placement);
    [[nodiscard]] ScanDebugState debugState() const;

private:
    /// @brief The surfaces of @p frame's added words are judged once each, their candidates carried
    ///        by the track's word id; the first read of a page's word is logged.
    void judgeAdded(capture::TrackFrame const& frame);
    /// @brief Start following @p window's page inside @p band; its frames come back to onFrame().
    void startTrack(quintptr window, QRect const& band);
    /// @return Every lemma of @p surface the band and the verdict store let through, judged state
    ///         attached. The one judgement entry: it reads the store and the level's band itself,
    ///         so the frame path and the verdict-change path share not only the pure filter but
    ///         the wiring that feeds it (docs/adr/0166).
    std::vector<std::pair<std::string, core::CandidateState>> judgeSurface(std::string const& surface) const;

    core::KnownStore& store_;
    Marshal           marshal_;
    TrackFactory      makeTrack_;
    StepRoute         route_; ///< The owner's step sink; the marshaled frame path routes through it.

    std::uint64_t                                       generation_ = 0;
    QString                                             scanContext_;
    int                                                 scanLevel_         = -1; ///< The level the scan's candidates were judged at; a change invalidates them.
    int                                                 minimumWordLength_ = 3;
    bool                                                scanPaused_        = false;
    bool                                                markButtonHeld_    = false; ///< The reader's finger is down on the mark button; the foreground gate holds.
    std::unique_ptr<ScanTrack>                          track_;                     ///< Follows the page on screen; null while the scan is not reading one.
    std::unordered_map<int, std::vector<ScanCandidate>> scanCandidates_;            ///< The candidates, by the track's word id.
    /// What each surface was judged to -- lemma and state -- for this scan generation. A word the
    /// track loses and reads again is not judged again.
    std::unordered_map<std::string, std::vector<std::pair<std::string, core::CandidateState>>> scanJudged_;
    QRect                                                                                      scanScreen_;
    QPoint                                                                                     scanScreenOrigin_;
    double                                                                                     scanScale_ = 1.0;
    QRect                                                                                      scanBand_;
};

}
