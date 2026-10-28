/** @file consent_preview.h
 * @brief The one-image, id-addressed provider behind the upload confirmation card.
 */
#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace lens::app {

/// @brief Holds the frozen crop for the card and nothing else.
///
/// The reader's image is addressed by `image://ocr-consent/<requestId>`; it is never written to
/// disk, never placed in a settings projection, and never encoded into a QML property. Setting a
/// new image replaces the previous one, so at most one selection is resident.
class ConsentPreview final : public QQuickImageProvider {
public:
    ConsentPreview();

    void set(std::uint64_t requestId, QImage const& image);
    void clear(std::uint64_t requestId);

    QImage requestImage(QString const& id, QSize* size, QSize const& requested) override;

private:
    QMutex                 mutex_;
    QHash<QString, QImage> images_;
};

}
