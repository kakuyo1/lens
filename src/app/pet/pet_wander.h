#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "core/domain/pet/pet_config.h"
#include "core/domain/pet/pet_state.h"

/**
 * @file pet_wander.h
 * @brief The geometry behind an idle stroll and an edge hide: a home ellipse, one walk cycle per step, and the
 *        offset that leaves part of the pet visible at a screen edge (docs/adr/0076).
 *
 * Pure geometry over Qt's integer rectangles. The controller owns the clock, the state machine and the window;
 * this class only says where the window goes.
 */

namespace lens::app::pet {

/// @brief The shape of a step's path, from animations.json's movement block.
enum class Gait : std::uint8_t {
    Glide,
    Float,
    Hop,
    Trot,
    Pounce,
};

/// @brief Which screen edge a pet hid against.
enum class EdgeSide : std::uint8_t {
    Left,
    Right,
};

/// @brief The gait a data style names.
Gait gaitOf(core::pet::MovementStyle style);

/// @brief One step: the window moves from `from` to `to` in one walk cycle.
struct WanderStep {
    QPoint from;
    QPoint to;
};

/**
 * @brief The home ellipse and the step paths of one pet, in screen pixels.
 *
 * The window's top-left is the point that moves. The radius is a percentage of the window width, and the vertical
 * radius is a fraction of that, by gait; every point is clamped into the ellipse and into the screen's work area.
 */
class PetWander {
public:
    /// @brief The chance a step that ends on a screen edge becomes a hide.
    static constexpr double kHideChance{0.7};
    /// @brief The share of the figure's ink width left visible when the window slides out.
    static constexpr double kHideVisible{0.34};
    /// @brief The share of the window width that must be back inside the work area for a drag to end a hide.
    static constexpr double kDragOutFraction{0.8};
    /// @brief How far a hidden pet leans in once per peek cycle, in screen pixels at the reference size.
    static constexpr int kPeekTravel{30};
    /// @brief How long the window takes to slide out of the edge.
    static constexpr std::chrono::milliseconds kHideSlide{260};
    /// @brief One lean in and back out while hidden.
    static constexpr std::chrono::milliseconds kPeekCycle{3600};
    /// @brief The window width the fixed arc heights and peek travel are drawn for.
    static constexpr int kReferenceWidth{264};

    /**
     * @param gait The path shape.
     * @param rangePercent The home radius as a percentage of the window width, 10 to 60.
     * @param random A source in [0, 1), used to pick each step's direction and length.
     */
    PetWander(Gait gait, int rangePercent, core::pet::RandomSource random);

    /// @brief Sets the point the stroll circles around: where the reader last dropped the pet.
    void   setHome(QPoint home);
    QPoint home() const
    {
        return home_;
    }

    /// @brief Sets the window size and the work area every point is kept inside.
    void  setBounds(QSize size, QRect area);
    QRect area() const
    {
        return area_;
    }

    /**
     * @brief Picks the next step from `from`, or nothing when every candidate is too short to see.
     * @param from The window's top-left now.
     * @return The step, clamped into the home ellipse and the work area.
     */
    std::optional<WanderStep> nextStep(QPoint from);

    /**
     * @brief Where the window is `progress` through `step`.
     * @param progress 0 at the start, 1 at the end; clamped.
     * @return The point, including the gait's small vertical arc and the clamps.
     */
    QPoint positionAt(WanderStep const& step, double progress) const;

    /// @return The edge a point sits on, within one pixel, or nothing when it is inside.
    std::optional<EdgeSide> edgeAt(QPoint at) const;
    bool                    hides(double roll) const
    {
        return roll < kHideChance;
    }

    /**
     * @brief The hidden window's left edge.
     * @param area The screen's work area.
     * @param size The window size.
     * @param side The edge it hides against.
     * @param inkLeft The figure's left ink edge in screen pixels, from the window's left.
     * @param inkRight The figure's right ink edge in screen pixels.
     * @return The x that leaves kHideVisible of the ink inside the area. Callers clamp y.
     */
    static int hiddenX(QRect area, QSize size, EdgeSide side, double inkLeft, double inkRight);

    /// @return How far the hidden window leans in `clock` into the peek cycle, in screen pixels.
    static int peekOffset(std::chrono::milliseconds clock);

    /// @return The window position `elapsed` into the slide from `from` to `base`, eased out.
    static QPoint slideAt(QPoint from, QPoint base, std::chrono::milliseconds elapsed);

    /// @return Whether a drop has brought at least kDragOutFraction of the window's width back inside `area`.
    static bool draggedOut(QPoint at, QSize size, QRect area);

private:
    /// @return `at` clamped into the work area and then into the home ellipse.
    QPoint clamped(QPoint at) const;
    /// @return The vertical radius of the home ellipse, as a fraction of the horizontal one.
    double verticalRatio() const;
    /// @return The vertical arc height the gait adds, in screen pixels for this window size.
    double arcHeight(double reference) const;

    Gait                    gait_;
    int                     rangePercent_;
    core::pet::RandomSource random_;
    QPoint                  home_{};
    QSize                   size_{kReferenceWidth, kReferenceWidth};
    QRect                   area_{};
};

} // namespace lens::app::pet
