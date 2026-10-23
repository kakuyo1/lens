/**
 * @file glass_backdrop.cpp
 * @brief The glass sheets, their shared reader, and the image provider that serves them (see glass_backdrop.h).
 */

#include "glass_backdrop.h"

#include "core/domain/surface_table.h"
#include "glass_capability.h"
#include "glass_capture.h"
#include "glass_duplication.h"
#include "glass_math.h"
#include "glass_stats.h"
#include "app/platform/window_native.h"
#include "util/log.h"

#include <QGuiApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScreen>
#include <QUrlQuery>
#include <QWindow>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>

#ifdef _WIN32
#include <windows.h>

#include <QtGui/qscreen_platform.h>
#endif

namespace lens::app {

namespace {

/// The glass surfaces, by the name QML gives them in `image://glass/<name>/…`: every row of the one
/// surface table flagged `sheet` (core/domain/surface_table.h, docs/adr/0084). A fixed set: each one
/// gets its sheet at construction, so the image provider never has a sheet to create on one of its
/// own threads. The preview cards are here for the reason the table records -- a separate window
/// holds one backdrop (docs/adr/0041).
/// How far the sheet reaches past the window on each side, in DIP: the reserve plus the hysteresis between retakes.
/// The shader blurs and bends within the picture, so the sheet need not carry the blur's reach (docs/adr/0070). Keeping
/// it near kSheetReserve is worth about a fifth of every desktop read, which is priced by area
/// (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.6).
constexpr int kSheetMargin = 112;
/// How near the window may come to a side of the sheet that is not on the screen's edge before a new sheet is taken, in
/// DIP. The old sheet serves while the new one processes, so this covers the distance a drag moves in that time.
constexpr int kSheetReserve = 72;
/// The reference period a GDI screen keeps: about fifteen reads a second in all, however many sheets are live. A screen
/// that duplicates does not use this budget, nor does it raise a GDI read's rate (docs/adr/0072).
// ponytail: fixed rate, no back-off when the machine is busy; add one when the reads show up in someone's frame times.
constexpr int kLiveIntervalMs = kGlassGdiFrameMs;
/// How often the pipeline's tally is logged. Ten seconds is long enough for the read's median to mean something on a
/// still desktop, and short enough that a run of the application shows several windows.
constexpr int kSummaryIntervalMs = 10000;
/// How long a chained read waits for the next frame, in ms. A 144 Hz frame is 7 ms away, so this covers a missed
/// composition; on a still desktop the worker wakes four times less often than a 16 ms poll at 60 Hz and finds
/// nothing. The chain keeps a GUI timer's jitter from coalescing frames away (docs/adr/0074).
constexpr int kChainWaitMs = 16;

/// What a sheet read needs, copied out of Qt's objects on the GUI thread so a refresh on the pool never touches them.
struct Request {
    QRect          region;                     ///< The sheet, in device-independent pixels.
    QRect          screen;                     ///< The geometry of the screen the sheet lies on.
    QRect          native;                     ///< The sheet in device pixels, on the desktop.
    QRect          window;                     ///< The card the text tone is judged over.
    double         scale            = 1.0;     ///< Device pixels per device-independent pixel.
    double         fillAlpha        = 0.0;     ///< The white the card sits under, 0 to 1.
    DesktopSource* source           = nullptr; ///< The source the read goes through, chosen on the GUI thread.
    int            acquireTimeoutMs = 0;       ///< How long the worker waits for a new frame; zero for a timer-driven poll.
};

/// A picture of the desktop and the text tone read over the card.
struct Taken {
    QImage   picture;
    ToneRead tone;
    bool     noFrame = false; ///< The live source had no new frame: nothing changed, nothing to publish.
    bool     refused = false; ///< Every live source refused: the settings page's note has something to say.
    bool     usedGdi = false; ///< The read went to GDI, so the shared fifteen-a-second budget has been spent.
    /// The picture's fingerprint when its source took one on the worker, or 0 when the sheet has to take it.
    quint64 fingerprint = 0;
};

/// The first device pixel of @p screen on the desktop. Windows places the monitors in device pixels; where there is no
/// Windows screen to ask (a test platform), the screen's own corner stands in, and nothing reads the desktop there.
QPoint nativeOriginOf(QScreen const& screen)
{
#ifdef _WIN32
    if (auto const* native = screen.nativeInterface<QNativeInterface::QWindowsScreen>()) {
        auto info   = MONITORINFO{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(native->handle(), &info) != FALSE)
            return {info.rcMonitor.left, info.rcMonitor.top};
    }
#endif
    return screen.geometry().topLeft();
}

/// The request for a sheet over @p region, or nothing when the region does not lie on one screen. GUI thread only.
std::optional<Request> requestFor(QRect const region, QRect const window, double const fillAlpha, DesktopSource* const source)
{
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) return std::nullopt;
    auto const scale = screen->devicePixelRatio();
    return Request{
        .region    = region,
        .screen    = screen->geometry(),
        .native    = toNativeRect(region, screen->geometry(), nativeOriginOf(*screen), scale),
        .window    = window,
        .scale     = scale,
        .fillAlpha = fillAlpha,
        .source    = source,
    };
}

/// The sheet's picture and the text tone over the card. Touches no Qt object, so it runs on the pool.
Taken takeSheet(Request const& request)
{
    auto read = request.source->read(request.native, request.scale, request.acquireTimeoutMs);
    // The tally counts what the read was: a desktop read, a frame copied back, or a poll that found nothing new. A
    // refused read with no picture is a failed read, and the sheet is cleared by its null picture.
    if (read.usedGdi)
        glassStats().addRead(read.copyMs);
    else if (read.image.isNull())
        glassStats().countFrameSkip();
    else
        glassStats().addFrame(read.copyMs);
    if (read.image.isNull()) return {{}, {}, not read.refused and not read.usedGdi, read.refused, read.usedGdi, read.fingerprint};
    // The text is judged over the window, not over the margin the sheet adds around it. Read in place: copying the
    // card out of a native-resolution sheet costs a per-frame allocation and a second walk of the same pixels.
    auto const scale = read.image.devicePixelRatioF();
    auto const card  = QRect(qRound((request.window.x() - request.region.x()) * scale),
                            qRound((request.window.y() - request.region.y()) * scale),
                            qRound(request.window.width() * scale),
                            qRound(request.window.height() * scale));
    auto const tone  = readTone(read.image, card, request.fillAlpha);
    return {std::move(read.image), tone, false, read.refused, read.usedGdi, read.fingerprint};
}

} // namespace

