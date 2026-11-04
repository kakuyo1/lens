/** @file foreground_window.h
 * @brief Fail-closed Win32 foreground identity for the scanner.
 */
#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace lens::capture {

struct ForegroundWindow {
    quintptr window = 0;
    QString  context;
    QString  processName;
    /// The monitor the window sits on, as Win32 hands it over.
    ///
    /// A handle rather than the monitor's name: Qt reports a screen by its *friendly* name
    /// ("B156HAN15.H", padded with a space) and `MONITORINFOEXW` by its device name
    /// (`\.\DISPLAY1`), so the two names never compare equal.
    quintptr monitor    = 0;
    bool     ownProcess = false;
    QPoint   anchor;
    /// The window's size in physical pixels, from the same rectangle as anchor.
    QSize size;
};

std::optional<ForegroundWindow> foregroundWindow();

/// @return The monitor a physical pixel falls on, or zero when it is on none of them.
quintptr monitorAt(QPoint physical);

/// @return The monitor's origin in physical pixels, or nothing when @p monitor is not one.
std::optional<QPoint> physicalScreenOrigin(quintptr monitor);

/// @brief The part of a window that a full-screen grab of its monitor holds.
/// @param window The window's rectangle, in physical pixels of the virtual desktop.
/// @param origin The monitor's top-left in the same pixels, from physicalScreenOrigin().
/// @param grab   The size of the full-screen grab of that monitor, in physical pixels.
/// @return The crop in the grab's own coordinates; empty when the window is not on this monitor.
QRect windowCropInGrab(QRect window, QPoint origin, QSize grab);

}
