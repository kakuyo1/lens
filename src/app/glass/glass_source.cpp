/**
 * @file glass_source.cpp
 * @brief The desktop sources the backdrop always has, and the order it tries them in (see glass_source.h).
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

bool GdiSource::canFollow() const
{
    return false;
}

FallbackSource::FallbackSource(std::initializer_list<DesktopSource*> const sources)
    : sources(sources)
{
}

DesktopRead FallbackSource::read(QRect const native, double const scale, int const timeoutMs)
{
    for (auto index = std::size_t{0}; index < sources.size(); ++index) {
        auto* const source = sources[index];
        auto const  last   = index + 1 == sources.size();
        // A source that cannot follow is not asked unless it is the last one: a picture has to come from somewhere.
        if (not last and not source->canFollow()) continue;
        auto read = source->read(native, scale, timeoutMs);
        if (read.refused) continue;
        // The sheet notes when this screen is on the slow path. A source that can follow frames is not that path,
        // even when an earlier one refused.
        read.refused = not source->canFollow();
        return read;
    }
    return {.refused = true};
}

bool FallbackSource::canFollow() const
{
    for (auto const* const source : sources)
        if (source->canFollow()) return true;
    return false;
}

} // namespace lens::app
