#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>

/**
 * @file pet_placement.h
 * @brief Where the pet window goes when the screen changed under it (PHASE3 3.5).
 */

namespace lens::app::pet {

/// @return Whether a window of `size` with its top-left at `at` lies wholly inside `available`.
bool fitsWithin(QPoint at, QSize size, QRect available);

/// @return The top-left corner that puts a window of `size` in the bottom-right of `available`.
QPoint bottomRightOf(QSize size, QRect available);

/**
 * @brief Nudges a window the least distance that brings it wholly inside `area`.
 * @param at Current top-left of the window.
 * @param size Size of the window.
 * @param area The usable area of one screen.
 * @return `at` when it already fits, else each overflowing axis pulled back to the area's edge. An axis the window
 *         cannot fit in at all pins to the area's near edge, so the overflow stays on the far side.
 */
QPoint clampedWithin(QPoint at, QSize size, QRect area);

/**
 * @brief Keeps a window where it is while some screen still holds it, else moves it to the primary screen's corner.
 * @param at Current top-left of the window.
 * @param size Size of the window.
 * @param areas The usable area of every screen now attached.
 * @param primary The usable area of the primary screen.
 * @return `at` when one of `areas` holds the window whole, else bottomRightOf(size, primary).
 */
QPoint placeWithin(QPoint at, QSize size, QList<QRect> const& areas, QRect primary);

/**
 * @brief Finds the screen a point belongs to, or the one nearest to it when the point is off every screen.
 * @param at A point, such as the top-left of the window when it was dropped.
 * @param areas The usable area of every screen.
 * @return The index in `areas` of the area holding `at`, else of the nearest one; -1 when `areas` is empty.
 */
int indexOfNearestArea(QPoint at, QList<QRect> const& areas);

} // namespace lens::app::pet