GlassSheet::GlassSheet(QString name, GlassBackdrop* owner)
    : QObject(owner), name_(std::move(name)), owner_(owner)
{
}

QString GlassSheet::name() const
{
    return name_;
}

bool GlassSheet::lightText() const
{
    QMutexLocker const lock(&mutex_);
    return lightText_;
}

int GlassSheet::serial() const
{
    QMutexLocker const lock(&mutex_);
    return serial_;
}

QRect GlassSheet::sheet() const
{
    QMutexLocker const lock(&mutex_);
    return sheet_;
}

int GlassSheet::publishes() const
{
    QMutexLocker const lock(&mutex_);
    return publishes_;
}

bool GlassSheet::live() const
{
    return live_;
}

bool GlassSheet::duplicated() const
{
    return duplicated_;
}

bool GlassSheet::duplicationRefused() const
{
    return duplicationRefused_;
}

QImage GlassSheet::picture() const
{
    QMutexLocker const lock(&mutex_);
    return image_;
}

void GlassSheet::follow(QRect const window, double const fillAlpha)
{
    // Kept current even when the sheet still covers the card: a live refresh judges the text tone over where the card is.
    window_    = window;
    fillAlpha_ = fillAlpha;
    if (covers(window)) return;
    auto* const screen = QGuiApplication::screenAt(window.center());
    if (screen == nullptr or window.isEmpty() or not screen->geometry().contains(window)) {
        clear();
        return;
    }
    auto const sheet = window.adjusted(-kSheetMargin, -kSheetMargin, kSheetMargin, kSheetMargin).intersected(screen->geometry());
    capture(sheet, window, fillAlpha);
}

