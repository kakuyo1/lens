/**
 * @file glass_sheet_item.cpp
 * @brief The live sheet texture (see glass_sheet_item.h).
 */

#include "glass_sheet_item.h"

#include "glass_backdrop.h"

#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

namespace lens::app {

GlassSheetItem::GlassSheetItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QObject* GlassSheetItem::sheetObject() const
{
    return sheet_;
}

void GlassSheetItem::setSheetObject(QObject* const sheet)
{
    auto* const typed = qobject_cast<GlassSheet*>(sheet);
    if (typed == sheet_) return;
    if (pictureConnection_) disconnect(pictureConnection_);
    sheet_ = typed;
    if (sheet_ != nullptr)
        pictureConnection_ = connect(sheet_, &GlassSheet::pictureChanged, this, &GlassSheetItem::takePicture);
    takePicture();
    emit sheetChanged();
}

bool GlassSheetItem::active() const
{
    return active_;
}

void GlassSheetItem::setActive(bool const active)
{
    if (active_ == active) return;
    active_ = active;
    if (active_) {
        takePicture();
    } else {
        {
            QMutexLocker const lock(&mutex_);
            pending_        = {};
            pendingPicture_ = true; // the next frame drops the texture the card no longer draws
        }
        update();
    }
    emit activeChanged();
}

int GlassSheetItem::pictureSerial() const
{
    return pictureSerial_;
}

void GlassSheetItem::takePicture()
{
    if (sheet_ == nullptr) return;
    // The serial moves with every publish, the drop to empty included, so a test can tell it from a reload.
    if (auto const serial = sheet_->serial(); serial != pictureSerial_) {
        pictureSerial_ = serial;
        emit pictureSerialChanged();
    }
    if (!active_) return;
    auto const picture = sheet_->picture();
    {
        QMutexLocker const lock(&mutex_);
        pending_        = picture;
        pendingPicture_ = true;
    }
    update();
}

QSGNode* GlassSheetItem::updatePaintNode(QSGNode* const oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (node != nullptr) node->setRect(boundingRect());

    QImage picture;
    {
        QMutexLocker const lock(&mutex_);
        if (!pendingPicture_) return node;
        picture         = std::move(pending_);
        pendingPicture_ = false;
    }
    if (picture.isNull() or width() <= 0 or height() <= 0) {
        delete node;
        return nullptr;
    }
    if (node == nullptr) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    // Opaque: the sheet is the desktop, and a texture upload that read the picture's fourth byte as transparency once
    // left the glass drawing nothing (docs/adr/0070).
    auto* const texture = window() != nullptr
                              ? window()->createTextureFromImage(picture, QQuickWindow::TextureIsOpaque)
                              : nullptr;
    if (texture == nullptr) {
        // Without a scene graph there is nothing to upload into. The old texture is of a picture the sheet no longer
        // holds, so it goes rather than staying on screen as a stale one.
        delete node;
        return nullptr;
    }
    node->setTexture(texture);
    node->setRect(boundingRect());
    return node;
}

} // namespace lens::app
