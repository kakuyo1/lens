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

#include <initializer_list>
#include <vector>

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
 * canFollow() is asked on the GUI thread before a read is started, to set the timer's pace.
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

    /// @return Whether this source may keep up with the screen's frame production, which is what the backdrop paces
    ///         its timer by. A source that cannot is only worth a read when nothing else can serve one.
    virtual bool canFollow() const = 0;
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
    bool        canFollow() const override;

private:
    bool       onDesktop;
    SheetScale sheetScale;
};

/**
 * @brief The sources of one screen, tried in the order given: duplication, then Windows Graphics Capture, then GDI.
 *
 * The fallback is the normal case on hybrid machines, not an error: one output duplicates while another refuses on
 * every adapter (docs/adr/0072, docs/adr/0087). A read a source refuses is re-read from the next one, and the sheet is
 * told the answer came from the slow path so the settings page can say so in words. A source that says it cannot
 * follow frames is skipped unless it is the last one, which is always asked: a picture has to come from somewhere.
 */
class FallbackSource final : public DesktopSource {
public:
    explicit FallbackSource(std::initializer_list<DesktopSource*> sources);

    DesktopRead read(QRect native, double scale, int timeoutMs = 0) override;
    bool        canFollow() const override;

private:
    std::vector<DesktopSource*> sources;
};

} // namespace lens::app