void GlassSheet::followDeferred(QRect const window, double const fillAlpha)
{
    window_    = window;
    fillAlpha_ = fillAlpha;
    // One read per sheet in flight: a move that arrives while the desktop is being read is served by the picture that
    // is already on its way, whose margin covers more ground than a drag covers in one read.
    if (reading() or covers(window)) return;
    auto* const screen = QGuiApplication::screenAt(window.center());
    if (screen == nullptr or window.isEmpty() or not screen->geometry().contains(window)) {
        clear();
        return;
    }
    auto const region  = window.adjusted(-kSheetMargin, -kSheetMargin, kSheetMargin, kSheetMargin).intersected(screen->geometry());
    auto const request = requestFor(region, window, fillAlpha, &owner_->gdi_);
    if (not request) {
        clear();
        return;
    }
    ++generation_;
    pending_              = generation_;
    auto const generation = generation_;
    owner_->pool_.start([this, request = *request, generation] {
        auto taken = takeSheet(request);
        QMetaObject::invokeMethod(
            this,
            [this, request, generation, taken = std::move(taken)] {
                // A retake or a drop since this read began owns the sheet now: the picture is of a region the caller no
                // longer wants, and clearing pending_ must not clear a newer read's flag.
                if (pending_ == generation) pending_ = 0;
                if (generation != generation_) return;
                publish(request.region,
                        request.screen,
                        request.window,
                        request.fillAlpha,
                        std::move(taken.picture),
                        taken.tone,
                        taken.fingerprint,
                        true);
            },
            Qt::QueuedConnection);
    });
}

bool GlassSheet::covers(QRect const window) const
{
    QMutexLocker const lock(&mutex_);
    if (sheet_.isEmpty() or not sheet_.contains(window)) return false;
    // A side on the screen's edge needs no room: nothing lies beyond it to blur or bend.
    if (sheet_.left() > sheetScreen_.left() and window.left() - sheet_.left() < kSheetReserve) return false;
    if (sheet_.top() > sheetScreen_.top() and window.top() - sheet_.top() < kSheetReserve) return false;
    if (sheet_.right() < sheetScreen_.right() and sheet_.right() - window.right() < kSheetReserve) return false;
    if (sheet_.bottom() < sheetScreen_.bottom() and sheet_.bottom() - window.bottom() < kSheetReserve) return false;
    return true;
}

void GlassSheet::capture(QRect const region, QRect const window, double const fillAlpha)
{
    auto const request = requestFor(region, window, fillAlpha, &owner_->gdi_);
    if (not request) {
        clear();
        return;
    }
    // A refresh still reading the sheet before this one is now stale, and is dropped when it lands.
    ++generation_;
    auto taken = takeSheet(*request);
    publish(request->region,
            request->screen,
            request->window,
            request->fillAlpha,
            std::move(taken.picture),
            taken.tone,
            taken.fingerprint,
            true);
}

void GlassSheet::setLive(bool const on)
{
    if (live_ == on) return;
    live_ = on;
    emit liveChanged();
}

void GlassSheet::publish(QRect const     region,
                         QRect const     screen,
                         QRect const     window,
                         double const    fillAlpha,
                         QImage          picture,
                         ToneRead const& read,
                         quint64         fingerprint,
                         bool const      fresh,
                         bool const      duplicated)
{
    // A read that failed leaves nothing to show: drop the sheet rather than hold one that no longer matches the window.
    if (picture.isNull()) {
        clear();
        return;
    }
    // A duplicated frame is gated by the desktop's dirty and move rectangles before it reaches here. Windows Graphics
    // Capture has neither, so that source takes the fingerprint on the worker and hands it over; the sheet compares it
    // with the picture in force, which costs nothing on the GUI thread. The window's place and the fill are part of the
    // comparison: the text tone is judged over the window, not over the whole sheet.
    auto print = fingerprint;
    if (print == 0 and not duplicated) print = pictureFingerprint(picture);
    {
        QMutexLocker const lock(&mutex_);
        if (not fresh and region == sheet_ and print != 0 and print == fingerprint_ and window == publishedWindow_ and fillAlpha == publishedFillAlpha_) {
            glassStats().countIdenticalSkip();
            return;
        }
    }
    auto const settled = lightText();
    // A sheet just taken has no tone in force for its backdrop, so one read decides it; the ~15/s refresh keeps
    // the tone the reader has been reading unless the other one reads decisively better, on kToneHold reads running.
    auto const tone          = settleTone(read, settled, toneStreak_, fresh ? 1 : kToneHold);
    auto const sourceChanged = duplicated_ != duplicated;
    duplicated_              = duplicated;
    {
        QMutexLocker const lock(&mutex_);
        image_              = std::move(picture);
        lightText_          = tone;
        sheet_              = region;
        sheetScreen_        = screen;
        fingerprint_        = print;
        publishedWindow_    = window;
        publishedFillAlpha_ = fillAlpha;
        ++serial_;
        ++publishes_;
    }
    if (sourceChanged) emit captureChanged();
    // Counted outside the sheet's lock: the tally has its own, and the two are never held at once.
    glassStats().countPublish();
    // The two contrasts on their own say why the tone moved: a pair within a few percent of each other is the
    // sample sitting on the crossover, a pair far apart that swaps over is the sample swinging (docs/adr/0041).
    if (fresh or tone != settled)
        LENS_INFO("the {} card reads {} text: dark {:.2f}, light {:.2f}", name_.toStdString(), tone ? "light" : "dark", read.againstDark, read.againstLight);
    emit pictureChanged();
}

