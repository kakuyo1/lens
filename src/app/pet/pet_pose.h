#pragma once

/**
 * @file pet_pose.h
 * @brief Everything one frame draws, as the one value QML reads.
 *
 * The controller publishes a frame by moving its own state; this view projects that state onto the
 * names the stage, the window, the review sign and the settings preview bind to. The figure fields
 * change when the pet or its size does; the frame and scene fields when a frame is produced; the
 * gaze on its own, because the eyes move between frames. Reads only; the controller remains the
 * one writer (docs/adr/0141).
 */

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace lens::app::pet {

class PetController;

class PetPose final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Pet; reached as Pet.pose")

    // The figure: the sizes and art the pet was loaded with, and the idle ink the sign stands against.
    Q_PROPERTY(int canvas READ canvas NOTIFY figureChanged)
    Q_PROPERTY(double scale READ scale NOTIFY figureChanged)
    Q_PROPERTY(QVariantMap layers READ layers NOTIFY figureChanged)
    Q_PROPERTY(QPointF foot READ foot NOTIFY figureChanged)
    Q_PROPERTY(QVariantMap reviewSign READ reviewSign NOTIFY figureChanged)
    Q_PROPERTY(QRectF inkBox READ inkBox NOTIFY figureChanged)
    Q_PROPERTY(double inkCentre READ inkCentre NOTIFY figureChanged)

    // The frame the stage draws.
    Q_PROPERTY(QString action READ action NOTIFY frameChanged)
    Q_PROPERTY(int frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(int expression READ expression NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sheets READ sheets NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap anchors READ anchors NOTIFY frameChanged)
    Q_PROPERTY(QVariantList worn READ worn NOTIFY frameChanged)
    Q_PROPERTY(QPoint gaze READ gaze NOTIFY gazeChanged)

    // The scene the frame belongs to; empty, and neutral, outside one.
    Q_PROPERTY(QString scene READ scene NOTIFY frameChanged)
    Q_PROPERTY(int sceneFrame READ sceneFrame NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sceneSheets READ sceneSheets NOTIFY frameChanged)
    Q_PROPERTY(QPointF sceneShift READ sceneShift NOTIFY frameChanged)
    Q_PROPERTY(QPointF sceneAlign READ sceneAlign NOTIFY frameChanged)
    Q_PROPERTY(double sceneScale READ sceneScale NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sceneLayers READ sceneLayers NOTIFY frameChanged)
    /// The entry choreography in progress (ADR 0101); neutral outside one.
    Q_PROPERTY(QVariantMap entry READ entry NOTIFY frameChanged)

public:
    explicit PetPose(PetController& owner, QObject* parent = nullptr);

    int         canvas() const;
    double      scale() const;
    QVariantMap layers() const;
    QPointF     foot() const;
    QVariantMap reviewSign() const;
    QRectF      inkBox() const;
    double      inkCentre() const;

    QString      action() const;
    int          frame() const;
    int          expression() const;
    QVariantMap  sheets() const;
    QVariantMap  anchors() const;
    QVariantList worn() const;
    QPoint       gaze() const;

    QString     scene() const;
    int         sceneFrame() const;
    QVariantMap sceneSheets() const;
    QPointF     sceneShift() const;
    QPointF     sceneAlign() const;
    double      sceneScale() const;
    QVariantMap sceneLayers() const;
    QVariantMap entry() const;

signals:
    void frameChanged();
    void gazeChanged();
    void figureChanged();

private:
    PetController& owner_;
};

}
