/**
 * @file surface_geometry.cpp
 * @brief The placement rules and screen conversion of surface_geometry.h.
 */

#include "app/geometry/surface_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lens::app::geometry {
namespace {

/// @return @p value held inside [low, high], the low end winning when the range is reversed: an oversized
///         card collapses onto the work area's near edge rather than turning inside out.
qreal clamped(qreal const value, qreal const low, qreal const high)
{
    return std::max(low, std::min(value, std::max(low, high)));
}

/// @return @p value rounded to a whole number the way QML's Math.round does, including below zero: Windows
///         holds whole pixels, and a card at -8.5 rounds to -8 rather than away from zero.
int rounded(qreal const value)
{
    return static_cast<int>(std::floor(value + 0.5));
}

qreal scaleOf(qreal const ratio)
{
    return ratio > 0 ? ratio : 1.0;
}

/// @return The area of @p card that lies over [left, right] x [top, bottom].
qreal covered(QRectF const& card, qreal const left, qreal const top, qreal const right, qreal const bottom)
{
    auto const width  = std::max<qreal>(0, std::min(card.right(), right) - std::max(card.left(), left));
    auto const height = std::max<qreal>(0, std::min(card.bottom(), bottom) - std::max(card.top(), top));
    return width * height;
}

} // namespace

QSizeF SurfaceGeometry::boundedSize(QSizeF const size, QRectF const area, qreal const margin) const
{
    auto const availableWidth  = std::max<qreal>(1, area.width() - 2 * margin);
    auto const availableHeight = std::max<qreal>(1, area.height() - 2 * margin);
    return {std::min(std::max<qreal>(1, size.width()), availableWidth),
            std::min(std::max<qreal>(1, size.height()), availableHeight)};
}

QPoint SurfaceGeometry::besideAnchor(QRectF const anchor, QSizeF const size, QRectF const area, qreal const shadow, qreal const margin) const
{
    auto const left   = area.left() + margin;
    auto const top    = area.top() + margin;
    auto const right  = area.right() - margin;
    auto const bottom = area.bottom() - margin;

    auto const x = clamped(anchor.left() + anchor.width() / 2 - size.width() + shadow, left, right - size.width());
    auto       y = anchor.top() + anchor.height() / 2 - size.height() + shadow;
    if (y < top)
        y = anchor.bottom() + 8;
    return {rounded(x), rounded(clamped(y, top, bottom - size.height()))};
}

QPoint SurfaceGeometry::besidePet(QRectF const pet, QSizeF const size, QRectF const area, qreal const gap, qreal const margin) const
{
    auto const left   = area.left() + margin;
    auto const top    = area.top() + margin;
    auto const right  = area.right() - margin;
    auto const bottom = area.bottom() - margin;

    // Each clamp is monotone, so a pet dragged wholly past an edge collapses onto that edge rather than
    // turning its rectangle inside out.
    auto const petLeft   = clamped(pet.left(), left, right);
    auto const petRight  = clamped(pet.left() + pet.width(), petLeft, right);
    auto const petTop    = clamped(pet.top(), top, bottom);
    auto const petBottom = clamped(pet.top() + pet.height(), petTop, bottom);
    auto const centreX   = (petLeft + petRight) / 2;
    auto const centreY   = (petTop + petBottom) / 2;

    std::array const candidates{
        QPointF{petRight + gap, centreY - size.height() / 2},
        QPointF{petLeft - gap - size.width(), centreY - size.height() / 2},
        QPointF{centreX - size.width() / 2, petBottom + gap},
        QPointF{centreX - size.width() / 2, petTop - gap - size.height()},
    };
    /// The candidate's rectangle, pulled back inside the work area by however much of it hangs out.
    auto const onScreen = [&](QPointF const at) {
        return QRectF{clamped(at.x(), left, right - size.width()),
                      clamped(at.y(), top, bottom - size.height()),
                      size.width(),
                      size.height()};
    };

    auto chosen = onScreen(candidates.front());
    for (auto const& candidate : candidates) {
        auto const at = onScreen(candidate);
        if (covered(at, petLeft, petTop, petRight, petBottom) < covered(chosen, petLeft, petTop, petRight, petBottom))
            chosen = at;
    }
    return {rounded(chosen.left()), rounded(chosen.top())};
}

