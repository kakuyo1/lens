#pragma once

/**
 * @file glass_capture.h
 * @brief The screen DXGI duplication refuses, read through Windows Graphics Capture.
 *
 * Desktop Duplication is a per-output fact, and on hybrid machines the internal panel answers
 * `DXGI_ERROR_UNSUPPORTED` (docs/adr/0072). Windows.Graphics.Capture asks the compositor for the same screen
 * through another API and works there, so a screen that refuses duplication keeps a fast source instead of
 * falling to the fifteen-a-second GDI read (docs/adr/0087).
 *
 * The frame is copied back to the CPU, like a duplicated one: the delivery path GlassSheetItem owns stays the
 * same, and the GPU-texture route stays out until Qt exposes it (docs/adr/0074). One session per output, opened
 * on the thread that reads it.
 *
 * This is the middle adapter of the desktop seam (src/app/glass/glass_source.h): duplication is tried first,
 * this second, and GDI last.
 */

#include "glass_source.h"

#include <QRect>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace lens::app {

/**
 * @brief One output's Windows Graphics Capture, readable a sheet at a time.
 *
 * The session opens on the first read, on the thread that reads it. A frame arrives when the compositor draws
 * the display again; a read waits for the next one up to its timeout and returns nothing when there is none.
 *
 * The capture is refused, leaving the screen to GDI, when the system cannot draw it without the capture border
 * or the mouse pointer in the picture: both are properties of the session's interfaces, `IGraphicsCaptureSession2`
 * and `IGraphicsCaptureSession3`, which Windows 10 does not have. A rotated output and an HDR frame are refused
 * for the same reason: this source hands back the desktop as it stands, and the callers above it can still try
 * duplication and GDI.
 */
class GlassCapture final : public DesktopSource {
public:
    /**
     * @brief A session for one output.
     * @param deviceName The output's name as QScreen::name() reports it, for the log.
     * @param desktop The output's rectangle on the desktop, in device pixels.
     * @param monitor The output's native handle, as QWindowsScreen hands it over; zero when there is none.
     */
    GlassCapture(QString deviceName, QRect desktop, quintptr monitor);
    ~GlassCapture() override;

    GlassCapture(GlassCapture const&)            = delete;
    GlassCapture& operator=(GlassCapture const&) = delete;

    /**
     * @brief Take the screen under @p native out of the next frame, if there is one.
     * @param native The sheet, in desktop device pixels.
     * @param scale Device pixels per device-independent pixel; the picture carries it as its devicePixelRatio.
     * @param timeoutMs How long to wait for a frame before reporting none, in milliseconds. Zero takes the frame
     *        already waiting, if any.
     * @return The picture when a frame was copied; otherwise empty, with @c newFrame false (nothing changed) or
     *         @c refused true (the caller reads with GDI from now on).
     * @note Called on the glass refresh's worker thread, and on no other.
     */
    DesktopRead read(QRect native, double scale, int timeoutMs = 0) override;

    /// @return Whether a capture may be tried now. True before the first attempt; false after a refusal that
    ///         cannot change and while a failed session is in its cooldown. Safe from another thread.
    bool canFollow() const override;

    /**
     * @brief Release the capture session and its device.
     * @note Must run on the thread that read this source, before the object is destroyed: the frame pool and its
     *       session belong to that apartment. Safe to call when no session opened.
     */
    void close();

    /// @return The screen name, as given at construction: for the log.
    QString const& deviceName() const;

private:
    struct Impl;
    // Declared before impl_: the implementation's name is a copy of this, and members initialize in declaration order.
    QString               deviceName_;
    std::unique_ptr<Impl> impl_;
};

} // namespace lens::app
