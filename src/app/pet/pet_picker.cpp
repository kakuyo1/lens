#include "app/pet/pet_picker.h"

#include <utility>

#include "app/pet/pet_controller.h"

namespace lens::app::pet {

PetPicker::PetPicker(PetController& owner, QObject* parent)
    : QObject(parent), owner_(owner)
{
    connect(&owner_, &PetController::catalogChanged, this, &PetPicker::catalogChanged);
    // The choice travels with the settings: a switch lands, or falls back, through the same signal.
    connect(&owner_, &PetController::settingsChanged, this, &PetPicker::petChanged);
    connect(&owner_, &PetController::confirmationRequested, this, &PetPicker::confirmationRequested);
}

QVariantList PetPicker::pets() const
{
    return owner_.pets();
}

QVariantList PetPicker::refusals() const
{
    return owner_.refusals();
}

QString PetPicker::pet() const
{
    return owner_.pet();
}

QVariantMap PetPicker::portrait(QString id)
{
    return owner_.portrait(std::move(id));
}

void PetPicker::setPet(QString id)
{
    owner_.setPet(std::move(id));
}

void PetPicker::confirmPet(QString id)
{
    owner_.confirmPet(std::move(id));
}

void PetPicker::refresh()
{
    owner_.refresh();
}

void PetPicker::openPacksFolder()
{
    owner_.openPacksFolder();
}

}
