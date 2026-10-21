#pragma once

/**
 * @file glass_desktop.h
 * @brief Reading the pixels Windows is showing, which is what a glass sheet is cut from.
 *
 * Split out of the sheet's lifecycle (src/app/glass/glass_backdrop.h) because this call is the one whose cost decides the
 * refresh rate. ADR 0040 set 15 reads a second from an offline probe that measured 13.4 ms; the 2026-10-19 probe
 * measures 25.2 to 41.2 ms in a tight loop (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md),
 * while the in-application CPU record divides out to about 6.7 ms
 * (docs/metrics/resource-footprint/glass-memory-2026-10-18.md 9.7). Only a read taken at the pace the application
 * keeps settles which of the two is the truth, and a test has to be able to take one without building a window, a
 * sheet and a QML surface around it. That is the whole reason for this header: the function is unchanged apart from
 * taking its scale as an argument, so the measurement reads exactly what the application reads.
 */

#include <QImage>
#include <QRect>

namespace lens::app {

/**
 * @brief How far a sheet is read down from the device resolution.
 * @note A type of its own rather than an int: `readDesktopRegion` takes this and a `bool` next to each other, and an
 *       int there would accept the bool and quietly read every sheet at full size. The measurement says the two are
 *       nearly the same price (20.0 ms at a quarter, 23.5 ms at full, at the application's pace), so this is a quality
 *       choice and will be set per tier, not per machine
 *       (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.5).
 */
enum class SheetScale : int {
    Half    = 2,
    Quarter = 4,
    Full    = 1,
};

/**
 * @brief The desktop under @p native, drawn into a picture one over @p scale of its device size.
 * @param native The region to read, in desktop device pixels.
 * @param scale What to shrink it by: Full keeps the device resolution, Quarter is what the sheet keeps today.
 * @param onDesktop False where the platform has no desktop to read, and a black picture stands in: a test run must
 *        not photograph the reader's real screen.
 * @return The picture, or a null one when the read failed.
 * @note GDI, so what comes back is what the compositor hands to the legacy screen DC -- the card itself is kept out
 *       of it while the glass shows (docs/adr/0038). Never written to disk, never kept past its caller
 *       (docs/adr/0034, SECURITY.md).
 */
QImage readDesktopRegion(QRect native, SheetScale scale, bool onDesktop);

} // namespace lens::app