void GlassSheet::noteDuplicationRefused(bool const refused)
{
    if (duplicationRefused_ == refused) return;
    duplicationRefused_ = refused;
    emit captureChanged();
}

bool GlassSheet::keepOutOfCapture(QObject* window, bool const excluded)
{
    // Only Windows has the exclusion, and a window without a native handle has nothing to set it on: asking for one
    // would create the handle, so a hidden window is left alone.
    auto* const w = qobject_cast<QWindow*>(window);
    if (QGuiApplication::platformName() != QLatin1String("windows") or w == nullptr or w->handle() == nullptr)
        return false;
    auto const set = setExcludedFromCapture(*w, excluded);
    if (excluded and not set)
        LENS_WARN("the {} window can appear in screen captures: Windows refused the exclusion, so the glass hides the card instead",
                  name_.toStdString());
    auto const kept = set and excluded;
    if (kept != keptOut_) {
        keptOut_ = kept;
        if (kept)
            LENS_INFO("the {} window is kept out of screen captures while a glass theme shows", name_.toStdString());
        else
            LENS_INFO("the {} window is back in screen captures", name_.toStdString());
    }
    return kept;
}

void GlassSheet::clear()
{
    setLive(false);
    ++generation_;
    // Nothing is held, so nothing is in force: the next sheet's tone is judged from scratch.
    toneStreak_              = 0;
    auto const sourceChanged = duplicated_ or duplicationRefused_;
    duplicated_              = false;
    duplicationRefused_      = false;
    {
        QMutexLocker const lock(&mutex_);
        if (image_.isNull() and sheet_.isEmpty()) {
            if (sourceChanged) emit captureChanged();
            return;
        }
        image_              = {};
        lightText_          = false;
        sheet_              = {};
        sheetScreen_        = {};
        fingerprint_        = 0;
        publishedWindow_    = {};
        publishedFillAlpha_ = 0.0;
        ++serial_;
    }
    if (sourceChanged) emit captureChanged();
    emit pictureChanged();
}

GlassBackdrop::GlassBackdrop(QObject* parent)
    : QObject(parent), gdi_(QGuiApplication::platformName() == QLatin1String("windows"))
{
    sheets_.reserve(lens::core::surface::sheetCount());
    for (lens::core::surface::Row const& row : lens::core::surface::kRows) {
        if (not row.sheet)
            continue;
        auto* const sheet = new GlassSheet(QString::fromLatin1(row.name.data(), row.name.size()), this);
        connect(sheet, &GlassSheet::liveChanged, this, &GlassBackdrop::updateLive);
        sheets_.push_back(sheet);
    }
    live_.setInterval(kLiveIntervalMs);
    // A 6 ms tick on a 144 Hz screen is shorter than the coarse timer's own slack, and the poll has to land before the
    // next composition rather than a few milliseconds after it.
    live_.setTimerType(Qt::PreciseTimer);
    connect(&live_, &QTimer::timeout, this, &GlassBackdrop::refreshNext);
    pool_.setMaxThreadCount(1);
    window_.start();
}

