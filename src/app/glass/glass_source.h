#pragma once

/**
 * @file glass_source.h
 * @brief The seam under the glass backdrop: one read of the desktop, whichever way the machine can give it.
 *
 * The backdrop used to decide the way inside its own read: a duplicated output first, GDI when that was not usable, and
 * a black picture on a test platform. That left the fallback order unreachable from a test, and the probe re-implemented
 * the same DXGI and copy-box maths on its own (docs/adr/0086). The order now lives in FallbackSource, over sources that
 * answer one call: GlassDuplication for a duplicated output (src/app/glass/glass_duplication.h), GdiSource for the
 * compositor's screen DC, and a scripted source in the tests, which needs no desktop.
 */

#include "glass_desktop.h"

#include <QImage>
#include <QRect>

namespace lens::app {

/**
 * @brief What one desktop read produced.
 * @note These are the facts a sheet needs to publish or skip one picture: whether there is a picture, whether a
 *       duplicated source had nothing new behind the card, whether the read fell back to GDI, and what it cost.
 */
struct DesktopRead {
    QImage image;            ///< The picture; empty when there is nothing new to publish.
    bool   newFrame = false; ///< A duplicated source found a new frame. False with an empty image means nothing changed.
    bool   refused  = false; ///< This source could not serve; a fallback takes over, and the sheet notes the refusal.
    bool   usedGdi  = false; ///< The read went through GDI, so it spent the shared fifteen-a-second budget.
    double copyMs   = 0.0;   ///< The read's or the frame copy's duration, for the tally.
};

/**
 * @brief One way of reading the desktop.
 *
 * The backdrop reads through this and no longer knows which way answered. read() runs on the refresh's worker thread;
 * canDuplicate() is asked on the GUI thread before a read is started, to set the timer's pace.
 */
class DesktopSource {
public:
    virtual ~DesktopSource() = default;

    /**
     * @brief Take @p native out of the desktop.
     * @param native The region to read, in desktop device pixels.
     * @param scale Device pixels per device-independent pixel; the picture carries it as its devicePixelRatio.
     * @param timeoutMs How long a duplicated source waits for a new frame, in milliseconds. Zero polls and returns at
     *        once; the Smooth tier's chained read waits here instead of relying on a GUI timer (docs/adr/0074).
     * @return What the read produced. A source that cannot serve sets `refused` and leaves the picture empty.
     */
    virtual DesktopRead read(QRect native, double scale, int timeoutMs = 0) = 0;

    /// @return Whether a read may come from a duplicated frame, which is what the backdrop paces its timer by.
    virtual bool canDuplicate() const = 0;
};

/**
 * @brief The GDI read as one source: the compositor's screen DC, at a chosen scale.
 *
 * The first sheet of a surface and a drag's retake read through this even on a screen that duplicates: the first
 * picture has to be up at once, and a duplicated session drops its own unreliable first frame (docs/adr/0072).
 */
class GdiSource final : public DesktopSource {
public:
    /**
     * @param onDesktop False where the platform has no desktop to read; a black picture stands in, so a test run never
     *        photographs the reader's real screen.
     * @param scale How far the read is shrunk from device resolution. Full is what the sheet uses: the blur runs on the
     *        GPU now, so reading at full costs little more and keeps the sheet sharp (docs/adr/0070).
     */
    explicit GdiSource(bool onDesktop, SheetScale scale = SheetScale::Full);

    DesktopRead read(QRect native, double scale, int timeoutMs = 0) override;
    bool        canDuplicate() const override;

private:
    bool       onDesktop;
    SheetScale sheetScale;
};

/**
 * @brief Two sources, tried in the order given: the duplicated output while it can, GDI otherwise.
 *
 * The fallback is the normal case on hybrid machines, not an error: one output duplicates while another refuses on
 * every adapter (docs/adr/0072). A read the preferred source refuses is re-read from the fallback, and the sheet is
 * told the duplication attempt failed so the settings page can say so in words.
 */
class FallbackSource final : public DesktopSource {
public:
    FallbackSource(DesktopSource& preferred, DesktopSource& fallback);

    DesktopRead read(QRect native, double scale, int timeoutMs = 0) override;
    bool        canDuplicate() const override;

private:
    DesktopSource& preferred;
    DesktopSource& fallback;
};

} // namespace lens::app
