/**
 * @file pet_placement.cpp
 * @brief Implementation of the pet window's placement rules.
 */

#include "app/pet/pet_placement.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace lens::app::pet {

bool fitsWithin(QPoint at, QSize size, QRect available)
{
    return available.contains(QRect{at, size});
}

QPoint bottomRightOf(QSize size, QRect available)
{
    return QPoint{available.right() - size.width() + 1, available.bottom() - size.height() + 1};
}

QPoint clampedWithin(QPoint at, QSize size, QRect area)
{
    // The last top-left that still keeps the far edge inside; it lies before the near edge on an axis too short
    // for the window, which pins that axis to the area's near edge.
    auto const clampAxis = [](int value, int low, int high) { return high < low ? low : std::clamp(value, low, high); };
    return QPoint{clampAxis(at.x(), area.left(), area.right() - size.width() + 1),
                  clampAxis(at.y(), area.top(), area.bottom() - size.height() + 1)};
}

QPoint placeWithin(QPoint at, QSize size, QList<QRect> const& areas, QRect primary)
{
    for (auto const& area : areas) {
        if (fitsWithin(at, size, area)) return at;
    }
    return bottomRightOf(size, primary);
}

int indexOfNearestArea(QPoint at, QList<QRect> const& areas)
{
    auto nearest  = -1;
    auto bestDist = std::numeric_limits<qint64>::max();
    for (qsizetype i = 0; i < areas.size(); ++i) {
        auto const& area = areas.at(i);
        if (area.contains(at)) return static_cast<int>(i);
        // Distance from the point to the rectangle along each axis; zero along an axis the point lies within.
        auto const dx   = std::max({area.left() - at.x(), 0, at.x() - area.right()});
        auto const dy   = std::max({area.top() - at.y(), 0, at.y() - area.bottom()});
        auto const dist = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (dist < bestDist) {
            bestDist = dist;
            nearest  = static_cast<int>(i);
        }
    }
    return nearest;
}

void PetPlacement::setScreens(QList<QRect> areas, QRect primary)
{
    areas_   = std::move(areas);
    primary_ = primary;
}

int PetPlacement::nearest(QPoint at) const
{
    return indexOfNearestArea(at, areas_);
}

QRect PetPlacement::areaAt(QPoint at) const
{
    auto const index = nearest(at);
    return index < 0 ? QRect{} : areas_.at(index);
}

bool PetPlacement::holds(QPoint at, QSize size) const
{
    for (auto const& area : areas_) {
        if (fitsWithin(at, size, area)) return true;
    }
    return false;
}

QPoint PetPlacement::fit(QPoint at, QSize size) const
{
    auto const area = areaAt(at);
    return area.isValid() ? clampedWithin(at, size, area) : at;
}

QPoint PetPlacement::keep(QPoint at, QSize size) const
{
    return areas_.isEmpty() ? at : placeWithin(at, size, areas_, primary_);
}

void PetPlacement::setHome(QPoint at)
{
    home_ = at;
}

void PetPlacement::forgetHome()
{
    home_.reset();
}

} // namespace lens::app::pet
