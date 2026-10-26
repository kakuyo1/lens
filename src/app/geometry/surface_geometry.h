#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtQml/qqmlregistration.h>

/**
 * @file surface_geometry.h
 * @brief Where a surface's window goes: anchor, card size, gap, shadow and work area in, top-left out.
 *
 * The arithmetic every surface is placed by, and the one conversion between the native pixels the mouse hook
 * reports and the device-independent ones windows are placed in. Nothing here reads a window, asks Qt for a
 * screen or keeps state: SurfaceHost.qml gathers those facts and writes the answer back, which is what lets
 * the same rules be driven by value tests (docs/adr/0116).
 */

namespace lens::app::geometry {

/**
 * @brief The placement rules and the per-screen conversion, as QML calls them.
 *
 * @note A work area's far edges are exclusive: a window fits when its right edge is at most area.right().
 *       Every answer is in virtual DIPs. The engine builds this singleton itself -- there is nothing to
 *       provide() and no state to lose.
 */
class SurfaceGeometry : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SurfaceGeometry)
    QML_SINGLETON
    /// @brief The margin every surface keeps clear of the work area's edges, in DIPs, as QML reads it.
    Q_PROPERTY(qreal margin READ margin CONSTANT)
public:
    /// @brief The margin every surface keeps clear of the work area's edges, in DIPs.
    static constexpr qreal kMargin = 8;

    SurfaceGeometry() = default;

    /// @return The shared margin, so no surface writes the 8 again.
    qreal margin() const
    {
        return kMargin;
    }

    /**
     * @brief A surface's size held inside a work area without changing its content scale.
     * @param size The size the content asks for.
     * @param area The screen's work area, in DIPs.
     * @param margin The clear band at each edge.
     * @return Each axis at least 1 and at most the work area less both margins.
     */
    Q_INVOKABLE QSizeF boundedSize(QSizeF size, QRectF area, qreal margin = kMargin) const;

    /**
     * @brief Where a panel sits beside an anchor rectangle: its bottom-right corner on the anchor's centre.
     *
     * The x is clamped into the work area. A y that would put the card above the work area flips it below the
     * anchor instead; otherwise it is clamped.
     *
     * @param anchor The rectangle the panel hangs from, in DIPs.
     * @param size The window's size, shadow margin included.
     * @param area The anchor screen's work area, in DIPs.
     * @param shadow The window's transparent shadow margin.
     * @param margin The clear band at each edge of the work area.
     * @return The window's top-left, rounded to whole DIPs.
     */
    Q_INVOKABLE QPoint besideAnchor(QRectF anchor, QSizeF size, QRectF area, qreal shadow, qreal margin = kMargin) const;

    /**
     * @brief Where a panel sits beside the pet's rectangle rather than beside the tray icon.
     *
     * The candidates are the pet's four sides in the order right, left, below, above, each @p gap clear of
     * that side and centred across its length. Each is scored by how much of the pet the clamped card covers
     * (0 for a side the card clears); lowest wins, ties by that order. The score rather than a fit test keeps
     * the card off the pet: a work area too short clamps the card back over it, and the side clamped least far
     * onto the pet survives. The horizontal sides come first because the card is 532 px tall and needs that
     * much work-area height to clear the pet vertically, which the corner the pet is usually left in does not
     * have.
     *
     * @param pet The pet window's rectangle as it stands; it is read against the work area first, so a pet
     *        dragged over an edge is read as the part still on screen.
     * @param size The window's size, shadow margin included.
     * @param area The pet's screen work area, in DIPs.
     * @param gap The clear distance from the pet.
     * @param margin The clear band at each edge of the work area.
     * @return The window's top-left, rounded to whole DIPs.
     */
    Q_INVOKABLE QPoint besidePet(QRectF pet, QSizeF size, QRectF area, qreal gap, qreal margin = kMargin) const;

    /**
     * @brief Where a bubble or action bar sits: on the side the reader chose, or the other when it has no room.
     *
     * Neither side fitting is not decided here: the clamp then keeps the window on screen, on the chosen side's
     * edge. The left edge is pinned to the anchor's x and the shadow margin is taken off, so the card's left
     * edge, not the window's, lines up with the anchor.
     *
     * @param anchor The release point, in DIPs.
     * @param size The window's size, shadow margin included.
     * @param area The anchor screen's work area, in DIPs.
     * @param gap The clear distance between the anchor and the card.
     * @param shadow The window's transparent shadow margin.
     * @param preferBelow Whether the reader chose the below side.
     * @param margin The clear band at each edge of the work area.
     * @return The window's top-left, rounded to whole DIPs.
     */
    Q_INVOKABLE QPoint transient(QPointF anchor, QSizeF size, QRectF area, qreal gap, qreal shadow, bool preferBelow, qreal margin = kMargin) const;

    /**
     * @brief Where the word list's review card sits against the word it belongs to (UI-SPEC.md 4.8).
     *
     * The card's visible bottom edge keeps @p gap above the word and its left edge lines up with the word's
     * left edge. Near the top of the work area it flips below the word; near either side edge it is clamped,
     * so the gap survives the edge and only the horizontal alignment gives way. The gap is measured on the
     * card rather than the window: the shadow band dips over the word instead of hanging clear of it.
     *
     * @param word The word's rectangle, in DIPs.
     * @param size The window's size, shadow margin included.
     * @param area The word screen's work area, in DIPs.
     * @param gap The clear distance between the word and the card.
     * @param shadow The window's transparent shadow margin.
     * @param margin The clear band at each edge of the work area.
     * @return The window's top-left, rounded to whole DIPs.
     */
    Q_INVOKABLE QPoint reviewCard(QRectF word, QSizeF size, QRectF area, qreal gap, qreal shadow, qreal margin = kMargin) const;

    /**
     * @brief A window pulled back inside its work area after it grew.
     *
     * A surface that grows after it was placed keeps its top-left corner, so it runs off the bottom or right
     * edge by however much it grew. Only the overflow is taken back; a window already inside does not move.
     *
     * @param at The window's top-left now.
     * @param size The window's size.
     * @param area The work area of the screen holding the window.
     * @param margin The clear band at each edge of the work area.
     * @return The top-left that keeps the far edges inside; each axis collapses to the near edge when the
     *         window is larger than the work area.
     */
    Q_INVOKABLE QPoint keptOnScreen(QPointF at, QSizeF size, QRectF area, qreal margin = kMargin) const;

    /**
     * @brief A bubble window's height while its content transitions (docs/adr/0055).
     * @param contentHeight The content's height now.
     * @param heldFloor The height the hold is keeping, or 0 when none is active.
     * @param shadow The transparent shadow band on each side.
     * @return The taller of the two plus both shadow bands, rounded to a whole DIP.
     */
    Q_INVOKABLE qreal windowHeight(qreal contentHeight, qreal heldFloor, qreal shadow) const;

    /**
     * @brief The floor a height transition keeps: the height in force or the content's now, but never below
     *        the height it is heading for.
     * @param floorInForce The floor the running hold has kept; ignored when @p active is false.
     * @param active Whether a transition is already running.
     * @param contentHeight The content's height now.
     * @param target The content height the transition is heading for.
     */
    Q_INVOKABLE qreal heldFloor(qreal floorInForce, bool active, qreal contentHeight, qreal target) const;

    /**
     * @brief The DIP point of a native-pixel point, measured from the screen's own origin (docs/QML.md 4).
     *
     * Qt draws a window on a screen at native = origin + (dip - origin) * scale. Scaling from zero is right only
     * for a screen whose origin is zero; on the portrait monitor above the primary it misplaces every point by
     * 512 DIPs.
     *
     * @param native The point the hook reported, in native screen pixels.
     * @param screenOrigin The screen's virtual origin, in DIPs.
     * @param ratio The screen's device pixel ratio; a non-positive value is read as 1.
     * @return The point in DIPs.
     */
    Q_INVOKABLE QPointF dipFromNativePoint(QPointF native, QPointF screenOrigin, qreal ratio) const;

    /**
     * @brief The native-pixel point of a DIP point, measured from the same origin.
     * @return The point rounded to whole native pixels: Windows places a window on one.
     */
    Q_INVOKABLE QPoint nativeFromDipPoint(QPointF dip, QPointF screenOrigin, qreal ratio) const;

    /// @brief A DIP length from a native length on a screen of @p ratio.
    Q_INVOKABLE qreal dipFromNativeLength(qreal native, qreal ratio) const;

    /// @brief A native length from a DIP length on a screen of @p ratio, rounded to a whole pixel.
    Q_INVOKABLE int nativeFromDipLength(qreal dip, qreal ratio) const;
};

} // namespace lens::app::geometry
