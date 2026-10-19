/**
 * @file glass_backdrop.cpp
 * @brief The glass sheets, their shared reader, and the image provider that serves them (see glass_backdrop.h).
 */

#include "glass_backdrop.h"

#include "glass_capability.h"
#include "glass_desktop.h"
#include "glass_duplication.h"
#include "glass_math.h"
#include "glass_stats.h"
#include "pet/pet_window_native.h"
#include "util/log.h"

#include <QGuiApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScreen>
#include <QUrlQuery>
#include <QWindow>

#include <algorithm>
#include <array>
#include <optional>

#ifdef _WIN32
#include <windows.h>

#include <QtGui/qscreen_platform.h>
#endif

namespace lens::app {

namespace {

/// The glass surfaces, by the name QML gives them in `image://glass/<name>/…`. A fixed set: each one gets its sheet at
/// construction, so the image provider never has a sheet to create on one of its own threads.
constexpr std::array<char const*, 15> kSurfaces{
    "settings",
    "tray",
    "stats",
    "words",
    "yearWords",
    "cost",
    "bubble",
    "selectionBar",
    "notice",
    "update",
    "removal",
    // The settings preview's card is a second Bubble window rather than the bubble itself, so it
    // takes a sheet of its own: two windows sharing one sheet would take the backdrop of whichever
    // followed last, and the live refresh would flip the two between them.
    "preview",
    // The theme dropdown's preview is another transient window beside the settings panel, and it
    // has to show glass whichever theme is stored, so it takes a sheet too.
    "themePreview",
    // The vocabulary level dropdown's card is a third one: a separate window, and one sheet holds
    // one backdrop.
    "vocabPreview",
    // The action bar style dropdown's card is the fourth: it follows the stored theme, but its
    // miniature bar is still a separate window and needs its own sheet.
    "selectionBarPreview",
};
/// How far the sheet reaches past the window on each side, in DIP. It is the reserve plus the hysteresis between
/// retakes: the shader blurs and bends within the picture, so the sheet no longer has to carry the blur's reach
/// (docs/adr/0070). Keeping it near kSheetReserve is worth about a fifth of every desktop read, which is priced by area
/// (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.6).
constexpr int kSheetMargin = 112;
/// How near the window may come to a side of the sheet that is not on the screen's edge before a new sheet is taken, in
/// DIP. The old sheet serves while the new one processes, so this covers the distance a drag moves in that time.
constexpr int kSheetReserve = 72;
/// The refresh period a GDI screen keeps: about fifteen reads a second in all, however many sheets are live. A screen
/// that duplicates does not use this budget, but it no longer raises a GDI read's rate either (docs/adr/0072).
// ponytail: fixed rate, no back-off when the machine is busy; add one when the reads show up in someone's frame times.
constexpr int kLiveIntervalMs = 67;
/// The tick a duplicated screen gets: about sixty polls a second. A poll that finds no new frame costs an acquire and
/// nothing else, so a still desktop stays nearly free; the frame rate cannot exceed the desktop's own change rate.
constexpr int kLiveFrameMs = 16;
/// The sheet is read at the screen's own resolution. The blur runs on the GPU now (docs/adr/0070), and reading full costs
/// only about 3.5 ms more than a quarter (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.5):
/// the quarter existed to keep the CPU blur's input small, and with that gone it would only cost sharpness.
constexpr auto kSheetScale = SheetScale::Full;
/// How often the pipeline's tally is logged. Ten seconds is long enough for the read's median to mean something on a
/// still desktop, and short enough that a run of the application shows several windows.
constexpr int kSummaryIntervalMs = 10000;

/// What a sheet read needs, copied out of Qt's objects on the GUI thread so a refresh on the pool never touches them.
struct Request {
    QRect             region;                ///< The sheet, in device-independent pixels.
    QRect             screen;                ///< The geometry of the screen the sheet lies on.
    QRect             native;                ///< The sheet in device pixels, on the desktop.
    QRect             window;                ///< The card the text tone is judged over.
    double            scale       = 1.0;     ///< Device pixels per device-independent pixel.
    double            fillAlpha   = 0.0;     ///< The white the card sits under, 0 to 1.
    bool              onDesktop   = false;   ///< Whether this platform is the real Windows desktop, the only one to read.
    GlassDuplication* duplication = nullptr; ///< The screen's frame source, when a live sheet can use one.
};

/// A picture of the desktop and the text tone read over the card.
struct Taken {
    QImage   picture;
    ToneRead tone;
    bool     noFrame = false; ///< The duplicated output had no new frame: nothing changed, nothing to publish.
    bool     usedGdi = false; ///< The read went to GDI, so the shared fifteen-a-second budget has been spent.
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
std::optional<Request> requestFor(QRect const region, QRect const window, double const fillAlpha, GlassDuplication* const duplication = nullptr)
{
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) return std::nullopt;
    auto const scale     = screen->devicePixelRatio();
    auto const onDesktop = QGuiApplication::platformName() == QLatin1String("windows");
    return Request{
        .region      = region,
        .screen      = screen->geometry(),
        .native      = toNativeRect(region, screen->geometry(), nativeOriginOf(*screen), scale),
        .window      = window,
        .scale       = scale,
        .fillAlpha   = fillAlpha,
        .onDesktop   = onDesktop,
        .duplication = onDesktop ? duplication : nullptr,
    };
}

/// The sheet's picture and the text tone over the card. Touches no Qt object, so it runs on the pool.
Taken takeSheet(Request const& request)
{
    auto picture = QImage{};
    auto noFrame = false;
    auto usedGdi = false;
    if (request.duplication != nullptr) {
        QElapsedTimer started;
        started.start();
        auto frame = request.duplication->read(request.native, request.scale);
        if (frame.newFrame and not frame.image.isNull()) {
            // Timed separately from GDI: this is the copy the duplication path replaces a screen read with, and the two
            // are what the frame-rate decision is made of (src/app/glass_stats.h).
            glassStats().addFrame(static_cast<double>(started.nsecsElapsed()) / 1.0e6);
            picture = std::move(frame.image);
        } else if (not frame.newFrame and frame.usable) {
            // Nothing changed behind the card: the picture in force is still the desktop. Not a failed read, and not a
            // reason to spend the GDI budget.
            noFrame = true;
            glassStats().countFrameSkip();
        }
        // A session that is not usable now, or a frame whose copy failed, falls through to GDI.
    }
    if (picture.isNull() and not noFrame) {
        QElapsedTimer started;
        started.start();
        picture = readDesktopRegion(request.native, kSheetScale, request.onDesktop);
        // Timed here and not around this whole function: the tone read that follows is a few hundred pixels, and the
        // question is what one desktop read costs (src/app/glass_stats.h).
        glassStats().addRead(static_cast<double>(started.nsecsElapsed()) / 1.0e6);
        usedGdi = true;
    }
    if (picture.isNull()) return {{}, {}, noFrame, usedGdi};
    picture.setDevicePixelRatio(request.scale);
    // The text is judged over the window, not over the margin the sheet adds around it.
    auto const scale = picture.devicePixelRatioF();
    auto const card  = QRect(qRound((request.window.x() - request.region.x()) * scale),
                            qRound((request.window.y() - request.region.y()) * scale),
                            qRound(request.window.width() * scale),
                            qRound(request.window.height() * scale));
    auto const tone  = readTone(picture.copy(card.intersected(picture.rect())), request.fillAlpha);
    return {std::move(picture), tone, noFrame, usedGdi};
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
    auto const request = requestFor(region, window, fillAlpha);
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
                publish(request.region, request.screen, std::move(taken.picture), taken.tone, true);
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
    auto const request = requestFor(region, window, fillAlpha);
    if (not request) {
        clear();
        return;
    }
    // A refresh still reading the sheet before this one is now stale, and is dropped when it lands.
    ++generation_;
    auto taken = takeSheet(*request);
    publish(request->region, request->screen, std::move(taken.picture), taken.tone, true);
}

void GlassSheet::setLive(bool const on)
{
    if (live_ == on) return;
    live_ = on;
    emit liveChanged();
}

void GlassSheet::publish(QRect const region, QRect const screen, QImage picture, ToneRead const& read, bool const fresh)
{
    // A read that failed leaves nothing to show: drop the sheet rather than hold one that no longer matches the window.
    if (picture.isNull()) {
        clear();
        return;
    }
    // The desktop has not moved since the last refresh: the same sheet over the same region is already in force, and
    // publishing it again would bump the serial for a card that would look identical -- a new texture upload and
    // render for no visible change. Checked before settleTone() so that a skipped refresh leaves the tone bookkeeping
    // untouched.
    auto const print = pictureFingerprint(picture);
    {
        QMutexLocker const lock(&mutex_);
        if (not fresh and region == sheet_ and print == fingerprint_) {
            glassStats().countIdenticalSkip();
            return;
        }
    }
    auto const settled = lightText();
    // A sheet just taken has no tone in force for its backdrop, so one read decides it; the ~15/s refresh keeps
    // the tone the reader has been reading unless the other one reads decisively better, on kToneHold reads running.
    auto const tone = settleTone(read, settled, toneStreak_, fresh ? 1 : kToneHold);
    {
        QMutexLocker const lock(&mutex_);
        image_       = std::move(picture);
        lightText_   = tone;
        sheet_       = region;
        sheetScreen_ = screen;
        fingerprint_ = print;
        ++serial_;
        ++publishes_;
    }
    // Counted outside the sheet's lock: the tally has its own, and the two are never held at once.
    glassStats().countPublish();
    // The two contrasts on their own say why the tone moved: a pair within a few percent of each other is the
    // sample sitting on the crossover, a pair far apart that swaps over is the sample swinging (docs/adr/0041).
    if (fresh or tone != settled)
        LENS_INFO("the {} card reads {} text: dark {:.2f}, light {:.2f}", name_.toStdString(), tone ? "light" : "dark", read.againstDark, read.againstLight);
    emit pictureChanged();
}

bool GlassSheet::keepOutOfCapture(QObject* window, bool const excluded)
{
    // Only Windows has the exclusion, and a window without a native handle has nothing to set it on: asking for one
    // would create the handle, so a hidden window is left alone.
    auto* const w = qobject_cast<QWindow*>(window);
    if (QGuiApplication::platformName() != QLatin1String("windows") or w == nullptr or w->handle() == nullptr)
        return false;
    auto const set = pet::setExcludedFromCapture(*w, excluded);
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
    toneStreak_ = 0;
    {
        QMutexLocker const lock(&mutex_);
        if (image_.isNull() and sheet_.isEmpty()) return;
        image_       = {};
        lightText_   = false;
        sheet_       = {};
        sheetScreen_ = {};
        fingerprint_ = 0;
        ++serial_;
    }
    emit pictureChanged();
}

GlassBackdrop::GlassBackdrop(QObject* parent)
    : QObject(parent)
{
    sheets_.reserve(kSurfaces.size());
    for (auto const* const name : kSurfaces) {
        auto* const sheet = new GlassSheet(QString::fromLatin1(name), this);
        connect(sheet, &GlassSheet::liveChanged, this, &GlassBackdrop::updateLive);
        sheets_.push_back(sheet);
    }
    live_.setInterval(kLiveIntervalMs);
    connect(&live_, &QTimer::timeout, this, &GlassBackdrop::refreshNext);
    pool_.setMaxThreadCount(1);
    window_.start();
}

GlassBackdrop::~GlassBackdrop()
{
    // A read still on the pool posts its result back to this object, so let it finish before the object goes.
    pool_.waitForDone();
}

GlassSheet* GlassBackdrop::surface(QString const& name) const
{
    auto const found = std::ranges::find_if(sheets_, [&name](GlassSheet const* sheet) { return sheet->name() == name; });
    return found == sheets_.end() ? nullptr : *found;
}

void GlassBackdrop::refreshNext()
{
    glassStats().countTick();
    if (window_.hasExpired(kSummaryIntervalMs)) {
        // Logged on the tick, so a window is only ever reported while the refresh is actually running.
        auto const line = glassStats().takeSummaryLine(static_cast<int>(window_.elapsed() / 1000));
        if (not line.isEmpty()) LENS_INFO("{}", line.toStdString());
        window_.restart();
    }
    // One read at a time across every surface: a tick that finds the last still running is skipped, not queued behind it.
    if (busy_) {
        glassStats().countBusySkip();
        return;
    }
    auto const onDesktop = QGuiApplication::platformName() == QLatin1String("windows");
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
        if (candidate and onDesktop) {
            if (auto* const screen = QGuiApplication::screenAt(sheet->sheet_.center()); screen != nullptr)
                fast = duplicationFor(*screen)->usable();
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

    // Only a live sheet is served from a duplicated frame. A sheet just taken, or a drag's deferred retake, still reads
    // through GDI: that path is on the GUI thread or already timed, and its one read is what puts the first picture up.
    GlassDuplication* duplication = nullptr;
    if (onDesktop) {
        if (auto* const screen = QGuiApplication::screenAt(sheet->sheet_.center()); screen != nullptr) {
            auto* const session = duplicationFor(*screen);
            if (session->usable()) duplication = session;
        }
    }
    auto const read = requestFor(sheet->sheet_, sheet->window_, sheet->fillAlpha_, duplication);
    if (not read) {
        sheet->clear();
        return;
    }
    busy_ = true;
    pool_.start([this, request = *read, index = *next, generation = sheet->generation_] {
        auto taken = takeSheet(request);
        QMetaObject::invokeMethod(
            this,
            [this, request, index, generation, taken = std::move(taken)]() mutable {
                busy_ = false;
                // The GDI clock starts when the read finishes, which is also when the next one may start: the read itself
                // is the cost the fifteen-a-second budget is spacing out.
                if (taken.usedGdi) gdiPace_.restart();
                // Retaken or dropped while this read ran: the picture is of a region that is no longer held.
                auto* const target = sheets_[index];
                if (generation == target->generation_ and not taken.noFrame) {
                    if (taken.picture.isNull())
                        target->clear();
                    else
                        target->publish(request.region, request.screen, std::move(taken.picture), taken.tone, false);
                }
                // A session that failed on this read (another recorder took the output, a display mode changed) must
                // drop the pace back to the GDI budget before the next tick.
                updateLive();
            },
            Qt::QueuedConnection);
    });
}

void GlassBackdrop::updateLive()
{
    auto const onDesktop = QGuiApplication::platformName() == QLatin1String("windows");
    auto       anyLive   = false;
    auto       anyFast   = false;
    for (auto* const sheet : sheets_) {
        if (not sheet->live_) continue;
        anyLive = true;
        if (anyFast or not onDesktop) continue;
        if (auto* const screen = QGuiApplication::screenAt(sheet->sheet_.center()); screen != nullptr)
            anyFast = duplicationFor(*screen)->usable();
    }
    if (not anyLive) {
        live_.stop();
        return;
    }
    auto const interval = anyFast ? kLiveFrameMs : kLiveIntervalMs;
    if (live_.interval() != interval) live_.setInterval(interval);
    if (not live_.isActive()) live_.start();
    if (not screensLogged_) {
        // Once per process, and only now: the glass is what reads the desktop, so the screens it will find are worth
        // naming when it first goes to work (src/app/glass_capability.h).
        screensLogged_ = true;
        for (auto const& line : glassScreenLines())
            LENS_INFO("{}", line.toStdString());
    }
}

GlassDuplication* GlassBackdrop::duplicationFor(QScreen const& screen)
{
    // The native rectangle identifies the screen: a monitor's name is its model, and two of one model share it, while
    // the rectangle is where this screen actually is on the desktop -- in the orientation the sheet is cut in.
    auto const scale  = screen.devicePixelRatio();
    auto const native = QRect(nativeOriginOf(screen),
                              QSize(qRound(screen.geometry().width() * scale), qRound(screen.geometry().height() * scale)));
    for (auto const& session : duplications_)
        if (session.desktop == native) return session.open.get();
    duplications_.push_back(Session{native, std::make_unique<GlassDuplication>(screen.name(), native)});
    return duplications_.back().open.get();
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
