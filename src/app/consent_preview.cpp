/** @file consent_preview.cpp
 * @brief The id-addressed single-image provider behind consent_preview.h.
 */
#include "consent_preview.h"

#include <QMutexLocker>

namespace lens::app {

ConsentPreview::ConsentPreview()
    : QQuickImageProvider{QQuickImageProvider::Image}
{}

void ConsentPreview::set(std::uint64_t const requestId, QImage const& image)
{
    QMutexLocker lock{&mutex_};
    images_.clear();
    images_.insert(QString::number(requestId), image);
}

void ConsentPreview::clear(std::uint64_t const requestId)
{
    QMutexLocker lock{&mutex_};
    images_.remove(QString::number(requestId));
}

QImage ConsentPreview::requestImage(QString const& id, QSize* size, QSize const& requested)
{
    QMutexLocker lock{&mutex_};
    auto const   found = images_.constFind(id);
    if (found == images_.cend()) return {};
    QImage image = *found;
    if (requested.isValid()) image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size) *size = image.size();
    return image;
}

}
