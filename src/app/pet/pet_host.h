#pragma once

/**
 * @file pet_host.h
 * @brief The pet window's one entry: where it opens, how it is kept whole, and what its native handle takes.
 *
 * The window host is QML's side of the desktop window: the reader's saved spot, the fit after a size
 * change, the cursor in window coordinates, the clock's on/off, and the capture exclusion the review
 * sign shares. The controller keeps the geometry and the native rules (docs/adr/0141).
 */

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QtQml/qqmlregistration.h>

namespace lens::app::pet {

class PetController;

class PetHost final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Pet; reached as Pet.host")

    /// Whether the pet's window is on screen. Not enabled(): a goodbye keeps the window up after the switch went off.
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
    explicit PetHost(PetController& owner, QObject* parent = nullptr);

    bool shown() const;

    /// @param window The QML Window, whose native handle takes capture exclusion and pass-through.
    Q_INVOKABLE void attachWindow(QObject* window);
    /// @brief Keeps a QML window out of screen captures, as the pet's own is.
    Q_INVOKABLE void keepOutOfCapture(QObject* window);
    /// @return The mouse position in the same coordinates a window's x and y use.
    Q_INVOKABLE QPoint cursorPos() const;
    /// @return Where the window should open at this size; also records that spot as the stroll's home.
    Q_INVOKABLE QPoint initialPosition(QSize size);
    /// @brief Nudges the window the least distance that keeps it whole on its screen, and saves where it landed.
    Q_INVOKABLE void fitOnScreen();
    /// @brief Records where the window was dropped, relative to the screen it landed on.
    Q_INVOKABLE void savePosition(QPoint at);
    /// @brief Runs the frame clock for the desktop window. The window calls it with its visibility.
    Q_INVOKABLE void setRunning(bool on);
    /// @brief Tells the ceremonies whether motion is reduced, so both animation switches land them at once.
    Q_INVOKABLE void setMotionReduced(bool on);

signals:
    void shownChanged();

private:
    PetController& owner_;
};

}
