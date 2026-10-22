#pragma once

/**
 * @file glass_duplication.h
 * @brief The desktop read that skips GDI: one DXGI output duplicated on its own adapter, cropped to a sheet.
 *
 * ADR 0040 gave up on DXGI because the internal panel of the measurement machine refused duplication; the 2026-10-19
 * probe shows the refusal is per output, not per machine: the external monitor duplicates at 137 frames a second while
 * the integrated panel refuses on every adapter (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md
 * 3.1). A GDI read of a sheet costs 13 to 41 ms depending on the screen and the moment; a duplicated frame reaches the
 * CPU in 0.75 ms for the same sheet. That is the difference between a refresh budget of 15 a second and one that can
 * follow the display.
 *
 * The frame is copied back to the CPU rather than handed to the scene graph as a GPU texture: the shared-texture route
 * needs `QRhiTexture::createFrom`, which lives in a private header, and ADR 0040's rule against private headers stands.
 * The copy is cheap enough for the live refresh, and it keeps the delivery path the one GlassSheetItem already owns
 * (docs/adr/0071, docs/adr/0072).
 *
 * One session per output, created on the thread that reads it. A screen that cannot duplicate keeps the GDI read: the
 * fallback is the normal case on hybrid machines, not an error. This is the duplication adapter of the desktop seam
 * (src/app/glass/glass_source.h): it answers the same read as GDI and says when it refused, and the backdrop's
 * FallbackSource owns the order (docs/adr/0086).
 */

#include "glass_source.h"

#include <QRect>
#include <QSize>
#include <QString>

#include <memory>

namespace lens::app {

/// The rotation DXGI reports for an output, in the terms a desktop rectangle is mapped with.
enum class ScreenRotation {
    Identity,
    /// The frame holds the desktop image turned 90 degrees clockwise.
    Rotate90,
    /// The frame holds the desktop image turned 180 degrees.
    Rotate180,
    /// The frame holds the desktop image turned 90 degrees counter-clockwise.
    Rotate270,
};

/**
 * @brief The part of a duplicated frame that holds a rectangle of the screen.
 * @param screenRect The rectangle on the screen, in the screen's own device pixels, origin at its top-left corner.
 * @param frameSize The duplicated frame's size, in device pixels. A rotated output's frame is the desktop rectangle
 *        with its sides swapped: the measurement machine's portrait screen is 1440 x 2560 on the desktop and its frame
 *        is 2560 x 1440.
 * @param rotation How the frame is turned relative to the screen.
 * @return The rectangle in the frame. The size is the screen rectangle's with its sides swapped when rotated by 90 or
 *         270 degrees.
 * @note A rotated output is why this exists: DXGI reports the desktop rectangle in the screen's orientation and the
 *       duplicated surface in the panel's native one, and `toNativeRect` cannot bridge the two.
 */
QRect frameRectFor(QRect screenRect, QSize frameSize, ScreenRotation rotation);

/**
 * @brief One output's desktop duplication, readable a sheet at a time.
 *
 * The session opens on the first read, on the thread that reads it (the glass refresh's worker), because building the
 * D3D11 device is what pulls a driver thread pool and memory, and a screen that never shows glass should not pay for
 * it. Every later read polls `AcquireNextFrame` without waiting: no new frame means nothing changed behind the card, and
 * the caller has nothing to publish. A still desktop therefore costs a poll and no copy.
 *
 * Failures are per output and remembered. A refusal that will not change (the internal panel's
 * `DXGI_ERROR_UNSUPPORTED`) is permanent; an access lost to another recorder or a display mode change is retried after
 * a cooldown, with the GDI read covering the gap.
 */
class GlassDuplication final : public DesktopSource {
public:
    /**
     * @brief A session for one output.
     * @param deviceName The output's name as QScreen::name() reports it. On Windows that is the monitor's model, not
     *        the device name DXGI lists, so it is for the log; the rectangle is what finds the output.
     * @param desktop The output's rectangle on the desktop, in device pixels.
     */
    GlassDuplication(QString deviceName, QRect desktop);
    ~GlassDuplication();

    GlassDuplication(GlassDuplication const&)            = delete;
    GlassDuplication& operator=(GlassDuplication const&) = delete;

    /**
     * @brief Take the screen under @p native out of the next new frame, if there is one.
     * @param native The sheet, in desktop device pixels.
     * @param scale Device pixels per device-independent pixel; the picture carries it as its devicePixelRatio.
     * @param timeoutMs How long to wait for a frame before reporting none, in milliseconds. Zero polls and returns at
     *        once. The Smooth tier's chained reads wait here instead of relying on a GUI timer: a timer's jitter let
     *        frames produced between two ticks be coalesced away (docs/adr/0074).
     * @return The picture when a new frame was copied; otherwise empty, with @c newFrame false (nothing changed) or
     *         @c refused true (the caller reads with GDI from now on).
     * @note Called on the glass refresh's worker thread, and on no other. The device is created there and used there.
     */
    DesktopRead read(QRect native, double scale, int timeoutMs = 0) override;

    /// @return Whether the session can be tried now. True before the first attempt; false while a failed session is in
    ///         its cooldown, and after a refusal that cannot change. Safe from another thread.
    bool canDuplicate() const override;

    /// @return The screen name, as given at construction: for the log.
    QString const& deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lens::app