GlassBackdrop::~GlassBackdrop()
{
    // A read still on the pool posts its result back to this object, so let it finish before the object goes.
    pool_.waitForDone();
    // A capture session belongs to the apartment of the thread that opened it, so it is closed there rather than in
    // this thread's destructor.
    for (auto& session : duplications_) {
        auto* const capture = session.capture.get();
        pool_.start([capture] { capture->close(); });
    }
    pool_.waitForDone();
}

GlassSheet* GlassBackdrop::surface(QString const& name) const
{
    auto const found = std::ranges::find_if(sheets_, [&name](GlassSheet const* sheet) { return sheet->name() == name; });
    return found == sheets_.end() ? nullptr : *found;
}

void GlassBackdrop::setQuality(GlassQuality const quality)
{
    if (quality_ == quality) return;
    quality_ = quality;
    LENS_INFO("glass quality set to {}", glassQualityName(quality));
    // Eco stops the timer, and the other tiers restart it at their own pace; the picture in force stays on screen.
    updateLive();
}

GlassQuality GlassBackdrop::quality() const
{
    return quality_;
}

void GlassBackdrop::refreshNext()
{
    // Eco stopped the timer; a timeout already queued still lands here and has nothing to do.
    if (not glassQualityRefreshes(quality_)) return;
    glassStats().countTick();
    if (window_.hasExpired(kSummaryIntervalMs)) {
        // Logged on the tick, so a window is only ever reported while the refresh is actually running.
        auto const line = glassStats().takeSummaryLine(static_cast<int>(window_.elapsed() / 1000));
        if (not line.isEmpty()) LENS_INFO("{}", line.toStdString());
        window_.restart();
    }
    // One read at a time across every surface: a tick that finds the last still running is skipped, not queued behind it.
    if (busy_) {
        // A chained read is meant to be in flight; counting each tick of it as a skip would drown the tally.
        if (not chaining_) glassStats().countBusySkip();
        return;
    }
    // Standard is the GDI tier: it neither asks a screen to duplicate nor reads faster for one that does.
    auto const duplicates = QGuiApplication::platformName() == QLatin1String("windows") and glassQualityFollows(quality_);
    // The GDI budget is shared by every screen that has no duplication: a fast tick for another screen must not turn a
    // GDI read into a sixty-a-second one.
    auto const gdiDue = not gdiPace_.isValid() or gdiPace_.elapsed() >= kLiveIntervalMs;
    auto       live   = std::vector<bool>{};
    live.reserve(sheets_.size());
    for (auto* const sheet : sheets_) {
        // A sheet whose dragged window is being retaken is skipped for this tick; its deferred read is what the next
        // picture will be, and reading the old region again would only land after it.
        auto const candidate = sheet->live_ and not sheet->reading();
        auto       fast      = false;
        if (candidate and duplicates) {
            if (auto* const screen = QGuiApplication::screenAt(sheet->sheet_.center()); screen != nullptr)
                fast = sessionFor(*screen).chain->canFollow();
        }
        live.push_back(candidate and (fast or gdiDue));
    }
    auto const next = nextLive(live, cursor_);
    if (not next) {
        updateLive();
        return;
    }
    cursor_           = *next;
    auto* const sheet = sheets_[*next];

    // Only a live sheet is served from the follow source. A sheet just taken, or a drag's deferred retake, still reads
    // through GDI: that path is on the GUI thread or already timed, and its one read is what puts the first picture up.
    DesktopSource* source = &gdi_;
    if (duplicates) {
        if (auto* const screen = QGuiApplication::screenAt(sheet->sheet_.center()); screen != nullptr)
            source = sessionFor(*screen).chain.get();
    }
    auto const read = requestFor(sheet->sheet_, sheet->window_, sheet->fillAlpha_, source);
    if (not read) {
        sheet->clear();
        return;
    }
    // The read, and the chain that follows it. A single live sheet on Smooth reads the next frame from the worker rather
    // than from the next GUI tick: a timer's jitter coalesces away frames produced between two ticks, and that tier is
    // for the display's rate, not the timer's (docs/adr/0074). The chain lives in this one std::function, held strongly
    // by the task in flight and weakly by its own body, so it ends when a read decides not to continue and leaves
    // nothing behind.
    using StartRead    = std::function<void(std::size_t, int, Request const&)>;
    auto const starter = std::make_shared<StartRead>();
    auto const weak    = std::weak_ptr<StartRead>{starter};
    *starter           = [this, weak](std::size_t const index, int const generation, Request const& request) {
        busy_ = true;
        pool_.start([this, request, index, generation, starter = weak.lock()] {
            auto taken = takeSheet(request);
            QMetaObject::invokeMethod(
                this,
                [this, request, index, generation, taken = std::move(taken), starter]() mutable {
                    busy_ = false;
                    // The GDI clock starts when the read finishes, which is also when the next one may start: the read itself
                    // is the cost the fifteen-a-second budget is spacing out.
                    if (taken.usedGdi) gdiPace_.restart();
                    // Retaken or dropped while this read ran: the picture is of a region that is no longer held.
                    auto* const target = sheets_[index];
                    if (generation == target->generation_) {
                        // A GDI-only read never tried a live source, so it must not clear a refusal the reader has
                        // already been told about; a chain that skipped or lost its session did try.
                        if (request.source->canFollow() or taken.refused)
                            target->noteDuplicationRefused(taken.refused);
                        if (not taken.noFrame) {
                            if (taken.picture.isNull())
                                target->clear();
                            else
                                target->publish(request.region,
                                                request.screen,
                                                request.window,
                                                request.fillAlpha,
                                                std::move(taken.picture),
                                                taken.tone,
                                                taken.fingerprint,
                                                false,
                                                not taken.usedGdi);
                        }
                    }
                    // A session that failed on this read (another recorder took the output, a display mode changed) must
                    // drop the pace back to the GDI budget before the next tick.
                    updateLive();
                    // Continue the chain only while it is still the same live sheet, alone on a screen that can follow. A
                    // second live surface, a retake, or another tier hands the pace back to the timer.
                    if (quality_ != GlassQuality::Smooth or not target->live_ or target->reading() or target->generation_ != generation) {
                        chaining_ = false;
                        return;
                    }
                    auto const liveCount = std::count_if(sheets_.begin(), sheets_.end(), [](GlassSheet const* sheet) { return sheet->live_; });
                    if (liveCount != 1) {
                        chaining_ = false;
                        return;
                    }
                    auto* const screen = QGuiApplication::screenAt(target->sheet_.center());
                    if (screen == nullptr or not sessionFor(*screen).chain->canFollow()) {
                        chaining_ = false;
                        return;
                    }
                    auto next = requestFor(target->sheet_, target->window_, target->fillAlpha_, sessionFor(*screen).chain.get());
                    if (not next or not starter) {
                        chaining_ = false;
                        return;
                    }
                    next->acquireTimeoutMs = kChainWaitMs;
                    chaining_              = true;
                    (*starter)(index, generation, *next);
                },
                Qt::QueuedConnection);
        });
    };
    (*starter)(*next, sheet->generation_, *read);
}

