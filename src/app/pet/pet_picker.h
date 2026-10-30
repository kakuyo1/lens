#pragma once

/**
 * @file pet_picker.h
 * @brief The pet picker's one entry: the catalogue, the reader's choice, and the package actions.
 *
 * The settings page's type dropdown, its postcards and its package block are one surface; this view
 * gathers what that surface asks the controller and nothing else. The controller still scans, stores
 * and switches (docs/adr/0141).
 */

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace lens::app::pet {

class PetController;

class PetPicker final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Pet; reached as Pet.picker")

    /// Every pet the settings page offers, as {id, name, external}, in catalogue order.
    Q_PROPERTY(QVariantList pets READ pets NOTIFY catalogChanged)
    /// Every installed package that was refused, as {id, reason}; the reason is the Refusal name.
    Q_PROPERTY(QVariantList refusals READ refusals NOTIFY catalogChanged)
    /// The chosen pet's folder name; the reader's new choice while the old pet is still walking out.
    Q_PROPERTY(QString pet READ pet NOTIFY petChanged)

public:
    explicit PetPicker(PetController& owner, QObject* parent = nullptr);

    QVariantList pets() const;
    QVariantList refusals() const;
    QString      pet() const;

    /// @brief One still of a shipped pet's idle figure for the picker's postcard (UI-SPEC.md 4.4).
    /// @return {canvas, foot, sheets, anchors, expression, outfit}, or an empty map for an installed
    ///         package, an unknown id, or a folder that will not load.
    Q_INVOKABLE QVariantMap portrait(QString id);
    /// @brief Chooses a pet; an unconfirmed package asks for confirmation first.
    Q_INVOKABLE void setPet(QString id);
    /// @brief Records the reader's confirmation of an installed package, and switches to it.
    Q_INVOKABLE void confirmPet(QString id);
    /// @brief Rescans both folders, as the extensions section does each time it opens.
    Q_INVOKABLE void refresh();
    /// @brief Opens the reader's pets folder in the file manager.
    Q_INVOKABLE void openPacksFolder();

signals:
    void catalogChanged();
    void petChanged();
    /// @brief An installed package was chosen but not yet confirmed; the settings ask the reader.
    void confirmationRequested(QString id, QString name);

private:
    PetController& owner_;
};

}
