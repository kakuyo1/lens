/**
 * @file glass_backdrop.cpp
 * @brief The glass sheets, their shared reader, and the image provider that serves them (see glass_backdrop.h).
 */

#include "glass_backdrop.h"

#include "glass_math.h"
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
constexpr std::array<char const*, 11> kSurfaces{
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
};
/// How far the sheet reaches past the window on each side, in DIP. The window can be dragged this far before the sheet
/// has to be taken again, and the blur and the refraction read this far around the card.
constexpr int kSheetMargin = 160;
/// How near the window may come to a side of the sheet that is not on the screen's edge before a new sheet is taken, in
/// DIP. The old sheet serves while the new one processes, so this covers the distance a drag moves in that time.
constexpr int kSheetReserve = 72;
/// The refresh period while any sheet is live: about fifteen reads a second in all, however many sheets are live.
// ponytail: fixed rate, no back-off when the machine is busy; add one when the reads show up in someone's frame times.
constexpr int kLiveIntervalMs = 67;
/// The sheet is read at a quarter of the device resolution. The blur runs on it for every request, and at full size one
/// blur takes about 40 ms of the image loader.
// ponytail: one fixed quarter, not sized to the screen; the blur softens the edge anyway.
constexpr int kSheetDownscale = 4;

/// What a sheet read needs, copied out of Qt's objects on the GUI thread so a refresh on the pool never touches them.
struct Request {
    QRect  region;            ///< The sheet, in device-independent pixels.
    QRect  screen;            ///< The geometry of the screen the sheet lies on.
    QRect  native;            ///< The sheet in device pixels, on the desktop.
    QRect  window;            ///< The card the text tone is judged over.
    double scale     = 1.0;   ///< Device pixels per device-independent pixel.
    double fillAlpha = 0.0;   ///< The white the card sits under, 0 to 1.
    bool   onDesktop = false; ///< Whether this platform is the real Windows desktop, the only one there is to read.
};

/// A picture of the desktop and the text tone read over the card.
struct Taken {
    QImage   picture;
    ToneRead tone;
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

/// The desktop under @p native, drawn into a picture a quarter of its device size. A null picture means the read failed.
QImage readDesktop(QRect const native, bool const onDesktop)
{
    auto const size = QSize(qMax(1, native.width() / kSheetDownscale), qMax(1, native.height() / kSheetDownscale));
    if (not onDesktop) {
        // A test platform has no desktop to read, and must not read the real one: a black sheet stands in for it.
        auto black = QImage(size, QImage::Format_RGB32);
        black.fill(Qt::black);
        return black;
    }
#ifdef _WIN32
    // GDI reads what is on the screen. While the glass shows, the surface's window is kept out of captures, so the read
    // gets what lies behind the card and not the card itself (docs/adr/0038).
    auto const screen            = GetDC(nullptr);
    auto const memory            = CreateCompatibleDC(screen);
    auto       info              = BITMAPINFO{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = size.width();
    info.bmiHeader.biHeight      = -size.height(); // top-down, as QImage stores its rows
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void*      bits              = nullptr;
    auto const dib               = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto       read              = QImage();
    if (dib != nullptr) {
        auto const previous = SelectObject(memory, dib);
        SetStretchBltMode(memory, HALFTONE);
        SetBrushOrgEx(memory, 0, 0, nullptr);
        StretchBlt(memory, 0, 0, size.width(), size.height(), screen, native.x(), native.y(), native.width(), native.height(), SRCCOPY);
        read = QImage(static_cast<uchar*>(bits), size.width(), size.height(), QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
        DeleteObject(dib);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return read;
#else
    return {};
#endif
}

/// The request for a sheet over @p region, or nothing when the region does not lie on one screen. GUI thread only.
std::optional<Request> requestFor(QRect const region, QRect const window, double const fillAlpha)
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
        .onDesktop = QGuiApplication::platformName() == QLatin1String("windows"),
    };
}

/// The sheet's picture and the text tone over the card. Touches no Qt object, so it runs on the pool.
Taken takeSheet(Request const& request)
{
    auto picture = readDesktop(request.native, request.onDesktop);
    picture.setDevicePixelRatio(request.scale / kSheetDownscale);
    // The text is judged over the window, not over the margin the sheet adds around it.
    auto const scale = picture.devicePixelRatioF();
    auto const card  = QRect(qRound((request.window.x() - request.region.x()) * scale),
                            qRound((request.window.y() - request.region.y()) * scale),
                            qRound(request.window.width() * scale),
                            qRound(request.window.height() * scale));
    auto const tone  = readTone(picture.copy(card.intersected(picture.rect())), request.fillAlpha);
    return {std::move(picture), tone};
}

} // namespace

GlassSheet::GlassSheet(QString name, QObject* parent)
    : QObject(parent), name_(std::move(name))
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
        ++serial_;
    }
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
    // One read at a time across every surface: a tick that finds the last still running is skipped, not queued behind it.
    if (busy_) return;
    auto live = std::vector<bool>{};
    for (auto const* const sheet : sheets_)
        live.push_back(sheet->live_);
    auto const next = nextLive(live, cursor_);
    if (not next) {
        updateLive();
        return;
    }
    cursor_           = *next;
    auto* const sheet = sheets_[*next];
    auto const  read  = requestFor(sheet->sheet_, sheet->window_, sheet->fillAlpha_);
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
                // Retaken or dropped while this read ran: the picture is of a region that is no longer held.
                auto* const target = sheets_[index];
                if (generation == target->generation_)
                    target->publish(request.region, request.screen, std::move(taken.picture), taken.tone, false);
            },
            Qt::QueuedConnection);
    });
}

void GlassBackdrop::updateLive()
{
    auto const anyLive = std::ranges::any_of(sheets_, [](GlassSheet const* sheet) { return sheet->live_; });
    if (anyLive and not live_.isActive()) live_.start();
    if (not anyLive) live_.stop();
}

GlassImageProvider::GlassImageProvider(GlassBackdrop const& backdrop)
    : QQuickImageProvider(QQuickImageProvider::Image), backdrop_(backdrop)
{
}

QImage GlassImageProvider::requestImage(QString const& id, QSize* size, QSize const& /*requestedSize*/)
{
    // The id is the surface, the path and then the query, as QML hands it over: "tray/sheet?serial=7&blur=10&saturation=120".
    auto const parts = id.split(QLatin1Char('?'));
    auto const route = parts.value(0).split(QLatin1Char('/'));
    auto const query = QUrlQuery(parts.value(1));
    auto const value = [&query](QString const& key) { return query.queryItemValue(key).toDouble(); };

    auto const* const sheet   = backdrop_.surface(route.value(0));
    auto const        picture = sheet != nullptr ? sheet->picture() : QImage{};
    auto const        path    = route.value(1);
    auto              image   = QImage{};
    if (path == QLatin1String("sheet")) {
        image = blurredPicture(picture, value(QStringLiteral("blur")), value(QStringLiteral("saturation")));
    } else if (path == QLatin1String("map") and not picture.isNull()) {
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
