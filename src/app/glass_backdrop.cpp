/**
 * @file glass_backdrop.cpp
 * @brief The desktop sheet and the image provider that serves it (see glass_backdrop.h).
 */

#include "glass_backdrop.h"

#include "glass_math.h"
#include "pet/pet_window_native.h"
#include "util/log.h"

#include <QGuiApplication>
#include <QMutexLocker>
#include <QScreen>
#include <QUrlQuery>
#include <QWindow>

namespace lens::app {

namespace {

/// How far the sheet reaches past the window on each side, in DIP. The window can be dragged this far before the sheet
/// has to be taken again, and the blur and the refraction read this far around the card.
constexpr int kSheetMargin = 160;
/// How near the window may come to a side of the sheet that is not on the screen's edge before a new sheet is taken, in
/// DIP. The old sheet serves while the new one processes, so this covers the distance a drag moves in that time.
constexpr int kSheetReserve = 72;

} // namespace

GlassBackdrop::GlassBackdrop(QObject* parent)
    : QObject(parent)
{
}

bool GlassBackdrop::lightText() const
{
    QMutexLocker const lock(&mutex_);
    return lightText_;
}

int GlassBackdrop::serial() const
{
    QMutexLocker const lock(&mutex_);
    return serial_;
}

QRect GlassBackdrop::sheet() const
{
    QMutexLocker const lock(&mutex_);
    return sheet_;
}

QImage GlassBackdrop::picture() const
{
    QMutexLocker const lock(&mutex_);
    return image_;
}

void GlassBackdrop::follow(QRect const window, double const fillAlpha)
{
    if (covers(window)) return;
    auto* const screen = QGuiApplication::screenAt(window.center());
    if (screen == nullptr or window.isEmpty() or not screen->geometry().contains(window)) {
        clear();
        return;
    }
    auto const sheet = window.adjusted(-kSheetMargin, -kSheetMargin, kSheetMargin, kSheetMargin).intersected(screen->geometry());
    capture(sheet, window, fillAlpha);
}

bool GlassBackdrop::covers(QRect const window) const
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

void GlassBackdrop::capture(QRect const region, QRect const window, double const fillAlpha)
{
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) {
        clear();
        return;
    }
    auto const local = region.translated(-screen->geometry().topLeft());
    auto       image = screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
    // The grab is in device pixels; the glass scales its blur by this to match.
    auto const scale = screen->devicePixelRatio();
    image.setDevicePixelRatio(scale);
    // The text is judged over the window, not over the margin the sheet adds around it.
    auto const card  = QRect(qRound((window.x() - region.x()) * scale), qRound((window.y() - region.y()) * scale), qRound(window.width() * scale), qRound(window.height() * scale));
    auto const light = not image.isNull() and prefersLightText(image.copy(card.intersected(image.rect())), fillAlpha);
    {
        QMutexLocker const lock(&mutex_);
        image_       = std::move(image);
        lightText_   = light;
        sheet_       = region;
        sheetScreen_ = screen->geometry();
        ++serial_;
    }
    emit pictureChanged();
}

bool GlassBackdrop::keepOutOfCapture(QObject* window, bool const excluded)
{
    // Only Windows has the exclusion, and a window without a native handle has nothing to set it on: asking for one
    // would create the handle, so a hidden window is left alone.
    auto* const w = qobject_cast<QWindow*>(window);
    if (QGuiApplication::platformName() != QLatin1String("windows") or w == nullptr or w->handle() == nullptr)
        return false;
    auto const set = pet::setExcludedFromCapture(*w, excluded);
    if (excluded and not set)
        LENS_WARN("the settings window can appear in screen captures: Windows refused the exclusion, so the glass hides the card instead");
    auto const kept = set and excluded;
    if (kept != keptOut_) {
        keptOut_ = kept;
        if (kept)
            LENS_INFO("the settings window is kept out of screen captures while a glass theme shows");
        else
            LENS_INFO("the settings window is back in screen captures");
    }
    return kept;
}

void GlassBackdrop::clear()
{
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

GlassImageProvider::GlassImageProvider(GlassBackdrop const& backdrop)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , backdrop_(backdrop)
{
}

QImage GlassImageProvider::requestImage(QString const& id, QSize* size, QSize const& /*requestedSize*/)
{
    // The id is the path and then the query, as QML hands it over: "sheet?serial=7&blur=10&saturation=120".
    auto const parts = id.split(QLatin1Char('?'));
    auto const path  = parts.isEmpty() ? QString{} : parts.first();
    auto const query = QUrlQuery(parts.size() > 1 ? parts.at(1) : QString{});
    auto const value = [&query](QString const& key) { return query.queryItemValue(key).toDouble(); };

    auto const picture = backdrop_.picture();
    auto       image   = QImage{};
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