QPoint SurfaceGeometry::transient(QPointF const anchor, QSizeF const size, QRectF const area, qreal const gap, qreal const shadow, bool const preferBelow, qreal const margin) const
{
    auto const left   = area.left() + margin;
    auto const top    = area.top() + margin;
    auto const right  = area.right() - margin;
    auto const bottom = area.bottom() - margin;

    auto const above = anchor.y() - gap - size.height() + shadow;
    auto const below = anchor.y() + gap - shadow;
    auto const y     = preferBelow ? (below + size.height() <= bottom ? below : above)
                                   : (above >= top ? above : below);
    return {rounded(clamped(anchor.x() - shadow, left, right - size.width())),
            rounded(clamped(y, top, bottom - size.height()))};
}

QPoint SurfaceGeometry::reviewCard(QRectF const word, QSizeF const size, QRectF const area, qreal const gap, qreal const shadow, qreal const margin) const
{
    auto const left   = area.left() + margin;
    auto const top    = area.top() + margin;
    auto const right  = area.right() - margin;
    auto const bottom = area.bottom() - margin;

    auto const above = word.top() - gap + shadow - size.height();
    auto const below = word.top() + word.height() + gap - shadow;
    qreal      y     = 0;
    if (above >= top)
        y = above;
    else if (below + size.height() <= bottom)
        y = below;
    else if (word.top() - top >= bottom - (word.top() + word.height()))
        y = top;
    else
        y = bottom - size.height();
    y = clamped(y, top, bottom - size.height());

    // The card's left edge, not the window's: the window carries the shadow margin.
    return {rounded(clamped(word.left() - shadow, left, right - size.width())), rounded(y)};
}

QPoint SurfaceGeometry::keptOnScreen(QPointF const at, QSizeF const size, QRectF const area, qreal const margin) const
{
    return {rounded(clamped(at.x(), area.left() + margin, area.right() - margin - size.width())),
            rounded(clamped(at.y(), area.top() + margin, area.bottom() - margin - size.height()))};
}

qreal SurfaceGeometry::windowHeight(qreal const contentHeight, qreal const heldFloor, qreal const shadow) const
{
    return std::floor(std::max(heldFloor, contentHeight) + 2 * shadow + 0.5);
}

qreal SurfaceGeometry::heldFloor(qreal const floorInForce, bool const active, qreal const contentHeight, qreal const target) const
{
    return std::max(active ? floorInForce : contentHeight, target);
}

QPointF SurfaceGeometry::dipFromNativePoint(QPointF const native, QPointF const screenOrigin, qreal const ratio) const
{
    auto const scale = scaleOf(ratio);
    return {screenOrigin.x() + (native.x() - screenOrigin.x()) / scale,
            screenOrigin.y() + (native.y() - screenOrigin.y()) / scale};
}

QPoint SurfaceGeometry::nativeFromDipPoint(QPointF const dip, QPointF const screenOrigin, qreal const ratio) const
{
    auto const scale = scaleOf(ratio);
    return {rounded(screenOrigin.x() + (dip.x() - screenOrigin.x()) * scale),
            rounded(screenOrigin.y() + (dip.y() - screenOrigin.y()) * scale)};
}

qreal SurfaceGeometry::dipFromNativeLength(qreal const native, qreal const ratio) const
{
    return native / scaleOf(ratio);
}

int SurfaceGeometry::nativeFromDipLength(qreal const dip, qreal const ratio) const
{
    return rounded(dip * scaleOf(ratio));
}

} // namespace lens::app::geometry
