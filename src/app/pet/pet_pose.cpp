#include "app/pet/pet_pose.h"

#include "app/pet/pet_controller.h"

namespace lens::app::pet {

PetPose::PetPose(PetController& owner, QObject* parent)
    : QObject(parent), owner_(owner)
{
    connect(&owner_, &PetController::frameChanged, this, &PetPose::frameChanged);
    connect(&owner_, &PetController::gazeChanged, this, &PetPose::gazeChanged);
    // The figure is the loaded pet and the size the reader picked; both arrive as a settings change.
    connect(&owner_, &PetController::settingsChanged, this, &PetPose::figureChanged);
}

int PetPose::canvas() const
{
    return owner_.canvas();
}

double PetPose::scale() const
{
    return owner_.scale();
}

QVariantMap PetPose::layers() const
{
    return owner_.layers();
}

QPointF PetPose::foot() const
{
    return owner_.foot();
}

QVariantMap PetPose::reviewSign() const
{
    return owner_.reviewSign();
}

QRectF PetPose::inkBox() const
{
    return owner_.inkBox();
}

double PetPose::inkCentre() const
{
    return owner_.inkCentre();
}

QString PetPose::action() const
{
    return owner_.action();
}

int PetPose::frame() const
{
    return owner_.frameIndex();
}

int PetPose::expression() const
{
    return owner_.expression();
}

QVariantMap PetPose::sheets() const
{
    return owner_.sheets();
}

QVariantMap PetPose::anchors() const
{
    return owner_.anchors();
}

QVariantList PetPose::worn() const
{
    return owner_.wornAccessories();
}

QPoint PetPose::gaze() const
{
    return owner_.gaze();
}

QString PetPose::scene() const
{
    return owner_.scene();
}

int PetPose::sceneFrame() const
{
    return owner_.sceneFrame();
}

QVariantMap PetPose::sceneSheets() const
{
    return owner_.sceneSheets();
}

QPointF PetPose::sceneShift() const
{
    return owner_.sceneShift();
}

QPointF PetPose::sceneAlign() const
{
    return owner_.sceneAlign();
}

double PetPose::sceneScale() const
{
    return owner_.sceneScale();
}

QVariantMap PetPose::sceneLayers() const
{
    return owner_.sceneLayers();
}

QVariantMap PetPose::entry() const
{
    return owner_.sceneEntry();
}

}
