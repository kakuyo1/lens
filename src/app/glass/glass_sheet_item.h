#pragma once

/**
 * @file glass_sheet_item.h
 * @brief The live texture behind a glass card: the sheet's latest picture, uploaded on the render thread.
 */

#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace lens::app {

class GlassSheet;

/**
 * @brief A glass card's sheet as one texture that is replaced in place, not reloaded (docs/adr/0071).
 *
 * This item takes the picture a GlassSheet publishes and hands it to the scene graph on the render thread, replacing
 * the texture the shader samples; the image-loader route it replaces (`image://glass/...?serial=N`) put every publish
 * through the loader as a new URL, a new decode, a new texture and a new cache entry, fifteen times a second. The old
 * texture stays until the new one is ready, so a publish cannot blank the card, and the sheet's fingerprint gate means
 * a still desktop uploads nothing at all.
 *
 * It draws the picture itself and is sampled through a ShaderEffectSource, which also gives the sheet its mip chain for
 * the shader's blur.
 */
class GlassSheetItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(GlassSheetItem)
    /// The sheet whose pictures this item shows, from Controller.glassBackdrop.surface(name).
    Q_PROPERTY(QObject* sheet READ sheetObject WRITE setSheetObject NOTIFY sheetChanged)
    /// Whether the card draws the sheet. Off with the glass off and in passthrough: the item then holds no texture.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// The serial of the last picture the item took, so a test can tell a publish from a reload (docs/adr/0071).
    Q_PROPERTY(int pictureSerial READ pictureSerial NOTIFY pictureSerialChanged)

public:
    explicit GlassSheetItem(QQuickItem* parent = nullptr);

    QObject* sheetObject() const;
    void     setSheetObject(QObject* sheet);
    bool     active() const;
    void     setActive(bool active);
    int      pictureSerial() const;

    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

signals:
    void sheetChanged();
    void activeChanged();
    void pictureSerialChanged();

private:
    /// @brief Take the sheet's picture in force, if the card draws it, and ask for a frame to upload it.
    void takePicture();

    GlassSheet*             sheet_ = nullptr;
    QMetaObject::Connection pictureConnection_;
    /// The picture the next render frame uploads. The GUI thread writes it, the render thread takes it.
    QMutex mutex_;
    QImage pending_;
    bool   pendingPicture_ = false;
    int    pictureSerial_  = 0;
    bool   active_         = true;
};

} // namespace lens::app