void GlassBackdrop::updateLive()
{
    // Eco keeps the picture on screen and lets the desktop under it go stale: the one tier that does no live work.
    if (not glassQualityRefreshes(quality_)) {
        live_.stop();
        return;
    }
    auto const onDesktop  = QGuiApplication::platformName() == QLatin1String("windows");
    auto const duplicates = onDesktop and glassQualityFollows(quality_);
    auto       anyLive    = false;
    auto       anyFast    = false;
    auto       fastestHz  = 0.0;
    for (auto* const sheet : sheets_) {
        if (not sheet->live_) continue;
        anyLive = true;
        if (not duplicates) continue;
        if (auto* const screen = QGuiApplication::screenAt(sheet->sheet_.center()); screen != nullptr) {
            if (sessionFor(*screen).chain->canFollow()) {
                anyFast = true;
                // Not std::max: windows.h's max macro is in scope in this file.
                if (screen->refreshRate() > fastestHz) fastestHz = screen->refreshRate();
            }
        }
    }
    if (not anyLive) {
        live_.stop();
        return;
    }
    // Automatic keeps the conservative sixty-a-second cap; Smooth is the tier that spends the extra reads to follow the
    // screen (docs/adr/0074). A screen without a live source, or a Standard tier, keeps the shared GDI budget.
    auto const interval = not anyFast ? kLiveIntervalMs : glassQualityFrameMs(quality_, fastestHz);
    if (live_.interval() != interval) {
        live_.setInterval(interval);
        if (anyFast) {
            if (quality_ == GlassQuality::Smooth and fastestHz > 0.0)
                LENS_INFO("glass refresh on the smooth tier paced at {} ms: the {} Hz screen sets it", interval, qRound(fastestHz));
            else
                LENS_INFO("glass refresh on the {} tier paced at {} ms", glassQualityName(quality_), interval);
        }
    }
    if (not live_.isActive()) live_.start();
    if (not screensLogged_) {
        // Once per process, and only now: the glass is what reads the desktop, so the screens it will find are worth
        // naming when it first goes to work (src/app/glass/glass_capability.h).
        screensLogged_ = true;
        for (auto const& line : glassScreenLines())
            LENS_INFO("{}", line.toStdString());
    }
}

