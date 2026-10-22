/**
 * @file glass_source.cpp
 * @brief The two desktop sources the backdrop always has, and the order it tries them in (see glass_source.h).
 */

#include "glass_source.h"

#include <QElapsedTimer>

#include <utility>

namespace lens::app {

GdiSource::GdiSource(bool const onDesktop, SheetScale const scale)
    : onDesktop(onDesktop), sheetScale(scale)
{
}

DesktopRead GdiSource::read(QRect const native, double const scale, int /*timeoutMs*/)
{
    QElapsedTimer started;
    started.start();
    auto picture = readDesktopRegion(native, sheetScale, onDesktop);
    // Timed here and not around the whole call: what the budget is set by is one desktop read, not the tone read and
    // the publish that follow it (src/app/glass/glass_stats.h).
    auto const readMs = static_cast<double>(started.nsecsElapsed()) / 1.0e6;
    if (picture.isNull()) return {.usedGdi = true, .copyMs = readMs};
    picture.setDevicePixelRatio(scale);
    return {.image = std::move(picture), .newFrame = true, .usedGdi = true, .copyMs = readMs};
}

bool GdiSource::canDuplicate() const
{
    return false;
}

FallbackSource::FallbackSource(DesktopSource& preferred, DesktopSource& fallback)
    : preferred(preferred), fallback(fallback)
{
}

DesktopRead FallbackSource::read(QRect const native, double const scale, int const timeoutMs)
{
    if (preferred.canDuplicate()) {
        auto read = preferred.read(native, scale, timeoutMs);
        if (not read.refused) return read;
    }
    auto read = fallback.read(native, scale, timeoutMs);
    // The sheet notes that this screen refused duplication; a read that never tried it (GDI alone, a Standard tier)
    // leaves this false, so an unrelated read cannot clear the note.
    read.refused = true;
    return read;
}

bool FallbackSource::canDuplicate() const
{
    return preferred.canDuplicate();
}

} // namespace lens::app
