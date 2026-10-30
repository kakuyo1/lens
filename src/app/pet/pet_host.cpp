#include "app/pet/pet_host.h"

#include "app/pet/pet_controller.h"

namespace lens::app::pet {

PetHost::PetHost(PetController& owner, QObject* parent)
    : QObject(parent), owner_(owner)
{
    connect(&owner_, &PetController::shownChanged, this, &PetHost::shownChanged);
}

bool PetHost::shown() const
{
    return owner_.shown();
}

void PetHost::attachWindow(QObject* window)
{
    owner_.attachWindow(window);
}

void PetHost::keepOutOfCapture(QObject* window)
{
    owner_.keepOutOfCapture(window);
}

QPoint PetHost::cursorPos() const
{
    return owner_.cursorPos();
}

QPoint PetHost::initialPosition(QSize size)
{
    return owner_.initialPosition(size);
}

void PetHost::fitOnScreen()
{
    owner_.fitOnScreen();
}

void PetHost::savePosition(QPoint at)
{
    owner_.savePosition(at);
}

void PetHost::setRunning(bool on)
{
    owner_.setRunning(on);
}

void PetHost::setMotionReduced(bool on)
{
    owner_.setMotionReduced(on);
}

}
