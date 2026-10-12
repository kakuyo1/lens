#pragma once

/**
 * @file glass_backdrop.h
 * @brief The desktop picture the glass cards are drawn over, and the image provider that hands it to QML.
 *
 * The picture is kept in memory only: it is never written to disk, never logged, and dropped when the
 * glass themes are off (docs/adr/0034, docs/adr/0035). QML reads it through `image://glass/backdrop`, and
 * the liquid theme's displacement map through `image://glass/map`. Both are images, not QML types, so the
 * surfaces need no item class of their own.
 */

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QRect>
#include <QtQml/qqmlregistration.h>

namespace lens::app {

class GlassBackdrop : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.glassBackdrop")
    /// Whether the text over this picture should be light. Decided on every capture.
    Q_PROPERTY(bool lightText READ lightText NOTIFY pictureChanged)
    /// Changes with every capture, so QML's image cache never serves an earlier picture.
    Q_PROPERTY(int serial READ serial NOTIFY pictureChanged)

public:
    explicit GlassBackdrop(QObject* parent = nullptr);

    bool lightText() const;
    int  serial() const;

    /// @brief Take a picture of the desktop under @p region, in the device-independent coordinates QScreen uses.
    /// @param fillAlpha The white the card sits under, 0 to 1; the text tone is judged over it.
    /// @note A region that is empty or crosses two screens clears the picture rather than stitching one.
    Q_INVOKABLE void capture(QRect region, double fillAlpha);
    /// @brief Drop the picture.
    Q_INVOKABLE void clear();

    /// @return A copy of the picture, empty when none is held. Safe to call from the image provider's thread.
    QImage picture() const;

signals:
    void pictureChanged();

private:
    mutable QMutex mutex_;
    QImage         image_;
    bool           lightText_ = false;
    int            serial_    = 0;
};

/**
 * @brief Serves `image://glass/backdrop` and `image://glass/map?w=&h=&r=&b=`.
 *
 * Requests arrive on the image loader's worker threads, which is why the picture is copied out of
 * GlassBackdrop under its lock rather than shared.
 */
class GlassImageProvider final : public QQuickImageProvider {
public:
    explicit GlassImageProvider(GlassBackdrop const& backdrop);

    QImage requestImage(QString const& id, QSize* size, QSize const& requestedSize) override;

private:
    GlassBackdrop const& backdrop_;
};

} // namespace lens::app