GlassBackdrop::Session& GlassBackdrop::sessionFor(QScreen const& screen)
{
    // The native rectangle identifies the screen: a monitor's name is its model, and two of one model share it, while
    // the rectangle is where this screen actually is on the desktop -- in the orientation the sheet is cut in.
    auto const scale  = screen.devicePixelRatio();
    auto const native = QRect(nativeOriginOf(screen),
                              QSize(qRound(screen.geometry().width() * scale), qRound(screen.geometry().height() * scale)));
    for (auto& session : duplications_)
        if (session.desktop == native) return session;
    auto monitor = quintptr{0};
#ifdef _WIN32
    if (auto const* const windows = screen.nativeInterface<QNativeInterface::QWindowsScreen>())
        monitor = reinterpret_cast<quintptr>(windows->handle());
#endif
    duplications_.push_back(Session{native,
                                    std::make_unique<GlassDuplication>(screen.name(), native),
                                    std::make_unique<GlassCapture>(screen.name(), native, monitor),
                                    nullptr});
    auto& session = duplications_.back();
    session.chain = std::make_unique<FallbackSource>(
        std::initializer_list<DesktopSource*>{session.duplication.get(), session.capture.get(), &gdi_});
    return session;
}

GlassImageProvider::GlassImageProvider(GlassBackdrop const& backdrop)
    : QQuickImageProvider(QQuickImageProvider::Image), backdrop_(backdrop)
{
}

QImage GlassImageProvider::requestImage(QString const& id, QSize* size, QSize const& /*requestedSize*/)
{
    QElapsedTimer started;
    started.start();
    // The id is the surface, the path and then the query, as QML hands it over: "tray/map?w=380&h=480&r=18". The sheet
    // itself does not come through here: GlassSheetItem uploads it in place (docs/adr/0071).
    auto const parts = id.split(QLatin1Char('?'));
    auto const route = parts.value(0).split(QLatin1Char('/'));
    auto const query = QUrlQuery(parts.value(1));
    auto const value = [&query](QString const& key) { return query.queryItemValue(key).toDouble(); };

    auto const* const sheet   = backdrop_.surface(route.value(0));
    auto const        picture = sheet != nullptr ? sheet->picture() : QImage{};
    auto const        path    = route.value(1);
    auto              image   = QImage{};
    if (path == QLatin1String("map") and not picture.isNull()) {
        // The card's size is given in device-independent pixels, as QML knows it; the map is built in device pixels.
        auto const scale = picture.devicePixelRatioF();
        auto const card  = QSize(qRound(value(QStringLiteral("w")) * scale), qRound(value(QStringLiteral("h")) * scale));
        image            = cachedMap(card, value(QStringLiteral("r")), scale);
    }
    // Nothing taken yet is a transparent pixel, not a failed load: the glass then shows nothing.
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
    }
    glassStats().addRequest(static_cast<double>(started.nsecsElapsed()) / 1.0e6);
    if (size != nullptr) *size = image.size();
    return image;
}

QImage GlassImageProvider::cachedMap(QSize const size, double const radius, double const scale)
{
    QMutexLocker const lock(&mapMutex_);
    auto const         key = QStringLiteral("%1x%2 r%3 s%4").arg(size.width()).arg(size.height()).arg(radius).arg(scale);
    if (key != mapKey_) {
        map_    = refractionMap(size, radius, scale);
        mapKey_ = key;
    }
    return map_;
}

} // namespace lens::app
