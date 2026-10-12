/**
 * @file glass_backdrop.cpp
 * @brief The desktop picture and the image provider that serves it (see glass_backdrop.h).
 */

#include "glass_backdrop.h"

#include "glass_math.h"

#include <QGuiApplication>
#include <QMutexLocker>
#include <QScreen>
#include <QUrlQuery>

namespace lens::app {

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

QImage GlassBackdrop::picture() const
{
    QMutexLocker const lock(&mutex_);
    return image_;
}

void GlassBackdrop::capture(QRect const region, double const fillAlpha)
{
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) {
        clear();
        return;
    }
    auto const local = region.translated(-screen->geometry().topLeft());
    auto       image = screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
    auto const light = not image.isNull() and prefersLightText(image, fillAlpha);
    {
        QMutexLocker const lock(&mutex_);
        image_     = std::move(image);
        lightText_ = light;
        ++serial_;
    }
    emit pictureChanged();
}

void GlassBackdrop::clear()
{
    {
        QMutexLocker const lock(&mutex_);
        if (image_.isNull()) return;
        image_     = {};
        lightText_ = false;
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
    // The id is the path and then the query, as QML hands it over: "backdrop?7" or "map?w=380&h=480&r=22&b=18".
    auto const parts = id.split(QLatin1Char('?'));
    auto const path  = parts.isEmpty() ? QString{} : parts.first();
    auto const query = QUrlQuery(parts.size() > 1 ? parts.at(1) : QString{});

    auto image = QImage{};
    if (path == QLatin1String("backdrop")) {
        image = backdrop_.picture();
    } else if (path == QLatin1String("map")) {
        image = displacementMap(QSize(query.queryItemValue(QStringLiteral("w")).toInt(),
                                      query.queryItemValue(QStringLiteral("h")).toInt()),
                                query.queryItemValue(QStringLiteral("r")).toDouble(),
                                query.queryItemValue(QStringLiteral("b")).toDouble());
    }
    // A picture that is not taken yet is a transparent pixel, not a failed load: the card is then only its fill.
    if (image.isNull() and path == QLatin1String("backdrop")) {
        image = QImage(1, 1, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
    }
    if (size != nullptr) *size = image.size();
    return image;
}

} // namespace lens::app
